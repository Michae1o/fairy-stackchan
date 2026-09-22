#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用代码画出「Fairy 风格的脸」，直接生成设备能吃的 GIF 素材。

    python3 tools/make_face.py                  # 生成到 ./fairy-assets/
    python3 tools/make_face.py --out D:\\x       # 指定输出目录
    python3 tools/make_face.py --tint 0x8a5cf6   # 换个主色（做自己的角色）
    python3 tools/make_face.py --frames 60       # 改帧数（体积会跟着变）

产出（320×240、无限循环、20fps）：
    idle.gif          待机表情（白环缓慢呼吸 + 光晕脉动）
    thinking.gif      思考表情（光晕更大、呼吸更快）
    _emote_aliases.json   情绪别名表（23 种情绪 → idle.gif，thinking → thinking.gif）

原理（六层同心圆 + 光晕 + 双高光，参数见下面的常量）：
    这组半径/颜色是沿 8 个方向做像素剖面、找颜色跳变点实测出来的，
    六层边界在 8 个方向上一致（±3px）—— 说明结构是精确同心圆。
    ★ 改形象就是改下面那几个常量：半径、颜色、光晕、高光、动画幅度。

依赖：Pillow（只在【生成时】用，不进设备）：pip install pillow
"""
import argparse
import json
import math
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFilter
except Exception:
    print("需要 Pillow：pip install pillow")
    sys.exit(2)

# ══════════════════════════════════════════════════════════════════
#  可调参数（改这里就能改成你自己的形象）
# ══════════════════════════════════════════════════════════════════
W, H = 320, 240                 # 屏幕尺寸（固定，别改）
BG = (10, 14, 39)               # 背景色（深蓝黑）
SS = 2                          # 超采样倍数（抗锯齿用；2 = 2 倍后缩，边缘更顺）

# 六层同心圆：(半径, 颜色)，从内到外。半径用的是 320 宽坐标系
LAYERS = [
    (19,  (0x3c, 0x3e, 0x8a)),  # 瞳孔（深蓝）
    (28,  (0x33, 0x7d, 0xcf)),  # 亮蓝环
    (41,  (0x9d, 0xaf, 0xe0)),  # 浅蓝灰环
    (60,  (0xee, 0xf0, 0xf5)),  # 白环（最亮 —— 呼吸时主要靠它闪）
    (83,  (0x30, 0x3a, 0x8d)),  # 深紫蓝环
    (102, (0x39, 0x4c, 0xde)),  # 亮蓝主环（最宽）
]
EDGE_SOFT = 3                   # 层与层之间的过渡像素（模拟柔光/抗锯齿）

HALO_COLOR = (0x3a, 0x4f, 0xd6)  # 光晕色
HALO_OUT = 118                   # 光晕外沿半径（屏幕像素；比外环 102 略大一圈）
HALO_ALPHA = 0.34                # 光晕最强处的不透明度（太大整屏会发糊）

HL1 = dict(dx=34, dy=30, rx=11, ry=7, rot=-0.5, alpha=1.00)   # 右下主高光
HL2 = dict(dx=-26, dy=-30, rx=9, ry=6, rot=-0.5, alpha=0.35)  # 左上小高光

# 动画
FRAMES = 98                     # 帧数（成品是 98 帧 ≈ 4.9 秒）
MS = 50                         # 每帧时长（毫秒）⇒ 20fps

PRESETS = {
    # 待机：外径基本不动，白环缓慢呼吸（实测：外径 106~108，白环附近亮度 117↔240）
    # ★ pulse_hz 必须是【整数】：它表示「一个循环里呼吸几圈」，非整数会让接缝跳一下
    "idle":     dict(scale=1.00, scale_amp=0.008, halo_amp=0.16, pulse_hz=2,
                     wave_amp=0.030, wave_phase=-0.22, white_amp=0.10),
    # 思考：整体略大、光晕更亮、呼吸更快（实测：外径 ≈107~112）
    "thinking": dict(scale=1.04, scale_amp=0.014, halo_amp=0.26, pulse_hz=3,
                     wave_amp=0.022, wave_phase=-0.30, white_amp=0.06),
}

# 23 种情绪都指向 idle.gif（和成品 GIF 的别名表一致）
EMOTIONS = ["angry", "confident", "confused", "cool", "crying", "delicious",
            "embarrassed", "funny", "happy", "idle", "kissy", "laughing",
            "listening", "loving", "neutral", "relaxed", "sad", "shocked",
            "silly", "sleepy", "speaking", "surprised", "winking"]


# 把「坐标系半径」换算成屏幕像素。
#   作者成品的六层边界实测在 18/28/41/59/83/102 屏幕像素 ⇒ 就是 1:1，K = 1.0
#   （★ 想整体放大/缩小，改这个数就行；但外径别超过 115，否则上下会被屏幕切掉）
#   ★ 屏幕是 320×240（不是正方形）⇒ 【不能】先画正方形再压扁
K = 1.0


def mix(c1, c2, t):
    return tuple(int(round(a + (b - a) * t)) for a, b in zip(c1, c2))


def tint_layers(layers, tint):
    """把整体颜色往 tint 方向偏（做「自己的角色」用）。"""
    if not tint:
        return layers
    t = ((tint >> 16) & 255, (tint >> 8) & 255, tint & 255)
    return [(r, mix(c, t, 0.5)) for r, c in layers]


def render_frame(t, preset, layers, halo_color, white_ring_idx):
    """画第 t 帧（t ∈ [0,1) 表示循环进度）。"""
    cw, ch = W * SS, H * SS                      # ★ 画布也是 320×240 的比例，别用正方形
    cx, cy = cw / 2, ch / 2
    img = Image.new("RGB", (cw, ch), BG)
    d = ImageDraw.Draw(img, "RGBA")

    # ① 光晕：从外向内叠，模拟弥散
    scale = preset["scale"] * (1.0 + preset["scale_amp"] * math.sin(2 * math.pi * t))
    halo_alpha = HALO_ALPHA * (1.0 + preset["halo_amp"] * math.sin(2 * math.pi * t - 0.6))
    halo_out = HALO_OUT * K * scale * SS
    main_r = layers[-1][0] * K * scale * SS
    steps = 48
    for s in range(steps, 0, -1):
        k = s / steps
        rr = main_r + (halo_out - main_r) * k
        a = int(255 * max(0.0, min(1.0, halo_alpha * (1 - k) ** 2.2)))
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=halo_color + (a,))

    # ② 六层同心圆（从外向内画）
    #    呼吸波：每层的半径各自小幅脉动，相位按层号错开 ⇒ 看上去是「一圈圈往外荡」
    for i in range(len(layers) - 1, -1, -1):
        r0, col = layers[i]
        outer = layers[i + 1][1] if i + 1 < len(layers) else col
        ph = preset["wave_phase"] * (len(layers) - 1 - i)
        wave = 1.0 + preset["wave_amp"] * math.sin(2 * math.pi * t * preset["pulse_hz"] + ph)
        if i == white_ring_idx:                      # 白环额外提亮（最抓眼的那层）
            g = 0.5 + 0.5 * math.sin(2 * math.pi * t * preset["pulse_hz"] + ph)
            col = mix(col, (255, 255, 255), preset["white_amp"] * g)
        r = r0 * K * scale * SS * wave
        soft = EDGE_SOFT * SS
        for k in range(6, 0, -1):
            kk = k / 6.0
            d.ellipse([cx - r - soft * kk, cy - r - soft * kk,
                       cx + r + soft * kk, cy + r + soft * kk],
                      fill=mix(outer, col, 1 - kk))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)

    # ③ 双高光（按屏幕像素给，和参考实现一致）
    for hl in (HL1, HL2):
        hx = cx + hl["dx"] * SS * scale
        hy = cy + hl["dy"] * SS * scale
        rx, ry = hl["rx"] * SS, hl["ry"] * SS
        a = int(255 * hl["alpha"])
        d.ellipse([hx - rx, hy - ry, hx + rx, hy + ry], fill=(255, 255, 255, a))

    img = img.resize((W, H), Image.LANCZOS)
    return img.convert("P", palette=Image.ADAPTIVE, colors=64)


def build(name, out_dir, layers, tint, frames, ms):
    preset = PRESETS[name]
    white_idx = max(range(len(layers)), key=lambda i: sum(layers[i][1]))
    imgs = [render_frame(i / frames, preset, layers, HALO_COLOR, white_idx)
            for i in range(frames)]
    path = os.path.join(out_dir, name + ".gif")
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=ms,
                 loop=0, optimize=True, disposal=2)
    # ★ 报告实际落盘的帧数/体积（optimize 会合并完全相同的帧，别拿请求帧数当结论）
    with Image.open(path) as chk:
        real_n = getattr(chk, "n_frames", frames)
        real_ms = chk.info.get("duration", ms)
    size = os.path.getsize(path)
    print("   ✅ %-14s %d 帧 ｜ %dx%d ｜ %.2f MB ｜ %d ms/帧 ｜ %s"
          % (name + ".gif", real_n, W, H, size / 1048576.0, real_ms, path))
    if size > 8 * 1048576:
        print("      ⚠️ 超过 8MB 上限 ⇒ 减少 --frames 或改小颜色数")
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="fairy-assets", help="输出目录（默认 ./fairy-assets）")
    ap.add_argument("--frames", type=int, default=FRAMES, help="帧数（默认 %d）" % FRAMES)
    ap.add_argument("--ms", type=int, default=MS, help="每帧毫秒（默认 %d ⇒ 20fps）" % MS)
    ap.add_argument("--tint", default=None, help="主色偏色，如 0x8a5cf6（默认不偏）")
    ap.add_argument("--only", choices=["idle", "thinking"], help="只生成一个")
    args = ap.parse_args()

    tint = int(args.tint, 0) if args.tint else None
    layers = tint_layers(LAYERS, tint)
    os.makedirs(args.out, exist_ok=True)

    print("生成 Fairy 风格的表情素材 → %s" % os.path.abspath(args.out))
    print("  参数：%d 帧 ｜ %d ms/帧（%.1f fps）｜ %d 色 ｜ %s"
          % (args.frames, args.ms, 1000.0 / args.ms, 64,
             ("偏色 0x%06x" % tint) if tint else "原配色"))
    names = [args.only] if args.only else ["idle", "thinking"]
    for n in names:
        build(n, args.out, layers, tint, args.frames, args.ms)

    aliases = {}
    if os.path.exists(os.path.join(args.out, "idle.gif")):
        aliases["idle.gif"] = EMOTIONS
    if os.path.exists(os.path.join(args.out, "thinking.gif")):
        aliases["thinking.gif"] = ["thinking"]
    with open(os.path.join(args.out, "_emote_aliases.json"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(aliases, ensure_ascii=False, indent=2) + "\n")
    print("   ✅ _emote_aliases.json（%d 种情绪 → idle.gif）" % len(EMOTIONS))

    print("""
下一步：
  ① 把 <上游>/fairy-assets/ 指向这个目录（或把文件拷进去）：
       python3 tools/apply_to_upstream.py <上游目录> --gif-dir %s
     该脚本会：写 _emote_aliases.json、把 CoreS3 的表情包名改成 fairy
  ② 编译后自检（看「内嵌 GIF 数量」是否 > 0）：
       python3 tools/verify_artifact.py build/xiaozhi.bin
  ③ 改形象：编辑本文件顶部的 LAYERS（半径/颜色）/ HALO_* / HL1,HL2 / PRESETS
     —— 半径改大改小、颜色换一套、PRESETS 里的幅度决定"呼吸"的强弱。
""" % args.out)


if __name__ == "__main__":
    main()
