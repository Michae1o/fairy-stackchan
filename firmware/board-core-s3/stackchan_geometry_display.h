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

    // ★ 装饰器 —— 官方 modifier 的「视觉反馈」层
    //   官方 5 个装饰器（Heart / Shy / Dizzy / Angry / Sweat）的代码与素材
    //   早已在 stackchan_avatar/decorators/ 并已编进固件，
    //   此前【从没有任何地方调用 addDecorator()】⇒ 永远不显示（漏接线）。
    //   ⇒ 这里逐个封装成方法，由板卡反应函数 / 情绪下发调用。
    //
    //   ⛔⛔ 全部只作用于【几何脸（官方小智皮肤）】——
    //     ① 内部判 `geometry_visible_`（Fairy 皮肤下直接 return，不建对象）
    //     ② parent 传 `geometry_root_`（几何脸容器）⇒ 随容器一起显隐
    //     ⇒ 与 Fairy 皮肤【完全不搭边】，两套皮肤互不干扰。
    //
    //   ★ 官方各自用在哪（读 official-stackchan/firmware/main/stackchan/）：
    //     · 摸头：Heart + Shy 一起冒（head_pet.h L90/L91 同一句里连加两个）
    //     · 摇晃：Dizzy（imu.h）
    //     · Angry / Sweat —— 官方【自己也没接线】（全仓库只有类定义、0 调用点）
    //       ⇒ 本项目接在「服务器下发的情绪」上（见 SetEmotion）
    void ShowHeart();   // 摸头   → 冒爱心
    void ShowShy();     // 摸头   → 脸红（官方就是跟爱心同时冒）
    void ShowDizzy();   // 摇晃   → 转圈晕眩
    void ShowAngry();   // 情绪 angry → 头顶怒符
    void ShowSweat();   // 情绪 sad   → 冒冷汗

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

    // ★ 装饰器 id（-1 = 当前没有）—— 重触发时【先撤旧的再挂新的】
    //   官方 head_pet.h 就是这么做的（removeDecorator 后紧跟 addDecorator）。
    //   ⛔ 不这么做：情绪反复下发会一层层叠上去，满屏都是怒符/汗滴
    int heart_id_ = -1;
    int shy_id_   = -1;
    int dizzy_id_ = -1;
    int angry_id_ = -1;
    int sweat_id_ = -1;

    lv_obj_t* geometry_root_ = nullptr;   // 几何脸根容器（浮在 emoji_box 之上）
    bool      geometry_visible_ = false;
    bool      geometry_built_ = false;
    std::function<void()> boot_skin_hook_;   // 开机皮肤回调（板卡注入）
    // ★ 说话嘴动画状态（用户报：「说话的时候嘴怎么不会动，只有一条线」）
    bool     speaking_ = false;
    bool     mouth_open_ = false;
    uint32_t mouth_tick_cnt_ = 0;
};
