#ifndef _CORES3_ESPNOW_REMOTE_H_
#define _CORES3_ESPNOW_REMOTE_H_

// StackChan (CoreS3) 接收【官方 K151-R 遥控器】的 ESP-NOW 包
//
// ── 遥控器是什么（实读官方 official-stackchan/remote/ 源码）────────
//   · 硬件 = M5StickC-Plus + Hat Mini JoyC（摇杆帽），**独立的一台小电脑**
//   · 协议 = **ESP-NOW 广播** 到 ff:ff:ff:ff:ff:ff
//     ⇒ ★ 不需要配对、**不需要路由器**（点对点 2.4G）
//   · 8 字节包（joystick_handle.c L174-191）：
//       [0]   = id（0 = 广播）
//       [1..2]= yaw   int16 小端，1280 → -1280（★ 摇杆最右 = -1280）
//       [3..4]= pitch int16 小端，0 → 900
//       [5..6]= speed int16 小端，固定 600
//       [7]   = BtnB 状态 0/1   ← ⛔ 早期文档写成「laser」，是错的
//   · 单位都是 **0.1°** —— 与飞特舵机量程完全一致（yaw ±1280 / pitch 0~900）
//     ⇒ 中间不用换算，除以 10 就是「度」
//   · 发送节奏：30ms 一轮；摇杆变化 <5 不发包；BtnB 变化立即发一包
//
// ── ⛔⛔ 本项目定的头号约束：设备侧【一个字节都不动 WiFi 射频】──────
//   peer.channel = 0  ⇒ ESP-NOW 自动跟随 STA 当前信道
//   （esp_now.h 原文：If the value is 0, use the current channel which
//     station or softap is on）
//   ⇒ 不调 esp_wifi_set_channel ⇒ **不会把自己连服务器的那条链路搞断**
//   ⛔ 官方遥控器源码里的 wifi_espnow_reinit() 会重启 WiFi 再设信道 ——
//      那是【纯 ESP-NOW 设备（遥控器自己）】的做法，设备侧绝不能照抄。
//   代价（要告诉用户的）：**遥控器 SETUP 页里的 Channel 得调到与路由器一致**
//   （设备跟随路由器；遥控器屏幕上能改，1~14）。
//
// ── 分工 ────────────────────────────────────────────────────────
//   本体负责：等 WiFi 就绪 → esp_now_init → 收包 → 解析成角度
//   板卡负责：把角度送舵机、把 BtnB 接到对话开关（见 m5stack_core_s3.cc）

#include <atomic>
#include <cstdint>

class CoreS3EspNowRemote {
public:
    // 解析好的一包指令（角度已按设备量程夹紧）
    struct Command {
        float    yaw_deg;     // -128.0 ~ +128.0
        float    pitch_deg;   //    3.0 ~  87.0
        uint16_t speed;       // 0 ~ 1000
    };

    CoreS3EspNowRemote() = default;

    // 初始化 ESP-NOW（同步）。
    // ⛔⛔ 只允许在【WiFi 模块已就绪】之后调用 —— 板卡把它放在
    //   StartNetwork() 里（基类先走 esp_wifi_init 再回来）。
    //   绝不能在板卡构造函数里调：那时 esp_wifi_init() 还没执行，
    //   esp_now_init() 不会返回错误而是【直接崩】。
    //   （2026-10-06 实测：放构造函数里 ⇒ 开机白屏 + 重启循环 26 次。）
    bool Start();

    bool IsReady() const { return ready_.load(); }

    // 设备射频当前所在信道（遥控器要调到这个值）。0 = 还没连上
    uint8_t Channel() const { return channel_.load(); }

    // 最近一包的时间戳（ms，esp_timer 口径）。0 = 从没收到过。
    // 用途：待机小动作让路（用户在玩遥控器时别自己扭头）
    int64_t LastPacketMs() const { return last_packet_ms_.load(); }

    // 收到过多少包（日志/排查用）
    uint32_t PacketCount() const { return packet_count_.load(); }

    // 取走一包【还没消费过】的指令；没有新包返回 false
    bool TakeCommand(Command* out);

    // BtnB 的「按下」上升沿（取走即清）—— 板卡拿去切对话状态
    bool TakeButtonPress();

    // ⛔ 只给 ESP-NOW 接收回调调用。
    //   回调跑在 WiFi 任务上下文 ⇒ 这里【只做解析和置标志】，
    //   绝不碰 LVGL / UART / 舵机。
    void OnPacket(const uint8_t* data, int len);

private:
    // 按【当前实际信道】重建广播 peer。
    // 两处调用：初始化时、以及之后信道变化时（设备连上路由器 / 路由器换信道）。
    void RefreshPeer();

    // 读射频【现在】在哪个信道：
    //   已连路由器 → 路由器主信道；未连接 → 射频停着的信道（通常 1）
    uint8_t CurrentChannel();

    // 遥控器「找设备」时用广播回一句「我在信道 N」（见 .cc 顶部的探测包说明）
    void ReplyChannel();

    std::atomic<bool>     ready_{false};
    std::atomic<bool>     started_{false};
    std::atomic<uint8_t>  channel_{0};

    // 最新一包的原始值（0.1° 单位）。哨兵 -100000 = 还没有包
    std::atomic<int32_t>  pend_yaw_raw_{-100000};
    std::atomic<int32_t>  pend_pitch_raw_{-100000};
    std::atomic<int32_t>  pend_speed_raw_{600};
    std::atomic<uint32_t> seq_{0};           // 收包序号
    std::atomic<uint32_t> consumed_seq_{0};  // 已消费到的序号

    std::atomic<bool>     btn_pressed_{false};
    uint8_t               last_btn_b_ = 0;

    std::atomic<int64_t>  last_packet_ms_{0};
    std::atomic<uint32_t> packet_count_{0};
};

#endif  // _CORES3_ESPNOW_REMOTE_H_
