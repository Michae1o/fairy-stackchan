#include "cores3_py32_led.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "mcp_server.h"   // MCP 工具（让 AI 能控制灯）
#include "settings.h"     // ★ NVS 持久化（配色存这里，重启仍生效）

#define TAG "CoreS3Py32Led"

// ★ 各状态配色已改为 CoreS3Py32Led 的实例成员（见 .h），
//   由 NVS 持久化，可在控制台改而不必重编固件。
//   本节只保留「状态名 → NVS 键」的映射与读写辅助。

static constexpr int kLedCount = 12;
static constexpr uint8_t kRgbPin = 13;   // IO 扩展上 RGB 的引脚
static constexpr uint8_t kServoPowerPin = 0;  // VM EN

// 7 个可配状态的名字（顺序与 kStateNames 一致）
const char* const CoreS3Py32Led::kStateNames[7] = {
    "idle", "connecting", "listening", "speaking",
    "notifying", "error", "upgrading"
};

bool* CoreS3Py32Led::ModeByName(const std::string& name) {
    if (name == "idle")       return &mode_idle_;
    if (name == "connecting") return &mode_connecting_;
    if (name == "listening")  return &mode_listening_;
    if (name == "speaking")   return &mode_speaking_;
    if (name == "notifying")  return &mode_notifying_;
    if (name == "error")      return &mode_error_;
    if (name == "upgrading")  return &mode_upgrading_;
    return nullptr;
}

// ★ 键格式 "mode_<state>"，命名空间沿用 "fairy_rgb"（与颜色在一起）
void CoreS3Py32Led::SaveModeToNvs(const char* name, bool breath) {
    Settings settings("fairy_rgb", true);
    char key[32];
    snprintf(key, sizeof(key), "mode_%s", name);
    settings.SetInt(key, breath ? 1 : 0);
}

bool CoreS3Py32Led::LoadModeFromNvs(const char* name, bool* out) {
    Settings settings("fairy_rgb", false);
    char key[32];
    snprintf(key, sizeof(key), "mode_%s", name);
    // ★ 本项目的 Settings 类【没有 ContainsKey】
    //   （只有 GetString/GetInt/GetBool/SetXxx/EraseKey/EraseAll）
    //   ⇒ 用哨兵值判断"有没有存过"：存 1=呼吸 / 0=常亮，默认 -1=没存过
    int32_t v = settings.GetInt(key, -1);
    if (v < 0) {
        return false;      // 没存过 ⇒ 保持编译期默认
    }
    *out = (v != 0);
    return true;
}

// ★ 开机载入各状态亮法（没存过就保持编译期默认）
void CoreS3Py32Led::LoadModesFromNvs() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 7; i++) {
        bool* p = ModeByName(kStateNames[i]);
        if (p == nullptr) {
            continue;
        }
        bool tmp = false;
        if (LoadModeFromNvs(kStateNames[i], &tmp)) {
            *p = tmp;
            ESP_LOGI(TAG, "NVS mode %s = %s", kStateNames[i],
                     tmp ? "breath" : "static");
        }
    }
}

CoreS3Py32Led::Rgb* CoreS3Py32Led::ColorByName(const std::string& name) {
    if (name == "idle")       return &col_idle_;
    if (name == "connecting") return &col_connecting_;
    if (name == "listening")  return &col_listening_;
    if (name == "speaking")   return &col_speaking_;
    if (name == "notifying")  return &col_notifying_;
    if (name == "error")      return &col_error_;
    if (name == "upgrading")  return &col_upgrading_;
    return nullptr;
}

// ── NVS 持久化 ────────────────────────────────────────────────
// ★ Settings 只有 GetString/SetString/GetInt/SetInt/GetBool/SetBool，
//   没有 Blob 接口（读 settings.h 确认）⇒ 配色存成 "r,g,b" 字符串。
// 键格式 "col_<state>"，命名空间 "fairy_rgb"。
void CoreS3Py32Led::SaveColorToNvs(const char* name, const Rgb& c) {
    Settings settings("fairy_rgb", true);   // true = 可读写
    char key[24];
    snprintf(key, sizeof(key), "col_%s", name);
    char val[16];
    snprintf(val, sizeof(val), "%d,%d,%d", c.r, c.g, c.b);
    settings.SetString(key, val);
}

bool CoreS3Py32Led::LoadColorFromNvs(const char* name, Rgb* out) {
    if (out == nullptr) return false;
    Settings settings("fairy_rgb", false);  // false = 只读
    char key[24];
    snprintf(key, sizeof(key), "col_%s", name);
    std::string v = settings.GetString(key, "");
    if (v.empty()) {
        return false;   // 没存过 ⇒ 保留编译期默认值
    }
    int r = 0, g = 0, b = 0;
    if (sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) != 3) {
        ESP_LOGW(TAG, "NVS color %s 格式异常: %s", name, v.c_str());
        return false;
    }
    out->r = static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
    out->g = static_cast<uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g));
    out->b = static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
    return true;
}

void CoreS3Py32Led::LoadColorsFromNvs() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 7; i++) {
        Rgb* p = ColorByName(kStateNames[i]);
        if (p == nullptr) continue;
        Rgb tmp;
        if (LoadColorFromNvs(kStateNames[i], &tmp)) {
            *p = tmp;
            ESP_LOGI(TAG, "NVS color %s = {%d,%d,%d}",
                     kStateNames[i], tmp.r, tmp.g, tmp.b);
        }
    }
}

CoreS3Py32Led::CoreS3Py32Led(i2c_master_bus_handle_t i2c_bus) {
    io_expander_ = new m5::PY32IOExpander_Class(i2c_bus);
}

CoreS3Py32Led::~CoreS3Py32Led() {
    StopBreath();
    if (breath_timer_ != nullptr) {
        esp_timer_delete(breath_timer_);
        breath_timer_ = nullptr;
    }
    if (io_expander_ != nullptr) {
        SetAll(0, 0, 0);
        delete io_expander_;
        io_expander_ = nullptr;
    }
}

bool CoreS3Py32Led::Init() {
    if (io_expander_ == nullptr) {
        ESP_LOGE(TAG, "io_expander_ is null");
        return false;
    }
    // PY32 芯片启动较慢，重试等待
    bool ok = false;
    for (int i = 0; i < 6; i++) {
        if (io_expander_->begin()) {
            ok = true;
            break;
        }
        ESP_LOGW(TAG, "PY32 begin failed, retry %d", i + 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!ok) {
        ESP_LOGE(TAG, "PY32 IO expander not found (addr 0x6F)");
        return false;
    }

    // RGB 引脚：输出 + 上拉 + 推挽
    io_expander_->setDirection(kRgbPin, true);
    io_expander_->setPullMode(kRgbPin, true);
    io_expander_->setDriveMode(kRgbPin, false);
    io_expander_->setLedCount(kLedCount);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 清零两次，确保灯珠复位（官方固件也是这么做的）
    SetAll(0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    SetAll(0, 0, 0);

    // 舵机电源：输出 + 上拉，并使能
    io_expander_->setDirection(kServoPowerPin, true);
    io_expander_->setPullMode(kServoPowerPin, true);
    io_expander_->digitalWrite(kServoPowerPin, true);

    esp_timer_create_args_t args = {
        .callback = &CoreS3Py32Led::BreathTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fairy_rgb_breath",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &breath_timer_);

    inited_ = true;

    // ★ 载入用户保存过的配色（NVS）。没存过就保留编译期默认值。
    //   必须在 inited_ = true 之后调（LoadColorsFromNvs 会拿锁）。
    LoadColorsFromNvs();
    LoadModesFromNvs();  // ★ 载入各状态的亮法（常亮/呼吸）

    ESP_LOGI(TAG, "RGB ready: %d LEDs on PY32 pin %d", kLedCount, kRgbPin);
    return true;
}

void CoreS3Py32Led::SetAll(uint8_t r, uint8_t g, uint8_t b) {
    if (!inited_ || io_expander_ == nullptr) {
        return;
    }
    // ★ 颜色没变就不刷 —— 呼吸到三角波端点时会出现重复亮度，
    //   跳过可省一次 13 笔 I2C（12 设色 + 1 refresh），
    //   减少和音频任务抢 PY32 总线的机会。
    if (r == cur_r_ && g == cur_g_ && b == cur_b_) {
        return;
    }
    // 左右两组灯（0~5 / 6~11）都设成同色
    for (int i = 0; i < kLedCount; i++) {
        io_expander_->setLedColor(static_cast<uint8_t>(i), r, g, b);
    }
    io_expander_->refreshLeds();
    cur_r_ = r;
    cur_g_ = g;
    cur_b_ = b;
}

void CoreS3Py32Led::SetBrightnessColor(uint8_t r, uint8_t g, uint8_t b,
                                       int bright) {
    // bright: 0~100
    if (bright < 0) bright = 0;
    if (bright > 100) bright = 100;
    uint8_t rr = static_cast<uint8_t>(static_cast<uint32_t>(r) * bright / 100);
    uint8_t gg = static_cast<uint8_t>(static_cast<uint32_t>(g) * bright / 100);
    uint8_t bb = static_cast<uint8_t>(static_cast<uint32_t>(b) * bright / 100);
    SetAll(rr, gg, bb);
}

void CoreS3Py32Led::BreathTimerCb(void* arg) {
    auto* self = static_cast<CoreS3Py32Led*>(arg);
    if (self != nullptr) {
        self->OnBreathTick();
    }
}

void CoreS3Py32Led::OnBreathTick() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!breathing_) {
        return;
    }
    // 三角波呼吸：0 -> kBreathPeak -> 0
    // ★ 用户反馈「呼吸看着巨卡」—— 原为每步 5%（共 41 步）、
    //   周期 100ms ⇒ 台阶肉眼可数 + 一轮 4 秒太慢。
    //   现改为每步 1%（共 201 步）、周期 33ms（≈30fps），
    //   台阶消失，一轮约 6.6 秒…不对，是 201×33ms ≈ 6.6s。
    //   ⇒ 实际取 2%/步 + 33ms：约 101 步 × 33ms ≈ 3.3s 一轮，
    //     既无台阶又不会慢得发闷。
    breath_step_ += kBreathStep;                 // 见头文件：= 2
    if (breath_step_ > kBreathPeak * 2) {
        breath_step_ = 0;
    }
    int bright = breath_step_ <= kBreathPeak
                     ? breath_step_
                     : kBreathPeak * 2 - breath_step_;
    if (bright < 2) {
        bright = 2;  // 不要全灭，保持可见（原为 5，平台期太长）
    }
    SetBrightnessColor(breath_r_, breath_g_, breath_b_, bright);
}

void CoreS3Py32Led::StartBreath(uint8_t r, uint8_t g, uint8_t b) {
    if (!inited_) {
        return;
    }
    breath_r_ = r;
    breath_g_ = g;
    breath_b_ = b;
    if (breathing_) {
        return;  // 已在呼吸，只换颜色
    }
    breathing_ = true;
    breath_step_ = 0;
    if (breath_timer_ != nullptr) {
        esp_timer_start_periodic(breath_timer_,
                               kBreathPeriodMs * 1000);  // ★ 见头文件
    }
}

void CoreS3Py32Led::StopBreath() {
    breathing_ = false;
    if (breath_timer_ != nullptr) {
        esp_timer_stop(breath_timer_);
    }
}

// ★ 按【亮法】设置某状态的颜色：呼吸 或 常亮
//   （把原来写死在 ApplyStateColor 里的 StartBreath/SetAll 二选一集中到这儿）
void CoreS3Py32Led::ApplyOne(const Rgb& c, bool breath) {
    if (breath) {
        StartBreath(c.r, c.g, c.b);
    } else {
        StopBreath();
        SetAll(c.r, c.g, c.b);
    }
}

void CoreS3Py32Led::ApplyStateColor() {
    auto& app = Application::GetInstance();
    DeviceState st = app.GetDeviceState();

    switch (st) {
        // ★ 全部改为按【可配置的亮法】执行（用户可在控制台选 常亮/呼吸）
        //   取值顺序：NVS 存过 → 用它；没存过 → 用编译期默认（= 旧行为）
        case kDeviceStateIdle:
            ApplyOne(col_idle_, mode_idle_);
            break;

        case kDeviceStateConnecting:
            ApplyOne(col_connecting_, mode_connecting_);
            break;

        case kDeviceStateListening:
            ApplyOne(col_listening_, mode_listening_);
            break;

        case kDeviceStateSpeaking:
            ApplyOne(col_speaking_, mode_speaking_);
            break;

        case kDeviceStateNotifying:
            ApplyOne(col_notifying_, mode_notifying_);
            break;

        case kDeviceStateUpgrading:
        case kDeviceStateActivating:
            ApplyOne(col_upgrading_, mode_upgrading_);
            break;

        case kDeviceStateFatalError:
            ApplyOne(col_error_, mode_error_);
            break;

        case kDeviceStateWifiConfiguring:
            // 配网：蓝黄交替（用呼吸近似）
            StartBreath(col_connecting_.r, col_connecting_.g, col_connecting_.b);
            break;

        case kDeviceStateStarting:
        case kDeviceStateAudioTesting:
        case kDeviceStateUnknown:
        default:
            StartBreath(col_idle_.r, col_idle_.g, col_idle_.b);
            break;
    }
}

void CoreS3Py32Led::OnStateChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_) {
        return;
    }
    // ★ 若被 AI/用户手动接管（关灯/指定颜色），状态灯不再覆盖
    if (manual_override_) {
        return;
    }
    ApplyStateColor();
}

// ── MCP 工具（让 AI 能控制灯，免编译调参）──────────────────────
// ★ 与舵机那套保持一致的风格（cores3_servo.cc 的 RegisterMcpTools）。
// ★ 每个 Property 都必须给默认值：PropertyList::operator[] 找不到字段
//   会直接 esp_system_abort()，不能让 AI 漏传就崩设备。
void CoreS3Py32Led::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.led.set_color",
        "设置机器人身上 12 颗 RGB 灯的颜色。"
        "r/g/b 是 0~255 的分量；bright 是亮度百分比 0~100，默认 100。"
        "例如蓝色是 r=0,g=80,b=255。设置后会保持该颜色，不再随对话状态变化。",
        PropertyList({
            Property("r", kPropertyTypeInteger, 0, 0, 255),
            Property("g", kPropertyTypeInteger, 0, 0, 255),
            Property("b", kPropertyTypeInteger, 0, 0, 255),
            Property("bright", kPropertyTypeInteger, 100, 0, 100),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!inited_) {
                return false;
            }
            int r = p["r"].value<int>();
            int g = p["g"].value<int>();
            int b = p["b"].value<int>();
            int bright = p["bright"].value<int>();
            manual_override_ = true;
            StopBreath();
            SetBrightnessColor(static_cast<uint8_t>(r),
                               static_cast<uint8_t>(g),
                               static_cast<uint8_t>(b), bright);
            return true;
        });

    mcp.AddTool("self.led.off",
        "关掉机器人身上的所有 RGB 灯（变黑）。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!inited_) {
                return false;
            }
            manual_override_ = true;
            StopBreath();
            SetAll(0, 0, 0);
            return true;
        });

    mcp.AddTool("self.led.auto",
        "让 RGB 灯恢复成【跟随对话状态】的自动模式"
        "（待机暗紫呼吸、聆听亮青、说话紫粉）。"
        "用户说「让灯恢复正常」时用这个。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!inited_) {
                return false;
            }
            manual_override_ = false;
            ApplyStateColor();
            return true;
        });

    mcp.AddTool("self.led.get_state",
        "读取 RGB 灯当前状态。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "r=%d,g=%d,b=%d,breathing=%d,auto=%d,inited=%d",
                     static_cast<int>(cur_r_), static_cast<int>(cur_g_),
                     static_cast<int>(cur_b_), breathing_ ? 1 : 0,
                     manual_override_ ? 0 : 1, inited_ ? 1 : 0);
            return std::string(buf);
        });

    // ★ 设置某状态的默认配色并【存入 NVS】（重启后仍生效）
    //   这是"控制台改配色"的落点：控制台改完 → 调这个工具 → 设备记住。
    //   state 支持：idle/connecting/listening/speaking/notifying/error/upgrading
    mcp.AddTool("self.led.set_default",
        "设置某个对话状态下的默认灯色，并永久保存到设备（重启后仍生效）。"
        "state 取 idle(待机)/connecting(连接中)/listening(聆听)/"
        "speaking(说话)/notifying(通知)/error(错误)/upgrading(升级)；"
        "r/g/b 为 0~255 颜色分量。用户说「把待机灯改成蓝色」时用这个。",
        PropertyList({
            Property("state", kPropertyTypeString),
            Property("r", kPropertyTypeInteger, 0, 0, 255),
            Property("g", kPropertyTypeInteger, 0, 0, 255),
            Property("b", kPropertyTypeInteger, 0, 0, 255),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string name = p["state"].value<std::string>();
            Rgb* target = ColorByName(name);
            if (target == nullptr) {
                return std::string("未知状态: " + name +
                    "（可用: idle/connecting/listening/speaking/"
                    "notifying/error/upgrading）");
            }
            target->r = static_cast<uint8_t>(p["r"].value<int>());
            target->g = static_cast<uint8_t>(p["g"].value<int>());
            target->b = static_cast<uint8_t>(p["b"].value<int>());
            SaveColorToNvs(name.c_str(), *target);
            // 若当前正处在该状态，立即生效
            ApplyStateColor();
            char buf[96];
            snprintf(buf, sizeof(buf), "%s 已设为 {%d,%d,%d} 并保存",
                     name.c_str(), target->r, target->g, target->b);
            return std::string(buf);
        });

    // ★ 设置某状态的【亮法】：常亮 or 呼吸（用户要求"开放成可选"）
    mcp.AddTool("self.led.set_mode",
        "设置某个对话状态下灯光的亮法：常亮(static) 或 呼吸(breath)，"
        "并永久保存到设备。state 取 idle(待机)/connecting(连接中)/"
        "listening(聆听)/speaking(说话)/notifying(通知)/error(错误)/"
        "upgrading(升级)。用户说「把待机灯改成呼吸」时用这个。",
        PropertyList({
            Property("state", kPropertyTypeString),
            Property("mode", kPropertyTypeString),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string name = p["state"].value<std::string>();
            std::string md = p["mode"].value<std::string>();
            bool* target = ModeByName(name);
            if (target == nullptr) {
                return std::string("未知状态: " + name);
            }
            if (md != "static" && md != "breath") {
                return std::string("mode 只能是 static 或 breath，收到: " + md);
            }
            *target = (md == "breath");
            SaveModeToNvs(name.c_str(), *target);
            ApplyStateColor();
            return std::string(name + " 亮法已设为 " +
                (md == "breath" ? "呼吸" : "常亮") + " 并保存");
        });

    // ★ 读回某状态的亮法
    mcp.AddTool("self.led.get_mode",
        "读取某个对话状态下已保存的灯光亮法（static=常亮 / breath=呼吸）。",
        PropertyList({
            Property("state", kPropertyTypeString),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string name = p["state"].value<std::string>();
            bool* target = ModeByName(name);
            if (target == nullptr) {
                return std::string("未知状态: " + name);
            }
            return std::string(*target ? "breath" : "static");
        });

    // ★ 读回某状态的默认配色（控制台打开时用它回填输入框）
    mcp.AddTool("self.led.get_default",
        "读取某个对话状态下已保存的默认灯色。"
        "state 取 idle/connecting/listening/speaking/notifying/error/upgrading。",
        PropertyList({
            Property("state", kPropertyTypeString),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string name = p["state"].value<std::string>();
            Rgb* target = ColorByName(name);
            if (target == nullptr) {
                return std::string("未知状态: " + name);
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%d,%d,%d",
                     target->r, target->g, target->b);
            return std::string(buf);
        });

    ESP_LOGI(TAG, "registered 6 MCP tools for led");
}
