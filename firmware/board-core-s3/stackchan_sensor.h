#ifndef _STACKCHAN_SENSOR_H_
#define _STACKCHAN_SENSOR_H_

// StackChan 传感器：头顶触摸（SI12T）+ 摇晃（BMI270）
//
// 来源：官方 stackchan 固件（hal_head_touch.cpp / hal_imu.cpp /
//       motion_detector.h），移植到小智的 core-s3 板卡。
//
// 用户需求：「摸摸头和把它甩晕」
//   · 摸头 → 开心
//   · 甩   → 晕眩

#include <driver/i2c_master.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "drivers/bmi270/bmi270.h"   // ★ BMI270 驱动（0x69）

class StackChanSensor {
public:
    // 触摸通道（官方布局）
    enum TouchArea {
        kTouchNone = 0,
        kTouchHead = 1,     // 头顶
        kTouchChin = 2,     // 下巴
        kTouchLeft = 4,     // 左脸
        kTouchRight = 8,    // 右脸
    };

    StackChanSensor(i2c_master_bus_handle_t i2c_bus);
    ~StackChanSensor();

    // 初始化（I2C 上探测 SI12T 与 BMI270；缺任一都不报错，只是不启用）
    bool Init();

    // 轮询：由定时任务调用（触摸扫描 + 摇晃判定）
    void Poll();

    // 是否有传感器可用（控制台/日志用）
    bool HasTouch() const { return touch_ok_; }
    bool HasImu() const { return imu_ok_; }

    // ── 事件（由 Poll 置位，取走即清）──
    // 摸头（含左右脸）→ true 一次
    bool TakePetEvent();
    // 摇晃 → true 一次
    bool TakeShakeEvent();

    // 最近一次触摸区域（供日志/调试）
    std::string LastTouchName() const;

    // 生成 MCP 工具（让 AI/控制台也能查状态）
    void RegisterMcpTools();

private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    void* si12t_ = nullptr;      // si12t_handle_t（避免在头文件暴露 C 类型）
    bool touch_ok_ = false;
    bool imu_ok_ = false;

    mutable std::mutex mutex_;
    std::atomic<bool> pet_event_{false};
    std::atomic<bool> shake_event_{false};
    uint8_t last_touch_ = 0;

    // ★ BMI270 驱动实例（地址 0x69 —— 官方 hal_imu.cpp L57 同款）
    std::unique_ptr<BMI270> bmi270_;
    // BMI270 原始读（保留：ReadAccel 内部走 bmi270_）
    bool ReadAccel(float* ax, float* ay, float* az);

    // 摇晃判定（移植官方 MotionDetector 的算法）
    float prev_ax_ = 0, prev_ay_ = 0, prev_az_ = 0;
    int shake_count_ = 0;
    uint32_t last_shake_ms_ = 0;
    bool shake_latch_ = false;
    float shake_threshold_ = 4.0f;

    // 触摸去抖：同一区域按住期间只触发一次
    uint8_t touch_hold_ = 0;
};

#endif  // _STACKCHAN_SENSOR_H_
