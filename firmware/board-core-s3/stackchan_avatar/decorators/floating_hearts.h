/*
 * FloatingHearts —— 多个爱心向上漂浮的装饰器
 *
 * 为什么新写（不用官方 HeartDecorator）：
 *   官方 HeartDecorator::_update() 只做【旋转跳动】（150°/200°），
 *   没有位移 ⇒ 看起来是"原地心跳"，不是"漂浮"。
 *   用户要的是「屏幕上漂浮几个爱心」⇒ 需要自己控制位置 + 透明度。
 *
 * 设计：
 *   · N 个爱心（默认 5），各自独立：
 *       - 起始 x 随机散布、起始 y 在下方
 *       - 上升速度不同（越快的越早飘出）
 *       - 横向轻微摆动（正弦）
 *       - 大小不同（缩放靠 setRotation 不行，用 setSize 或直接不同图）
 *       - 越往上透明度越低（淡出）
 *   · 每个爱心到顶或生命周期结束就隐藏
 *   · 整个装饰器 destroyAfterMs 后自动销毁
 *     （靠 avatar->update() → _decorator_pool.cleanup()，已接好）
 *
 * 复用官方图片资源 decorator_heart（decorators/assets/decorator_heart.c）
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "decorators.h"

#include <lvgl.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// ★★★ 关键：LV_IMAGE_DECLARE 必须放在 namespace 【外面】
//   原因：资源文件 decorator_heart.c 是 C 文件 ⇒ 符号是全局的
//   若把声明放在 namespace stackchan::avatar 内 ⇒ 链接名会变成
//   stackchan::avatar::decorator_heart ⇒ 与 .c 里的全局符号不匹配
//   ⇒ undefined reference（本项目实际踩到）
//   （对照：官方 heart.cpp 也是在 namespace 外声明的）
LV_IMAGE_DECLARE(decorator_heart);

namespace stackchan::avatar {

class FloatingHearts : public Decorator {
public:
    /**
     * @param parent           父容器（一般是几何脸的根容器）
     * @param destroyAfterMs   生命周期（0 = 永久，直到手动移除）
     * @param count            爱心个数（默认 5）
     */
    FloatingHearts(lv_obj_t* parent, uint32_t destroyAfterMs = 2200,
                   int count = 5)
        : _parent(parent), _count(count > 0 ? count : 5) {
        _hearts.reserve(_count);
        _phases.reserve(_count);

        for (int i = 0; i < _count; i++) {
            auto h = std::make_unique<uitk::lvgl_cpp::Image>(parent);
            h->setSrc(&decorator_heart);
            h->setAlign(LV_ALIGN_CENTER);
            h->setImageRecolorOpa(LV_OPA_COVER);
            h->setImageRecolor(lv_color_hex(0xE13232));   // 与官方同色
            // 错开出现：初始 y 不同，避免"一次性齐刷刷"
            float phase = (float)i / (float)_count;        // 0..1
            _phases.push_back(HeartPhase{
                /* x0     */ randomOffset(-60, 60),
                /* y0     */ 70.0f + phase * 60.0f,        // 从下方开始
                /* vx     */ 0.0f,
                /* vy     */ 0.5f + phase * 0.5f,          // 上升速度(px/帧)
                /* swing  */ 12.0f + phase * 14.0f,        // 横向摆动幅度
                /* freq   */ 0.05f + phase * 0.03f,        // 摆动频率
                /* t      */ -phase * 30.0f,               // 时间偏移(错开)
            });
            _hearts.push_back(std::move(h));
        }
        _start_ms = lv_tick_get();
        if (destroyAfterMs > 0) {
            _destroy_at = _start_ms + destroyAfterMs;
            _has_lifetime = true;
        }
        _apply(0);
    }

    ~FloatingHearts() override = default;

    void _update() override {
        uint32_t now = lv_tick_get();

        // 到期销毁
        if (_has_lifetime && now >= _destroy_at) {
            for (auto& h : _hearts) {
                if (h) h->setHidden(true);
            }
            requestDestroy();
            return;
        }

        _apply((float)(now - _start_ms));
    }

private:
    struct HeartPhase {
        float x0;      // 起始横向位置
        float y0;      // 起始纵向位置（正数=下方）
        float vx;      // 横向速度（未用，摆动代替）
        float vy;      // 上升速度
        float swing;   // 摆动幅度
        float freq;    // 摆动频率
        float t;       // 时间偏移
    };

    static float randomOffset(int lo, int hi) {
        // 用 lv_rand（LVGL 自带，无需额外 include）
        int span = (hi - lo) + 1;
        return (float)(lo + (int)(lv_rand(0, span - 1)));
    }

    // 按已流逝毫秒数把每个爱心摆到位置
    void _apply(float elapsed_ms) {
        const float travel_up = 150.0f;   // 总共上升多少像素（到顶）

        for (int i = 0; i < _count; i++) {
            if (!_hearts[i]) continue;

            auto& ph = _phases[i];
            float sec = elapsed_ms * 0.001f;

            // ① 上升：y 从 ph.y0（下方）递减（LVGL 坐标 y 向下为正）
            float rise = ph.vy * riseSpeed() * (elapsed_ms * 0.001f);
            float y = ph.y0 - rise;                 // ★ 真的用来定位

            // ② 横向摆动（正弦，幅度 swing）
            float x = ph.x0 + ph.swing * sinf(sec * 2.0f * 3.14159265f
                                              * ph.freq * 4.0f);

            // ③ 超出顶部 ⇒ 本颗结束（隐藏）
            if (y < -190.0f) {
                _hearts[i]->setHidden(true);
                continue;
            }
            _hearts[i]->setHidden(false);

            // ④ ★ 关键：真的设置位置（原来漏了这句 ⇒ 爱心不动）
            _hearts[i]->setPos((int)x, (int)y);

            // ⑤ 透明度：越往上越淡（淡出效果）
            float progress = rise / travel_up;
            int opa = 255 - (int)(progress * 200.0f);
            if (opa < 25) opa = 25;
            if (opa > 255) opa = 255;
            _hearts[i]->setImageRecolorOpa((lv_opa_t)opa);

            // ⑥ 轻微旋转（每颗角度不同，更自然）
            _hearts[i]->setTransformPivot(
                _hearts[i]->getWidth() / 2, _hearts[i]->getHeight() / 2);
            _hearts[i]->setRotation(((int)ph.x0 * 3) % 40 - 20);
        }
    }

    // 上升速度系数（把 vy 换算成"每秒多少像素"）
    static float riseSpeed() { return 18.0f; }

    lv_obj_t* _parent = nullptr;
    int _count = 5;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _hearts;
    std::vector<HeartPhase> _phases;

    uint32_t _start_ms = 0;
    uint32_t _destroy_at = 0;
    bool _has_lifetime = false;
};

}  // namespace stackchan::avatar
