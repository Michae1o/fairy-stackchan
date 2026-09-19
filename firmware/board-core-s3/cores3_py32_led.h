#ifndef _CORES3_PY32_LED_H_
#define _CORES3_PY32_LED_H_

// StackChan (CoreS3) 的 RGB 灯效
//
// 硬件事实（读官方 stackchan 固件源码确认）：
//   · 12 颗 RGB，挂在 PY32 IO 扩展芯片的 13 号引脚（不是直连 ESP32 GPIO）
//   · 左灯 = index 0~5，右灯 = index 6~11
//   · IO 扩展走 I2C，地址 0x6F
//   · 舵机电源（VM EN）也在同一颗芯片的 0 号引脚
//
// 本类实现小智框架的 Led 接口（OnStateChanged），
// 按设备状态切换灯色，让 Fairy 有"情绪灯光"。

#include "led/led.h"   // 路径相对 main/（与其它板卡一致）
#include <atomic>
#include <mutex>
#include <string>
#include <esp_timer.h>
#include "PY32IOExpander_Class.hpp"

class CoreS3Py32Led : public Led {
public:
    // i2c_bus: 复用板卡已有的 I2C 总线（与 PMIC/AW9523/触摸同一根）
    CoreS3Py32Led(i2c_master_bus_handle_t i2c_bus);
    virtual ~CoreS3Py32Led();

    void OnStateChanged() override;

    // 供板卡初始化时调用：配置 IO 扩展的 RGB 引脚、设置灯数
    bool Init();

    // 注册 MCP 工具（让 AI 能说「把灯关了」「换成蓝色」）
    void RegisterMcpTools();

    // ★ 从 NVS 载入各状态配色（开机时调一次）。
    //   没存过就用编译期默认值。让用户在控制台改的颜色能【重启后仍生效】。
    void LoadColorsFromNvs();

private:
    std::mutex mutex_;
    m5::PY32IOExpander_Class* io_expander_ = nullptr;
    bool inited_ = false;

    // ★ AI/用户手动接管：true 时 OnStateChanged 不再覆盖灯色，
    //   否则对话状态一变（idle/listening/speaking）就冲掉手动设的颜色。
    std::atomic<bool> manual_override_{false};

    // ── ★ 各状态配色（运行时可改，由 NVS 持久化）──────────────
    //   原来是 static constexpr（编译期常量），改色必须重编重刷。
    //   现在改成实例成员 + NVS：控制台改完→存 NVS→重启仍生效。
    //   状态名（字符串）与 NVS 键一一对应，便于控制台/工具按名寻址。
    struct Rgb { uint8_t r, g, b; };
    Rgb col_idle_      = { 24,  8, 40};   // 待机：暗紫
    Rgb col_connecting_= {  0, 32, 96};   // 连接中：蓝
    Rgb col_listening_ = {  0, 96, 128};  // 聆听：青蓝
    Rgb col_speaking_  = { 96, 24, 128};  // 说话：紫粉
    Rgb col_notifying_ = {128, 64,  0};   // 通知：橙
    Rgb col_error_     = {128,  0,  0};   // 错误：红
    Rgb col_upgrading_ = { 64, 64,  0};   // 升级：黄

    // 按状态名取颜色指针（找不到返回 nullptr）
    // 支持的名字：idle/connecting/listening/speaking/notifying/error/upgrading
    // ★ 各状态的【亮法】：true = 呼吸，false = 常亮
    //   用户需求：「灯光那里我看到每个底下有常亮和呼吸，
    //   但是不可选，你应该开放成可选的」
    //   默认值 = 改之前的硬编码行为（所以不改就不变）
    bool mode_idle_       = true;   // 原：StartBreath
    bool mode_connecting_ = true;   // 原：StartBreath
    bool mode_listening_  = false;  // 原：SetAll
    bool mode_speaking_   = false;  // 原：SetAll
    bool mode_notifying_  = false;  // 原：SetAll
    bool mode_error_      = false;  // 原：SetAll
    bool mode_upgrading_  = true;   // 原：StartBreath

    // 按状态名取模式指针（找不到返回 nullptr）
    bool* ModeByName(const std::string& name);

    // ★ 从 NVS 载入各状态亮法（开机时调一次）
    void LoadModesFromNvs();
    void SaveModeToNvs(const char* name, bool breath);
    bool LoadModeFromNvs(const char* name, bool* out);

    Rgb* ColorByName(const std::string& name);
    // 状态名列表（供 MCP 工具枚举）
    static const char* const kStateNames[7];

    // NVS 读写（键形如 "col_idle"）
    void SaveColorToNvs(const char* name, const Rgb& c);
    bool LoadColorFromNvs(const char* name, Rgb* out);

    // 当前颜色（用于避免重复刷新）
    uint8_t cur_r_ = 0, cur_g_ = 0, cur_b_ = 0;
    int cur_brightness_ = 0;

    // ★ 呼吸参数（用户反馈「呼吸看着巨卡，完全不丝滑」后调整）
    //   原值：步长 5%/步（41 步）、周期 100ms ⇒ 台阶肉眼可数 + 一轮 4 秒
    //   现值：步长 2%/步（101 步）、周期 33ms ⇒ 约 3.3 秒一轮，无台阶
    //   调法：想更快就调小 kBreathPeriodMs；想更细就调小 kBreathStep。
    static constexpr int kBreathPeak = 100;      // 亮度峰值（%）
    static constexpr int kBreathStep = 2;        // 每步亮度增量（%）
    static constexpr int kBreathPeriodMs = 33;   // 每步间隔（ms）

    esp_timer_handle_t breath_timer_ = nullptr;
    bool breathing_ = false;
    int breath_step_ = 0;
    uint8_t breath_r_ = 0, breath_g_ = 0, breath_b_ = 0;

    // 基础操作
    void SetAll(uint8_t r, uint8_t g, uint8_t b);
    void SetBrightnessColor(uint8_t r, uint8_t g, uint8_t b, int bright);
    void StartBreath(uint8_t r, uint8_t g, uint8_t b);
    void StopBreath();
    void OnBreathTick();
    static void BreathTimerCb(void* arg);

    // ★ 按亮法（呼吸/常亮）设置某状态颜色
    void ApplyOne(const Rgb& c, bool breath);

    // 状态 → 颜色映射
    void ApplyStateColor();
};

#endif  // _CORES3_PY32_LED_H_
