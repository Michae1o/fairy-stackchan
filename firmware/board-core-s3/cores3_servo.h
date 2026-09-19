#ifndef _CORES3_SERVO_H_
#define _CORES3_SERVO_H_

// StackChan (CoreS3 / K151) 的舵机控制层
//
// 硬件事实（读官方 stackchan 固件源码确认，非猜测）：
//   · 飞特(Feetech) 串口总线舵机，驱动类 SCSCL（不是 SMS_STS）
//   · UART_NUM_1, 1 Mbps, TX=GPIO6, RX=GPIO7
//   · yaw   : ID=1, 零点 460, 范围 ±1280 (±128°)   ← 水平转头
//   · pitch : ID=2, 零点 620, 范围  30~870 (3°~87°) ← 抬头/低头
//   · 角度单位 0.1°；raw = zero + angle*16/5/10
//   · 舵机电源由 PY32 IO 扩展 pin0 (VM EN) 控制，须先使能
//   · pitch 有堵转保护（防烧舵机），本类一并实现
//
// 本类提供：
//   · Init()           初始化 UART + 使能扭矩 + 回中
//   · SetYawAngle()    角度制接口（-128.0° ~ +128.0°）
//   · SetPitchAngle()  角度制接口（3.0° ~ 87.0°）
//   · MoveTo()         同时设 yaw/pitch
//   · GetState()       读回当前角度
//   · 注册 MCP 工具，让 AI 能控制转头（免编译调参用）

#include <atomic>
#include <mutex>
#include <string>

class SCSCL;

class CoreS3Servo {
public:
    CoreS3Servo();
    ~CoreS3Servo();

    // 供板卡初始化时调用
    bool Init();

    // ★ 从 NVS 载入用户保存的「开机默认角度」（回中位）。
    //   没存过就用内置的 yaw=0 / pitch=45。
    void LoadHomeFromNvs();

    // ★ 保存「开机默认角度」到 NVS（控制台改完调它）
    bool SaveHomeToNvs(float yaw_deg, float pitch_deg);
    float GetHomeYaw() const { return home_yaw_; }
    float GetHomePitch() const { return home_pitch_; }

    // 角度制接口（单位：度，浮点）
    //   yaw   : -128.0 ~ 128.0（0 = 正前）
    //   pitch :    3.0 ~  87.0（约 45 = 水平）
    bool SetYawAngle(float deg, uint16_t speed = 600);
    bool SetPitchAngle(float deg, uint16_t speed = 600);
    bool MoveTo(float yaw_deg, float pitch_deg, uint16_t speed = 600);

    // 回中（yaw=0, pitch=45）
    bool Center(uint16_t speed = 400);

    // 读回当前角度（度）。失败返回 NaN 语义值 -999
    float GetYawAngle();
    float GetPitchAngle();

    // 是否初始化成功
    bool IsReady() const { return ready_; }

    // 注册 MCP 工具（让 AI 能说「转头看看」「抬个头」）
    void RegisterMcpTools();

private:
    SCSCL* bus_ = nullptr;
    std::atomic<bool> ready_{false};
    std::mutex mutex_;

    // 零点（raw），与官方默认一致
    int yaw_zero_ = 460;
    int pitch_zero_ = 620;

    // ★ 开机默认角度（回中位）—— 可由控制台改，存 NVS
    //   内置默认：yaw=0（正前）、pitch=45（水平）
    float home_yaw_ = 0.0f;
    float home_pitch_ = 45.0f;

    // 角度限位（度）—— 与官方 angleLimit 一致（0.1° 单位 → 度）
    static constexpr float kYawMin = -128.0f;
    static constexpr float kYawMax = 128.0f;
    static constexpr float kPitchMin = 3.0f;
    static constexpr float kPitchMax = 87.0f;

    // pitch 堵转保护（防烧舵机）—— 参数取自官方 hal_servo.cpp
    bool pitch_stall_enabled_ = true;
    int last_pitch_cmd_raw_ = 0;
    float pitch_limit_deg_ = kPitchMax;   // 检测到堵转后收紧上限
    unsigned long last_stall_check_ms_ = 0;

    static constexpr int kStallFeedbackIntervalMs = 50;
    static constexpr int kStallMinTargetDeltaRaw = 8;
    static constexpr int kStallMaxPositionDeltaRaw = 1;
    static constexpr int kStallCurrentRiseThreshold = 80;
    static constexpr int kStallLoadRiseThreshold = 150;
    static constexpr int kStallCurrentAbsThreshold = 350;
    static constexpr int kStallLoadAbsThreshold = 650;
    static constexpr int kStallConfirmSamples = 2;
    int stall_hit_count_ = 0;

    // 限位与换算
    float ClampYaw(float d) const;
    float ClampPitch(float d) const;
    int AngleToRaw(int zero, float deg) const;
    float RawToAngle(int zero, int raw) const;

    // 堵转保护
    void CheckPitchStall(int target_raw);
};

#endif  // _CORES3_SERVO_H_
