#include "stackchan_sensor.h"

#include <cmath>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "drivers/Si12T/Si12T.h"
#include "mcp_server.h"

#define TAG "StackChanSensor"

// ★★★ BMI270 的 I2C 地址 = 0x69（不是 0x68！）
//   官方源码依据：official-stackchan/firmware/main/hal/hal_imu.cpp L57
//       _bmi270 = std::make_unique<BMI270>(i2c_bus, 0x69);
//   ⇒ 这块板子 BMI270 的 SDO 接 VCC ⇒ 0x69
//   ⇒ 与 Si12T 的 0x68 【不冲突】！
//   ⛔ 我早前写 0x68 并断言"冲突"是【错的】，
//      导致连续几轮误报"甩晕做不到"。此处纠正。
#define BMI270_ADDR 0x69

StackChanSensor::StackChanSensor(i2c_master_bus_handle_t i2c_bus)
    : i2c_bus_(i2c_bus) {}

StackChanSensor::~StackChanSensor() {
    if (si12t_ != nullptr) {
        si12t_delete((si12t_handle_t)si12t_);
        si12t_ = nullptr;
    }
}

bool StackChanSensor::Init() {
    if (i2c_bus_ == nullptr) {
        ESP_LOGW(TAG, "I2C bus 为空，传感器不启用");
        return false;
    }

    // ── ① SI12T 触摸（地址 0x68）──
    esp_err_t ret = i2c_master_probe(i2c_bus_, SI12T_GND_ADDRESS,
                                     pdMS_TO_TICKS(200));
    if (ret == ESP_OK) {
        si12t_config_t cfg = {};
        cfg.i2c_bus = i2c_bus_;
        cfg.dev_addr = SI12T_GND_ADDRESS;
        si12t_handle_t h = nullptr;
        if (si12t_init(&cfg, &h) == ESP_OK) {
            si12t_ = (void*)h;
            si12t_setup(h, SI12T_TYPE_LOW, SI12T_SENSITIVITY_LEVEL_3);
            si12t_enable_channel(h);
            si12t_sleep_disable(h);
            touch_ok_ = true;
            ESP_LOGI(TAG, "SI12T 触摸已启用");
        } else {
            ESP_LOGW(TAG, "SI12T 初始化失败");
        }
    } else {
        ESP_LOGW(TAG, "I2C 上没找到 SI12T（0x%02X），触摸不启用",
                 SI12T_GND_ADDRESS);
    }

    // ── ② BMI270 加速度计（★ 地址 0x69，不是 0x68）──
    // ★★★ 重要纠正：官方源码 official-stackchan/firmware/main/hal/hal_imu.cpp
    //     L57:  _bmi270 = std::make_unique<BMI270>(i2c_bus, 0x69);
    //     ⇒ 这块板子 BMI270 的 SDO 接 VCC ⇒ 地址是 0x69
    //     ⇒ 与 Si12T 的 0x68 【不冲突】，两者可同时使用！
    //     ⛔ 我早前猜成 0x68 并断言"冲突、IMU 用不了"——那是错的，
    //        导致连续几轮误报"甩晕做不到"。此处纠正。
    {
        esp_err_t probe = i2c_master_probe(i2c_bus_, BMI270_ADDR,
                                           pdMS_TO_TICKS(200));
        if (probe == ESP_OK) {
            bmi270_ = std::make_unique<BMI270>(i2c_bus_, BMI270_ADDR);
            if (bmi270_->begin()) {
                imu_ok_ = true;
                ESP_LOGI(TAG, "BMI270 加速度计已启用（0x%02X）"
                              "⇒ 甩晕可用", BMI270_ADDR);
            } else {
                bmi270_.reset();
                ESP_LOGW(TAG, "BMI270 begin() 失败（0x%02X）", BMI270_ADDR);
            }
        } else {
            ESP_LOGW(TAG, "I2C 上没找到 BMI270（0x%02X），甩晕不启用",
                     BMI270_ADDR);
        }
    }

    ESP_LOGI(TAG, "传感器初始化结果：触摸=%s IMU=%s",
             touch_ok_ ? "OK" : "无", imu_ok_ ? "OK" : "无");
    return touch_ok_ || imu_ok_;
}

bool StackChanSensor::ReadAccel(float* ax, float* ay, float* az) {
    // ★ 从 BMI270 读真实加速度（官方 hal_imu.cpp 同款做法）
    if (bmi270_ == nullptr || !imu_ok_) {
        *ax = *ay = *az = 0.0f;
        return false;
    }
    if (!bmi270_->update()) {
        return false;
    }
    bmi270_->getAccelerometer(*ax, *ay, *az);
    return true;
}

void StackChanSensor::Poll() {
    // ── ① 触摸扫描 ──
    if (touch_ok_ && si12t_ != nullptr) {
        uint8_t raw = 0;
        if (si12t_read_touch_result((si12t_handle_t)si12t_, &raw) == ESP_OK
            && raw != 0) {
            uint8_t parsed[8] = {0};
            si12t_parse_touch_result_to(raw, parsed);
            // 官方布局：parsed[0] 为有效通道位图
            uint8_t area = parsed[0] ? parsed[0] : raw;
            if (area != touch_hold_) {
                // 新触摸（去抖：按住期间不重复触发）
                touch_hold_ = area;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    last_touch_ = area;
                }
                // 头顶 / 左右脸 都算"摸头"
                if (area & (kTouchHead | kTouchLeft | kTouchRight)) {
                    pet_event_.store(true);
                    ESP_LOGI(TAG, "摸头 detected (area=0x%02X)", area);
                }
            }
        } else if (raw == 0) {
            touch_hold_ = 0;   // 松手
        }
    }

    // ── ② 摇晃检测 ──
    float ax = 0, ay = 0, az = 0;
    if (imu_ok_ && ReadAccel(&ax, &ay, &az)) {
        float diff = fabsf(ax - prev_ax_) + fabsf(ay - prev_ay_) +
                     fabsf(az - prev_az_);
        prev_ax_ = ax; prev_ay_ = ay; prev_az_ = az;
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (diff > shake_threshold_) {
            if (now - last_shake_ms_ > 100) {        // 去抖 100ms
                if (now - last_shake_ms_ < 1000) {   // 1 秒窗口
                    shake_count_++;
                } else {
                    shake_count_ = 1;
                }
                last_shake_ms_ = now;
                if (shake_count_ >= 3) {
                    shake_count_ = 0;
                    shake_event_.store(true);
                    ESP_LOGI(TAG, "摇晃 detected");
                }
            }
        }
    }
}

bool StackChanSensor::TakePetEvent() {
    return pet_event_.exchange(false);
}

bool StackChanSensor::TakeShakeEvent() {
    return shake_event_.exchange(false);
}

std::string StackChanSensor::LastTouchName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (last_touch_) {
        case kTouchHead:  return "头顶";
        case kTouchChin:  return "下巴";
        case kTouchLeft:  return "左脸";
        case kTouchRight: return "右脸";
        default:          return "无";
    }
}

void StackChanSensor::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.sensor.get_status",
        "查看 StackChan 传感器状态：触摸（SI12T）与摇晃（BMI270）"
        "是否可用，以及最后一次触摸的区域。",
        PropertyList(),
        [this](const PropertyList& p) -> ReturnValue {
            (void)p;
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "触摸(SI12T): %s；摇晃(BMI270): %s；最后触摸: %s",
                     touch_ok_ ? "可用" : "不可用",
                     imu_ok_ ? "可用" : "不可用",
                     LastTouchName().c_str());
            return std::string(buf);
        });
}
