#include "cores3_servo.h"

#include <cmath>
#include <esp_log.h>
#include <esp_timer.h>

#include "FTServo/SCSCL.h"
#include "mcp_server.h"
#include "settings.h"     // ★ NVS 持久化（开机默认角度存这里）

#define TAG "CoreS3Servo"

// ── 硬件参数（与官方 stackchan 固件一致，源码实读）
static constexpr uart_port_t kUart = UART_NUM_1;
static constexpr int kBaud = 1000000;   // 1 Mbps
static constexpr int kTxPin = 6;
static constexpr int kRxPin = 7;
static constexpr u8 kYawId = 1;
static constexpr u8 kPitchId = 2;

CoreS3Servo::CoreS3Servo() = default;

CoreS3Servo::~CoreS3Servo() {
    // ★ 飞特驱动的 SCS 基类没有虚析构函数，delete SCSCL* 会触发
    //   -Werror=delete-non-virtual-dtor 编译失败。
    //   本对象生命周期与设备一致（全程存在），不做 delete，避免 UB。
    bus_ = nullptr;
}

// ── 限位与换算 ──────────────────────────────────────────────
float CoreS3Servo::ClampYaw(float d) const {
    if (d < kYawMin) return kYawMin;
    if (d > kYawMax) return kYawMax;
    return d;
}

float CoreS3Servo::ClampPitch(float d) const {
    if (d < kPitchMin) return kPitchMin;
    // 堵转后上限会被收紧
    float hi = pitch_limit_deg_ < kPitchMax ? pitch_limit_deg_ : kPitchMax;
    if (d > hi) return hi;
    return d;
}

// 官方换算（hal_servo.cpp L76）：raw = zero + angle*16/5/10
//   angle 单位 0.1° → raw 一步 = 0.3125°
int CoreS3Servo::AngleToRaw(int zero, float deg) const {
    return zero + static_cast<int>(deg * 10.0f * 16.0f / 5.0f / 10.0f);
}

// 官方换算（L206）：angle = (raw - zero) * 5 * 10 / 16  → 0.1° 单位
float CoreS3Servo::RawToAngle(int zero, int raw) const {
    return static_cast<float>(raw - zero) * 5.0f * 10.0f / 16.0f / 10.0f;
}

// ── 初始化 ─────────────────────────────────────────────────
bool CoreS3Servo::Init() {
    bus_ = new SCSCL();
    if (bus_ == nullptr) {
        ESP_LOGE(TAG, "alloc SCSCL failed");
        return false;
    }

    // 1 Mbps, TX=GPIO6, RX=GPIO7（与官方 hal_servo.cpp 完全一致）
    if (!bus_->begin(kUart, kBaud, kTxPin, kRxPin)) {
        ESP_LOGE(TAG, "uart begin failed (UART%d %d bps TX=%d RX=%d)",
                 (int)kUart, kBaud, kTxPin, kRxPin);
        return false;
    }
    ESP_LOGI(TAG, "UART ready: UART%d %d bps TX=%d RX=%d",
             (int)kUart, kBaud, kTxPin, kRxPin);

    // 使能扭矩
    bus_->EnableTorque(kYawId, 1);
    bus_->EnableTorque(kPitchId, 1);

    // 探测：能否读到位置（读不到说明舵机没上电/接线不对）
    int yaw_raw = bus_->ReadPos(kYawId);
    int pitch_raw = bus_->ReadPos(kPitchId);
    if (yaw_raw < 0 && pitch_raw < 0) {
        ESP_LOGW(TAG, "no servo response (check servo power / VM EN)");
        // 不算致命错误：保留 ready_=false，其余功能可用
        return false;
    }

    ESP_LOGI(TAG, "servo ok: yaw_id=%d raw=%d  pitch_id=%d raw=%d",
             kYawId, yaw_raw, kPitchId, pitch_raw);

    // ★ 载入用户保存的「开机默认角度」
    LoadHomeFromNvs();

    // 回中到用户设定的默认角度（未改过就是 yaw=0°, pitch=45°）
    ready_ = true;
    Center(400);
    return true;
}

// ── 开机默认角度（回中位）的 NVS 持久化 ──────────────────────
// ★ Settings 只有 GetString/SetString/GetInt/SetInt/GetBool/SetBool
//   （读 settings.h 确认）⇒ 存成 "yaw,pitch" 字符串。
//   角度是 float，乘 10 存成整数可保留 0.1° 精度。
void CoreS3Servo::LoadHomeFromNvs() {
    Settings settings("fairy_servo", false);   // 只读
    std::string v = settings.GetString("home", "");
    if (v.empty()) {
        ESP_LOGI(TAG, "no saved home, use default yaw=%.1f pitch=%.1f",
                 home_yaw_, home_pitch_);
        return;
    }
    int y10 = 0, p10 = 0;
    if (sscanf(v.c_str(), "%d,%d", &y10, &p10) != 2) {
        ESP_LOGW(TAG, "home 格式异常: %s", v.c_str());
        return;
    }
    home_yaw_ = ClampYaw(static_cast<float>(y10) / 10.0f);
    home_pitch_ = ClampPitch(static_cast<float>(p10) / 10.0f);
    ESP_LOGI(TAG, "NVS home = yaw=%.1f pitch=%.1f",
             home_yaw_, home_pitch_);
}

bool CoreS3Servo::SaveHomeToNvs(float yaw_deg, float pitch_deg) {
    float y = ClampYaw(yaw_deg);
    float p = ClampPitch(pitch_deg);
    Settings settings("fairy_servo", true);    // 可读写
    char val[24];
    snprintf(val, sizeof(val), "%d,%d",
             static_cast<int>(y * 10.0f), static_cast<int>(p * 10.0f));
    settings.SetString("home", val);
    home_yaw_ = y;
    home_pitch_ = p;
    ESP_LOGI(TAG, "home saved: yaw=%.1f pitch=%.1f", y, p);
    return true;
}

// ── 动作 ───────────────────────────────────────────────────
bool CoreS3Servo::SetYawAngle(float deg, uint16_t speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || bus_ == nullptr) {
        return false;
    }
    float d = ClampYaw(deg);
    int raw = AngleToRaw(yaw_zero_, d);
    bus_->WritePos(kYawId, static_cast<u16>(raw), 0, speed);
    return true;
}

// pitch 带堵转保护（官方 hal_servo.cpp 的安全机制，移植）
bool CoreS3Servo::SetPitchAngle(float deg, uint16_t speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || bus_ == nullptr) {
        return false;
    }
    float d = ClampPitch(deg);
    int raw = AngleToRaw(pitch_zero_, d);
    last_pitch_cmd_raw_ = raw;
    bus_->WritePos(kPitchId, static_cast<u16>(raw), 0, speed);
    CheckPitchStall(raw);
    return true;
}

bool CoreS3Servo::MoveTo(float yaw_deg, float pitch_deg, uint16_t speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || bus_ == nullptr) {
        return false;
    }
    float yd = ClampYaw(yaw_deg);
    float pd = ClampPitch(pitch_deg);
    int yraw = AngleToRaw(yaw_zero_, yd);
    int praw = AngleToRaw(pitch_zero_, pd);

    bus_->WritePos(kYawId, static_cast<u16>(yraw), 0, speed);
    last_pitch_cmd_raw_ = praw;
    bus_->WritePos(kPitchId, static_cast<u16>(praw), 0, speed);
    CheckPitchStall(praw);
    return true;
}

bool CoreS3Servo::Center(uint16_t speed) {
    // ★ 回中到用户保存的「开机默认角度」（未改过 = yaw 0°, pitch 45°）
    return MoveTo(home_yaw_, home_pitch_, speed);
}

float CoreS3Servo::GetYawAngle() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || bus_ == nullptr) {
        return -999.0f;
    }
    int raw = bus_->ReadPos(kYawId);
    if (raw < 0) {
        return -999.0f;
    }
    return RawToAngle(yaw_zero_, raw);
}

float CoreS3Servo::GetPitchAngle() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || bus_ == nullptr) {
        return -999.0f;
    }
    int raw = bus_->ReadPos(kPitchId);
    if (raw < 0) {
        return -999.0f;
    }
    return RawToAngle(pitch_zero_, raw);
}

// ── pitch 堵转保护（防烧舵机）
//    参数与判定逻辑取自官方 hal_servo.cpp L177-318
void CoreS3Servo::CheckPitchStall(int target_raw) {
    if (!pitch_stall_enabled_) {
        return;
    }
    unsigned long now = static_cast<unsigned long>(
        esp_timer_get_time() / 1000);
    if (now - last_stall_check_ms_ < kStallFeedbackIntervalMs) {
        return;
    }
    last_stall_check_ms_ = now;

    // 目标与当前差太小 → 不检查（本来就没想动）
    int cur_raw = bus_->ReadPos(kPitchId);
    if (cur_raw < 0) {
        return;
    }
    if (abs(target_raw - cur_raw) < kStallMinTargetDeltaRaw) {
        stall_hit_count_ = 0;
        return;
    }

    int cur = bus_->ReadCurrent(kPitchId);
    int load = bus_->ReadLoad(kPitchId);
    if (cur < 0 || load < 0) {
        return;
    }

    bool stall = (cur > kStallCurrentAbsThreshold) ||
                 (load > kStallLoadAbsThreshold);
    if (stall) {
        stall_hit_count_++;
    } else {
        stall_hit_count_ = 0;
        return;
    }

    if (stall_hit_count_ >= kStallConfirmSamples) {
        // 确认堵转 → 收紧上限、停止动作、就地在当前位置落点
        ESP_LOGW(TAG, "pitch stall detected (cur=%d load=%d) → limiting",
                 cur, load);
        float cur_deg = RawToAngle(pitch_zero_, cur_raw);
        if (cur_deg > kPitchMin + 5.0f) {
            pitch_limit_deg_ = cur_deg - 2.0f;   // 收紧
        }
        bus_->WritePos(kPitchId, static_cast<u16>(cur_raw), 0, 0);
        stall_hit_count_ = 0;
    }
}

// ── MCP 工具（让 AI 能控制转头，免编译调参）
void CoreS3Servo::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.servo.set_angles",
        "设置机器人脖子的两个舵机角度（转头/抬头）。"
        "yaw 是水平转头角度，单位度，范围 -128~128，0 为正前方；"
        "pitch 是俯仰角度，单位度，范围 3~87，约 45 为水平；"
        "speed 是转动速度 0~1000，默认 600。",
        PropertyList({
            Property("yaw", kPropertyTypeInteger, -128, 128),
            Property("pitch", kPropertyTypeInteger, 3, 87),
            // ★ speed 必须给【默认值】：PropertyList::operator[] 找不到字段
            //   会直接 esp_system_abort()，不能让 AI 漏传就崩设备。
            Property("speed", kPropertyTypeInteger, 600, 0, 1000),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            int yaw = p["yaw"].value<int>();
            int pitch = p["pitch"].value<int>();
            int speed = p["speed"].value<int>();   // 有默认值 600
            if (!IsReady()) {
                return false;
            }
            return MoveTo(static_cast<float>(yaw),
                          static_cast<float>(pitch),
                          static_cast<uint16_t>(speed));
        });

    mcp.AddTool("self.servo.center",
        "让机器人的脖子回到正前方（回中）。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            if (!IsReady()) {
                return false;
            }
            return Center(400);
        });

    mcp.AddTool("self.servo.look_around",
        "让机器人左右张望一下（先左后右再回中）。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            if (!IsReady()) {
                return false;
            }
            SetYawAngle(-60.0f, 500);
            vTaskDelay(pdMS_TO_TICKS(500));
            SetYawAngle(60.0f, 500);
            vTaskDelay(pdMS_TO_TICKS(500));
            SetYawAngle(0.0f, 500);
            return true;
        });

    mcp.AddTool("self.servo.get_angles",
        "读取机器人脖子当前的两个舵机角度。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "yaw=%.1f deg, pitch=%.1f deg (ready=%d)",
                     GetYawAngle(), GetPitchAngle(), IsReady() ? 1 : 0);
            return std::string(buf);
        });

    // ★ 设置「开机默认角度」并存入 NVS（重启后仍生效）
    //   用户说「以后开机就朝左边」时用这个。
    mcp.AddTool("self.servo.set_default",
        "设置机器人开机时的默认姿势（回中位），永久保存到设备。"
        "yaw 是水平角度 -128~128（0=正前）；pitch 是俯仰 3~87（约45=水平）。"
        "用户说「以后开机就抬头」「默认朝左」时用这个。",
        PropertyList({
            Property("yaw", kPropertyTypeInteger, 0, -128, 128),
            Property("pitch", kPropertyTypeInteger, 45, 3, 87),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            int yaw = p["yaw"].value<int>();
            int pitch = p["pitch"].value<int>();
            if (!IsReady()) {
                return std::string("舵机未就绪，无法保存");
            }
            SaveHomeToNvs(static_cast<float>(yaw),
                          static_cast<float>(pitch));
            Center(400);   // 立即回到新默认位
            char buf[80];
            snprintf(buf, sizeof(buf),
                     "开机默认姿势已设为 yaw=%d pitch=%d 并保存", yaw, pitch);
            return std::string(buf);
        });

    // ★ 读回已保存的开机默认角度
    mcp.AddTool("self.servo.get_default",
        "读取机器人已保存的开机默认姿势。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            char buf[64];
            snprintf(buf, sizeof(buf), "yaw=%.1f,pitch=%.1f",
                     GetHomeYaw(), GetHomePitch());
            return std::string(buf);
        });

    ESP_LOGI(TAG, "registered 6 MCP tools for servo");
}
