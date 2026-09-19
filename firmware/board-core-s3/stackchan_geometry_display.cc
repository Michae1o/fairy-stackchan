// StackChan (CoreS3) 的「官方几何脸」皮肤 —— 实现
//
// ★ 设计（读 small 源码后确定）：
//   LcdDisplay 里已有 emoji_box_（GIF 表情容器）
//   本类再加 geometry_root_（几何脸容器，浮在最上层）
//   ⇒ 两者显隐互斥 ⇒ 切换 = 换显隐，不销毁重建
//
// ★ 素材/代码来源：official-stackchan（M5Stack 官方，MIT）
//   已搬到 ../stackchan_avatar/

#include "stackchan_geometry_display.h"

#include <esp_log.h>

#include <cstring>

#include "stackchan_avatar/avatar.h"
#include "stackchan_avatar/decorators/decorators.h"  // ★ 爱心/晕眩装饰器
#include "stackchan_avatar/decorators/floating_hearts.h"  // ★ 漂浮爱心
#include <memory>

#define TAG "StackChanGeometry"

using namespace stackchan::avatar;

// ── 情绪名归一化 ────────────────────────────────────────────
// 小智下发的是它自己的词汇（happy/laughing/crying/doubtful...），
// 官方几何脸只认 6 个（neutral/happy/angry/sad/doubt/sleepy）。
const char* StackChanGeometryDisplay::NormalizeEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return "neutral";
    }
    if (strcmp(emotion, "neutral")   == 0) return "neutral";
    if (strcmp(emotion, "happy")     == 0) return "happy";
    if (strcmp(emotion, "laughing")  == 0) return "happy";    // 同义
    if (strcmp(emotion, "angry")     == 0) return "angry";
    if (strcmp(emotion, "sad")       == 0) return "sad";
    if (strcmp(emotion, "crying")    == 0) return "sad";      // 同义
    if (strcmp(emotion, "sleepy")    == 0) return "sleepy";
    if (strcmp(emotion, "doubtful")  == 0) return "doubt";    // 同义
    if (strcmp(emotion, "doubt")     == 0) return "doubt";
    ESP_LOGW(TAG, "未知情绪 '%s' → neutral", emotion);
    return "neutral";
}

static Emotion ToEmotionEnum(const char* norm) {
    if (strcmp(norm, "happy")  == 0) return Emotion::Happy;
    if (strcmp(norm, "angry")  == 0) return Emotion::Angry;
    if (strcmp(norm, "sad")    == 0) return Emotion::Sad;
    if (strcmp(norm, "doubt")  == 0) return Emotion::Doubt;
    if (strcmp(norm, "sleepy") == 0) return Emotion::Sleepy;
    return Emotion::Neutral;
}

// ── 构造 / 析构 ─────────────────────────────────────────────
StackChanGeometryDisplay::StackChanGeometryDisplay(
    esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, int offset_x, int offset_y, bool mirror_x,
    bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy) {
    ESP_LOGI(TAG, "constructed (%dx%d)", width, height);
}

StackChanGeometryDisplay::~StackChanGeometryDisplay() {
    DestroyGeometryLayer();
}

// ── 建几何脸层（黑底 + 眼/嘴/眉）────────────────────────────
void StackChanGeometryDisplay::CreateGeometryLayer() {
    if (geometry_built_) {
        return;
    }

    // 挂在 LVGL 顶层（screen），盖住 emoji_box_
    lv_obj_t* scr = lv_screen_active();
    if (scr == nullptr) {
        ESP_LOGE(TAG, "lv_screen_active() == nullptr");
        return;
    }

    // 根容器：整屏、黑底、不可滚动、默认隐藏
    geometry_root_ = lv_obj_create(scr);
    lv_obj_set_size(geometry_root_, width_, height_);
    lv_obj_center(geometry_root_);
    lv_obj_set_style_bg_color(geometry_root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(geometry_root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(geometry_root_, 0, 0);
    lv_obj_set_style_radius(geometry_root_, 0, 0);
    lv_obj_set_style_pad_all(geometry_root_, 0, 0);
    lv_obj_remove_flag(geometry_root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(geometry_root_, LV_OBJ_FLAG_HIDDEN);   // 默认不显示
    // 不拦触摸（保持小智原本的触屏交互）
    lv_obj_remove_flag(geometry_root_, LV_OBJ_FLAG_CLICKABLE);

    // 官方几何脸：黑底 + 白线（primaryColor 白 / secondaryColor 黑）
    avatar_ = std::make_unique<DefaultAvatar>();
    avatar_->primaryColor   = lv_color_white();
    avatar_->secondaryColor = lv_color_black();
    avatar_->init(geometry_root_, &lv_font_montserrat_14);
    avatar_->setEmotion(Emotion::Neutral);

    geometry_built_ = true;
    ESP_LOGI(TAG, "geometry layer created (%dx%d, black bg)", width_,
             height_);
}

void StackChanGeometryDisplay::DestroyGeometryLayer() {
    if (!geometry_built_) {
        return;
    }
    avatar_.reset();          // 先删 avatar（它的容器挂在 root 下）
    if (geometry_root_ != nullptr) {
        lv_obj_del(geometry_root_);
        geometry_root_ = nullptr;
    }
    geometry_built_ = false;
    ESP_LOGI(TAG, "geometry layer destroyed");
}

// ── 皮肤切换 ────────────────────────────────────────────────
void StackChanGeometryDisplay::SetGeometryVisible(bool visible) {
    DisplayLockGuard lock(this);

    if (visible && !geometry_built_) {
        CreateGeometryLayer();
    }
    if (!geometry_built_) {
        return;   // 建失败
    }

    geometry_visible_ = visible;
    if (visible) {
        lv_obj_clear_flag(geometry_root_, LV_OBJ_FLAG_HIDDEN);
        // 藏掉 GIF 表情，避免两套同时显示（用户要求：不是叠加，是二选一）
        if (emoji_box_ != nullptr) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        // 停掉 GIF 解码，释放那 ~384KB（用户问过内存）
        if (gif_controller_) {
            gif_controller_->Stop();
        }
        // 黑底 ⇒ 把字幕/聊天文字也藏掉，保持"官方味"
        if (chat_message_label_ != nullptr) {
            lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(geometry_root_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_ != nullptr) {
            lv_obj_clear_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        if (gif_controller_) {
            gif_controller_->Start();
        }
        if (chat_message_label_ != nullptr) {
            lv_obj_clear_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ESP_LOGI(TAG, "geometry %s", visible ? "ON" : "OFF");
}

// ── 说话嘴动画（用户报「说话的时候嘴怎么不会动，只有一条线」）──
//
// ★ 为什么要这样写（读官方源码得出）：
//   DefaultMouth::setWeight(0~100)：
//     weight=0   → 90x6 的细线（闭嘴）
//     weight=100 → 60x50 的圆（大张嘴）
//   本类原先只调 setEmotion，从没调 setWeight ⇒ 嘴恒为 0 ⇒ 永远一条线。
//
//   官方 SpeakingModifier（official-stackchan/firmware/main/stackchan/
//   modifiers/speaking.h）的做法：每 180ms 随机开合一次
//     开 = random(40..80)，闭 = random(0..20)
//   ⇒ 不读音频，纯随机开合，视觉上就像在说话。
//   本实现照抄同一套参数，保证观感与官方一致。
void StackChanGeometryDisplay::SetSpeaking(bool speaking) {
    if (speaking_ == speaking) {
        return;
    }
    speaking_ = speaking;
    DisplayLockGuard lock(this);
    if (!speaking_ && avatar_) {
        avatar_->mouth().setWeight(0);   // 说完闭嘴（回到细线）
    }
    ESP_LOGI(TAG, "speaking %s", speaking ? "ON" : "OFF");
}

void StackChanGeometryDisplay::TickMouth() {
    if (!speaking_ || !geometry_visible_ || avatar_ == nullptr) {
        return;
    }
    // 板卡定时器 20ms 一跳 ⇒ 每 9 跳 = 180ms 调一次（与官方 interval 一致）
    if (++mouth_tick_cnt_ % 9 != 0) {
        return;
    }
    mouth_open_ = !mouth_open_;
    int w;
    if (mouth_open_) {
        w = 40 + (int)(esp_random() % 41);   // 40..80
    } else {
        w = (int)(esp_random() % 21);        // 0..20
    }
    DisplayLockGuard lock(this);
    avatar_->mouth().setWeight(w);
}

// ── 装饰器：摸头冒爱心 / 甩晕（用户要求）────────────────────
//
// ★ 为什么之前"没做"：
//   官方装饰器（HeartDecorator / DizzyDecorator / Shy / Sweat / Angry）
//   的代码早已移植进 stackchan_avatar/decorators/，
//   但【从没有任何地方调用 addDecorator()】⇒ 永远不显示。
//   这是"漏接线"，不是"做不到"。
//
// ★ 官方签名（读 decorators.h）：
//     HeartDecorator(lv_obj_t* parent, uint32_t destroyAfterMs = 0,
//                    uint32_t animationIntervalMs = 500);
//     avatar.addDecorator(std::unique_ptr<Decorator>(...))
//   destroyAfterMs = 生命周期（到点自动销毁，避免堆积）
void StackChanGeometryDisplay::ShowHeart() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 用户反馈：「爱心漂浮，但效果不佳，不如做回原来的爱心样式」
    //   ⇒ 回退到【官方 HeartDecorator】（原地心跳跳动）
    //   参数对齐官方 heart.cpp：动画 500ms 一跳（150°/200° 来回）
    //   存活 1.5 秒后自动销毁（靠 AvatarTick → cleanup）
    avatar_->addDecorator(
        std::make_unique<HeartDecorator>(geometry_root_, 1500, 500));
    ESP_LOGI(TAG, "冒爱心");
}

void StackChanGeometryDisplay::ShowDizzy() {
    DisplayLockGuard lock(this);
    if (avatar_ == nullptr || !geometry_visible_) {
        return;
    }
    // ★ 参数对齐官方 dizzy.cpp（动画间隔走官方默认，转圈才明显）
    //   存活 2 秒后自动销毁
    avatar_->addDecorator(
        std::make_unique<DizzyDecorator>(geometry_root_, 2000, 300));
    ESP_LOGI(TAG, "转圈晕眩");
}

// ★★★ 驱动 avatar 每帧更新（装饰器动画 + 自动销毁）
//
//   官方依据：stackchan.h L136 `_avatar->update();`
//   avatar.h L23-37 的 update() 做三件事：
//     ① key_elements 各自 _update()（眨眼/呼吸等）
//     ② _decorator_pool 各自 _update()（爱心心跳旋转 / 晕眩转圈）
//     ③ _decorator_pool.cleanup()  ← 到期的装饰器在这里被销毁
//
//   ⛔ 不调它 = 用户实测的三个现象：
//     · 「爱心不是漂浮的，甩晕也不是动态的」→ 动画从不推进
//     · 「一旦冒出爱心和甩晕，表情就不会变了，被这两个覆盖」
//       → requestDestroy() 从不触发 ⇒ 装饰器永不过期 ⇒ 一直挡着
//
//   ★ 调用频度：由板卡 20ms 轮询定时器调（50Hz，足够流畅）
//     只在几何脸显示时才需要（Fairy 模式走 GIF，没 avatar）
void StackChanGeometryDisplay::AvatarTick() {
    if (!geometry_visible_ || avatar_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    avatar_->update();
}

// ── Display 接口覆写 ────────────────────────────────────────
void StackChanGeometryDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI called twice, skip");
        return;
    }
    // 先让父类把基础 UI 建好（emoji_box_ 等）
    SpiLcdDisplay::SetupUI();

    // ★ 读 NVS 决定开机用哪套皮肤（在 UI 建好之后）
    //   这里用 weak 回调避免显示类依赖板卡：板卡通过 SetBootSkinHook 注入
    if (boot_skin_hook_) {
        boot_skin_hook_();
    }

    ESP_LOGI(TAG, "SetupUI done");
}

void StackChanGeometryDisplay::SetBootSkinHook(std::function<void()> hook) {
    boot_skin_hook_ = std::move(hook);
}

void StackChanGeometryDisplay::SetEmotion(const char* emotion) {
    const char* norm = NormalizeEmotion(emotion);

    if (geometry_visible_) {
        // 几何脸模式：用官方几何脸自己的表情
        DisplayLockGuard lock(this);
        if (avatar_) {
            avatar_->setEmotion(ToEmotionEnum(norm));
        }
        // 睡觉时顺带给个台词（官方也是这么做的）
        if (strcmp(norm, "sleepy") == 0) {
            avatar_->setSpeech("Zzz…");
        } else {
            avatar_->clearSpeech();
        }
        return;
    }

    // Fairy 模式：交回父类（走 GIF）
    SpiLcdDisplay::SetEmotion(emotion);
}

void StackChanGeometryDisplay::SetStatus(const char* status) {
    if (geometry_visible_) {
        // 几何脸模式下不显示状态栏文字（用户说"状态栏不写也没关系"）
        return;
    }
    SpiLcdDisplay::SetStatus(status);
}

void StackChanGeometryDisplay::SetChatMessage(const char* role,
                                              const char* content) {
    if (geometry_visible_) {
        // 几何脸模式下不显示字幕（保持官方那套纯脸效果）
        return;
    }
    SpiLcdDisplay::SetChatMessage(role, content);
}
