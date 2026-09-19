// StackChan (CoreS3) 的「官方几何脸」皮肤
//
// 背景（用户需求）：
//   「机器里同时装着两套皮肤：① Fairy（GIF 脸）② 官方小表情（几何脸）。
//     平时用 Fairy，哪天不想用了 → 切一下 → 整个界面换成小表情那套。
//     不是叠加，是二选一。」
//
// 本类实现第 ② 套：官方 stack-chan 的几何脸（LVGL 矢量绘制 + 装饰器）。
//
// ★ 与 Fairy 并存的架构（读小智 SpiLcdDisplay 源码后确定）：
//   SpiLcdDisplay 里已有两个容器：
//       emoji_box_       ← GIF 表情（Fairy 用）
//       preview_image_   ← 预览图
//   本类再加第三个：
//       geometry_root_   ← 几何脸（眼/嘴/眉 + 装饰器）
//   ⇒ 三者的显隐互斥 ⇒ 切换 = 换显隐，【不销毁重建、无黑屏】
//
// ★ 素材来源：official-stackchan/firmware/main/stackchan/avatar/
//   （M5Stack 官方，MIT 许可）
//   几何脸主体 = skins/default/（eyes/mouth/speech_bubble）
//   情绪       = elements/emotion.h（6 种）
//
// ★ 依赖：smooth_ui_toolkit（uitk::lvgl_cpp），已拉到 components/

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "display/lcd_display.h"

namespace stackchan::avatar {
class Avatar;
class DefaultAvatar;
}

class StackChanGeometryDisplay : public SpiLcdDisplay {
public:
    StackChanGeometryDisplay(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_handle_t panel,
                             int width, int height,
                             int offset_x, int offset_y,
                             bool mirror_x, bool mirror_y, bool swap_xy);
    virtual ~StackChanGeometryDisplay();

    // ── Display 接口 ────────────────────────────────────────
    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;

    // ★ 开机皮肤钩子：板卡注入，SetupUI 建好 UI 后回调
    //   （避免显示类反向依赖板卡）
    void SetBootSkinHook(std::function<void()> hook);

    // ── 皮肤切换 ────────────────────────────────────────────
    // show_geometry = true  → 显示几何脸，隐藏 GIF
    // show_geometry = false → 显示 GIF（Fairy），隐藏几何脸
    void SetGeometryVisible(bool visible);
    bool IsGeometryVisible() const { return geometry_visible_; }

    // ★ 说话时嘴开合动画（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    //   根因：DefaultMouth::setWeight(0~100) 控制张嘴，本类从没调它 ⇒ 恒为细线
    //   做法：照官方 SpeakingModifier —— 180ms 随机开合（开 40~80 / 闭 0~20）
    void TickMouth();                 // 由板卡 20ms 定时器驱动（内部限频 180ms）
    void SetSpeaking(bool speaking);  // 说话开始/结束
    bool IsSpeaking() const { return speaking_; }

    // ★ 装饰器（用户要求：「摸头冒爱心」「甩晕」）
    //   HeartDecorator / DizzyDecorator 的代码早已在 stackchan_avatar/decorators/
    //   里，但从没被调用过 ⇒ 这就是"爱心/甩晕没做"的真因。
    //   接口：avatar.addDecorator(std::unique_ptr<Decorator>)
    //   ⇒ 这里封装成两个方法，由板卡的反应函数调用。
    void ShowHeart();   // 摸头 → 冒爱心
    void ShowDizzy();   // 摇晃 → 转圈晕眩

    // ★★★ 驱动 avatar 的每帧更新 —— 装饰器动画与自动销毁的唯一入口
    //   官方：stackchan.h L136 `_avatar->update();`（主循环每帧调）
    //   avatar.h L23-37 的 update() 会：
    //     ① 遍历 key_elements → element->_update()（眨眼/呼吸）
    //     ② 遍历 _decorator_pool → decorator->_update()（爱心心跳/晕眩转动）
    //     ③ _decorator_pool.cleanup()（★ 到期的装饰器在此销毁）
    //   ⛔ 不调它的后果（用户实测过）：
    //     · 爱心/甩晕静止不动（不动态）
    //     · 装饰器永不过期 ⇒ 一直盖住表情 ⇒ 表情再也不变
    void AvatarTick();

    // 情绪名（与官方一致）：neutral/happy/angry/sad/doubt/sleepy
    // 另兼容小智下发的：laughing→happy、crying→sad、doubtful→doubt
    static const char* NormalizeEmotion(const char* emotion);

private:
    void CreateGeometryLayer();
    void DestroyGeometryLayer();

    // ★ 官方几何脸（stackchan::avatar::DefaultAvatar）
    std::unique_ptr<stackchan::avatar::DefaultAvatar> avatar_;

    lv_obj_t* geometry_root_ = nullptr;   // 几何脸根容器（浮在 emoji_box 之上）
    bool      geometry_visible_ = false;
    bool      geometry_built_ = false;
    std::function<void()> boot_skin_hook_;   // 开机皮肤回调（板卡注入）
    // ★ 说话嘴动画状态（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    bool     speaking_ = false;
    bool     mouth_open_ = false;
    uint32_t mouth_tick_cnt_ = 0;
};
