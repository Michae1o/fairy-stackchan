#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把本仓库（fairy-stackchan）的改动应用到一份【干净的上游 xiaozhi-esp32】上。

用法：
    python3 tools/apply_to_upstream.py <上游目录>
    python3 tools/apply_to_upstream.py <上游目录> --dry-run     # 只看会改什么，不写
    python3 tools/apply_to_upstream.py <上游目录> --no-cmake    # 只覆盖文件，不改 CMake
    python3 tools/apply_to_upstream.py <上游目录> --gif-dir <放 GIF 的目录>

做的事 = INSTALL.md 的 1.2 ~ 1.4 节：
    ① 覆盖板卡目录     firmware/board-core-s3/*      → main/boards/m5stack/core-s3/
    ② 覆盖公共设备层   firmware/board-common/*       → main/boards/common/
    ③ 覆盖本地组件     firmware/components/*         → components/
    ④ 覆盖 ota.cc      firmware/ota.cc               → main/ota.cc
    ⑤ 覆盖显示层       firmware/display/*            → main/display/
    ⑥ 唤醒词          往 sdkconfig.defaults.esp32s3 追加 Hi Fairy（上游只带「你好小智」）
    ⑦ 改 main/CMakeLists.txt：子目录源文件 + PRIV_REQUIRES + 表情包名
    ⑧ 改 scripts/build_default_assets.py：加 fairy 表情包分支
    ⑨ 建 fairy-assets/：情绪别名表（★ 本仓库【不附带】GIF 素材，请自备）

★ 幂等：重复执行不会重复插入。
★ 不动 sdkconfig、不编译（编译见 INSTALL.md 1.6）。
★ 锚点找不到就报错退出 —— 绝不静默跳过，避免「以为改好了」。
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

FW = Path(__file__).resolve().parents[1] / "firmware"

# ── 要插入 CMakeLists.txt 的源文件清单（子目录 GLOB 不收，必须显式列）──
SOURCES_BLOCK = '''# ★ StackChan(CoreS3) 的 RGB 需要 M5Stack 的 PY32 IO 扩展驱动。
# 该文件后缀是 .cpp，上面的 GLOB 只收 *.cc/*.c，所以必须显式加入
# （否则链接期报 undefined reference to m5::PY32IOExpander_Class::...）。
# 同理，飞特舵机驱动放在 FTServo/ 子目录，GLOB 不递归，也要显式加入。
if(BOARD_DIR STREQUAL "m5stack/core-s3")
    list(APPEND SOURCES
        "boards/m5stack/core-s3/PY32IOExpander_Class.cpp"
        "boards/m5stack/core-s3/FTServo/SCS.cpp"
        "boards/m5stack/core-s3/FTServo/SCSCL.cpp"
        "boards/m5stack/core-s3/FTServo/SCSerial.cpp"
        # ★ 传感器驱动（GLOB 不递归，子目录必须显式列出）
        #   Si12T = 头顶触摸（I2C 0x68）；motion_detector = 摇晃判定
        "boards/m5stack/core-s3/drivers/Si12T/Si12T.cpp"
        # ★ BMI270 的 API 符号来自上游自带的组件 espressif/bmi270_sensor
        #   （预编译 .a，组件管理器在 build 时自动下载）
        #   ⇒ 同目录 BMI270_SensorAPI/ 下的 7 个 .c 是 Bosch 参考源码，
        #     本版【没用到】，不要加进清单
        "boards/m5stack/core-s3/drivers/bmi270/bmi270.cpp")
    # ★ 官方几何脸（第二套皮肤）
    #   放在 stackchan_avatar/ 子目录，GLOB 不递归 ⇒ 必须显式列出。
    #   ⛔ 只列 .cpp；素材 .c 里是 const 数组，需要单独加（见下）。
    list(APPEND SOURCES
        "boards/m5stack/core-s3/stackchan_avatar/decorators/angry.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/dizzy.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/heart.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/shy.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/sweat.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/skins/default/default.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/skins/default/eyes.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/skins/default/mouth.cpp"
        "boards/m5stack/core-s3/stackchan_avatar/skins/default/speech_bubble.cpp")
    # ★ 素材（LVGL 图片数组，LV_IMAGE_DECLARE 声明 + .c 定义）
    #   ⛔ 少了这些 ⇒ 链接期 undefined reference to `decorator_xxx`
    list(APPEND SOURCES
        "boards/m5stack/core-s3/stackchan_avatar/decorators/assets/decorator_angry.c"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/assets/decorator_dizzy.c"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/assets/decorator_heart.c"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/assets/decorator_shy.c"
        "boards/m5stack/core-s3/stackchan_avatar/decorators/assets/decorator_sweat.c"
        "boards/m5stack/core-s3/stackchan_avatar/skins/default/assets/default_bubble_arrow.c")
endif()
'''

PRIV_EXTRA = ["lvgl", "smooth_ui_toolkit", "mooncake", "mooncake_log"]
PRIV_ANCHOR = "                        xiaozhi-fonts\n"

# ── 表情别名表（和本项目一致：23 种情绪 → idle，thinking → thinking）──
IDLE_EMOTIONS = ["angry", "confident", "confused", "cool", "crying", "delicious",
                 "embarrassed", "funny", "happy", "idle", "kissy", "laughing",
                 "listening", "loving", "neutral", "relaxed", "sad", "shocked",
                 "silly", "sleepy", "speaking", "surprised", "winking"]

FAIRY_BRANCH = '''    # ★ Local extension (Fairy project): use a repo-local asset directory.
    #    Placed OUTSIDE managed_components/ on purpose: that directory is
    #    managed by the component manager (has .component_hash / CHECKSUMS.json),
    #    so edits there can be reverted or rejected on the next reconfigure.
    if default_emoji_collection == 'fairy':
        if project_root:
            fairy_path = os.path.join(project_root, 'fairy-assets')
            if os.path.exists(fairy_path):
                print(f"Using Fairy emoji collection: {fairy_path}")
                return fairy_path
            else:
                print(f"Warning: Fairy emoji collection directory not found: {fairy_path}")
                return None
        else:
            print("Warning: project_root not provided, cannot locate fairy collection")
            return None

'''

ASSETS_README = """# fairy-assets —— 全屏表情（GIF）放这里

固件启动时会把本目录里的 GIF 打包进 `assets.bin`，作为设备的全屏表情。

## 需要什么

    idle.gif        待机 / 大部分情绪（本项目自用 1.17 MB 那个）
    thinking.gif    思考中
    _emote_aliases.json   情绪别名表（本目录已生成）

GIF 尺寸建议 ≤ 320x240、循环播放；本项目实测单张 1 MB 左右最稳。

## ★ 本仓库【不附带】任何 GIF

原因见 `CREDITS.md`：Fairy 的美术形象版权属于米哈游，不能随包分发。
你可以：

  1. **自己画 / 自己做**（零版权风险，推荐）
  2. 用上游自带的 otto 表情包：把 `main/CMakeLists.txt` 里 core-s3 那段的
     `set(DEFAULT_EMOJI_COLLECTION fairy)` 改成 `otto-gif`，并把 GIF 换成上游的
  3. 什么都不换 ⇒ 用上游默认的彩色 emoji（不是全屏动画）

放好 GIF 后重新跑一次 `python3 tools/apply_to_upstream.py <上游目录>`
（它会按你的文件名重写 `_emote_aliases.json`），然后重新编译。
"""


def say(msg=""):
    print(msg, flush=True)


def die(msg):
    say("\n❌ " + msg)
    sys.exit(1)


def write_text(path, text, dry):
    if dry:
        say("      [dry-run] 会写入 " + str(path))
        return
    path.write_text(text, encoding="utf-8", newline="\n")


def patch_text(path, anchor, insert, marker, dry, label, before=False):
    """在 anchor 前后插入 insert；marker 已存在则跳过（幂等）。"""
    text = path.read_text(encoding="utf-8")
    if marker and marker in text:
        say("      = 已经改过（%s），跳过" % label)
        return
    if anchor not in text:
        die("锚点没找到 ⇒ %s\n       期望的锚点: %r\n       上游可能改了代码结构，请手工按 INSTALL.md 处理。" % (label, anchor[:60]))
    new = text.replace(anchor, insert + anchor if before else anchor + insert, 1)
    if dry:
        say("      [dry-run] 会改 " + str(path) + "  （%s）" % label)
        return
    path.write_text(new, encoding="utf-8", newline="\n")
    say("      ✅ 已改 " + str(path) + "（%s）" % label)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("target", help="上游 xiaozhi-esp32 目录")
    ap.add_argument("--dry-run", action="store_true", help="只显示会改什么")
    ap.add_argument("--no-cmake", action="store_true", help="不动 CMakeLists / 表情包")
    ap.add_argument("--gif-dir", help="把这里的 *.gif 拷进 fairy-assets/")
    args = ap.parse_args()

    tgt = Path(args.target).expanduser().resolve()
    dry = args.dry_run
    say("上游目录: " + str(tgt) + ("   [dry-run]" if dry else ""))

    # ── 0. 校验目标 ───────────────────────────────────────────────
    if not (tgt / "main" / "CMakeLists.txt").is_file():
        die("这不像 xiaozhi-esp32 目录（找不到 main/CMakeLists.txt）")
    board = tgt / "main" / "boards" / "m5stack" / "core-s3"
    if not board.is_dir():
        die("找不到 main/boards/m5stack/core-s3/ ⇒ 目录不对或上游结构变了")
    say("✅ 目标校验通过")

    # ── 1~5. 覆盖文件 ─────────────────────────────────────────────
    say("\n【1/9】覆盖板卡目录 → main/boards/m5stack/core-s3/")
    if not dry:
        for item in sorted((FW / "board-core-s3").iterdir()):
            dst = board / item.name
            if item.is_dir():
                shutil.copytree(item, dst, dirs_exist_ok=True)
            else:
                shutil.copy2(item, dst)
    say("      ✅ 完成（%d 项）" % len(list((FW / "board-core-s3").iterdir())))

    say("\n【2/9】覆盖公共 I2C 设备层 → main/boards/common/")
    common = tgt / "main" / "boards" / "common"
    if not dry:
        common.mkdir(parents=True, exist_ok=True)
        for f in (FW / "board-common").glob("i2c_device.*"):
            shutil.copy2(f, common / f.name)
    say("      ✅ 完成")

    say("\n【3/9】覆盖本地组件 → components/")
    comp = tgt / "components"
    if not dry:
        comp.mkdir(parents=True, exist_ok=True)
        for d in sorted((FW / "components").iterdir()):
            if d.is_dir():
                shutil.copytree(d, comp / d.name, dirs_exist_ok=True)
    say("      ✅ 完成（%d 个组件）" % len(list((FW / "components").iterdir())))

    say("\n【4/9】覆盖 ota.cc → main/ota.cc")
    if not dry:
        shutil.copy2(FW / "ota.cc", tgt / "main" / "ota.cc")
    say("      ✅ 完成")

    say("\n【5/9】覆盖显示层 → main/display/")
    if not dry:
        for f in (FW / "display").glob("lcd_display.*"):
            shutil.copy2(f, tgt / "main" / "display" / f.name)
    say("      ✅ 完成")

    # ── 6. 唤醒词 ─────────────────────────────────────────────────
    say("\n【6/9】唤醒词：Hi Fairy 写进 sdkconfig.defaults.esp32s3")
    cfg = tgt / "sdkconfig.defaults.esp32s3"
    if not cfg.is_file():
        die("上游没有 sdkconfig.defaults.esp32s3 ⇒ 请检查上游版本")
    text = cfg.read_text(encoding="utf-8")
    if "CONFIG_SR_WN_WN9_HIFAIRY_TTS2=y" in text:
        say("      = 已存在，跳过")
    else:
        if dry:
            say("      [dry-run] 会追加 CONFIG_SR_WN_WN9_HIFAIRY_TTS2=y")
        else:
            with cfg.open("a", encoding="utf-8", newline="\n") as f:
                f.write("\n# ★ 本项目加的第二个唤醒词（上游只带「你好小智」）\n")
                f.write("CONFIG_SR_WN_WN9_HIFAIRY_TTS2=y\n")
            say("      ✅ 已追加（⛔ 注意：写的是 defaults 文件，不是 sdkconfig）")

    if args.no_cmake:
        say("\n（--no-cmake：跳过 7~9 步）")
        return _next_steps()

    # ── 7. main/CMakeLists.txt ────────────────────────────────────
    say("\n【7/9】改 main/CMakeLists.txt")
    cm = tgt / "main" / "CMakeLists.txt"
    # 7a 子目录源文件
    patch_text(cm, "list(APPEND SOURCES ${BOARD_SOURCES})\n", "\n" + SOURCES_BLOCK,
               "stackchan_avatar", dry, "子目录源文件（FTServo/drivers/avatar/素材）")
    # 7b PRIV_REQUIRES
    patch_text(cm, PRIV_ANCHOR,
               "".join("                        %s\n" % x for x in PRIV_EXTRA),
               "smooth_ui_toolkit", dry, "PRIV_REQUIRES 加 4 个组件（lvgl + 3 个本地组件）")

    # 7c 表情包名（只在真有 GIF 时改，避免编出空表情包）
    assets_dir = tgt / "fairy-assets"
    if args.gif_dir:
        gifs_src = Path(args.gif_dir).expanduser()
        if not dry:
            assets_dir.mkdir(parents=True, exist_ok=True)
            for g in sorted(gifs_src.glob("*.gif")):
                shutil.copy2(g, assets_dir / g.name)
        say("      ✅ 从 %s 拷入 GIF" % gifs_src)
    gifs = sorted(p.name for p in assets_dir.glob("*.gif")) if assets_dir.is_dir() else []

    if gifs or "--force-emoji" in sys.argv:
        text = cm.read_text(encoding="utf-8")
        if "set(DEFAULT_EMOJI_COLLECTION fairy)" in text:
            say("      = 表情包名已经是 fairy，跳过")
        else:
            # 只改 CoreS3 那一块，别动别的板卡
            start = text.find("elseif(CONFIG_BOARD_TYPE_M5STACK_CORE_S3)")
            end = text.find("elseif(", start + 10)
            if start < 0 or end < 0:
                die("找不到 CoreS3 的 elseif 块 ⇒ 请手工改 set(DEFAULT_EMOJI_COLLECTION fairy)")
            block = text[start:end]
            new_block = None
            for orig in ("noto-color-emoji_64", "noto-color-emoji_128", "noto-color-emoji_32"):
                if "set(DEFAULT_EMOJI_COLLECTION %s)" % orig in block:
                    new_block = block.replace("set(DEFAULT_EMOJI_COLLECTION %s)" % orig,
                                              "set(DEFAULT_EMOJI_COLLECTION fairy)", 1)
                    break
            if new_block is None:
                die("CoreS3 块里没有 set(DEFAULT_EMOJI_COLLECTION ...) ⇒ 请手工改")
            if dry:
                say("      [dry-run] 会把 CoreS3 的表情包名改成 fairy")
            else:
                cm.write_text(text[:start] + new_block + text[end:], encoding="utf-8", newline="\n")
                say("      ✅ 表情包名 → fairy（只改 CoreS3 那块）")
    else:
        say("      ⚠️ fairy-assets/ 里还没有 GIF ⇒ 跳过表情包设置")
        say("         （不放 GIF 就改的话，表情会是空白。放好 GIF 再重跑本脚本即可。）")

    # ── 8. build_default_assets.py ────────────────────────────────
    say("\n【8/9】改 scripts/build_default_assets.py（加 fairy 表情包分支）")
    bda = tgt / "scripts" / "build_default_assets.py"
    if not bda.is_file():
        die("找不到 scripts/build_default_assets.py")
    patch_text(bda, "    # Try PNG emoji collections first.\n", FAIRY_BRANCH,
               "fairy-assets", dry, "fairy 表情包分支")

    # ── 9. fairy-assets/ ──────────────────────────────────────────
    say("\n【9/9】fairy-assets/（别名表 + 说明）")
    if not dry:
        assets_dir.mkdir(parents=True, exist_ok=True)
        readme = assets_dir / "README.md"
        if not readme.is_file():
            readme.write_text(ASSETS_README, encoding="utf-8", newline="\n")
        alias = {}
        for g in gifs:
            low = g.lower()
            if "think" in low:
                alias[g] = ["thinking"]
            elif "idle" in low:
                alias[g] = IDLE_EMOTIONS
            else:
                name = low[:-4]
                alias[g] = [name] if name in IDLE_EMOTIONS + ["thinking"] else []
        if gifs:
            (assets_dir / "_emote_aliases.json").write_text(
                json.dumps(alias, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8", newline="\n")
            say("      ✅ 已按文件名生成 _emote_aliases.json：")
            for k, v in alias.items():
                say("         %-22s → %s" % (k, ("%d 个情绪" % len(v)) if v else "⚠️ 未匹配到情绪，请手工改"))
        else:
            say("      ⚠️ 还没有 GIF：目录和说明已建好，放入 GIF 后重跑本脚本")
    else:
        say("      [dry-run] 会建 fairy-assets/（别名表 + README）")

    return _next_steps()


def _next_steps():
    say("""
────────────────────────────────────────────────────────────────
  下一步（★ 顺序不能反，见 INSTALL.md 1.6）：

    cd <上游目录>
    . ./export.sh                     # Windows: export.bat（每个新终端都要）
    idf.py set-target esp32s3         # 先生成 sdkconfig
    echo "CONFIG_BOARD_TYPE_M5STACK_CORE_S3=y" >> sdkconfig   # 选 CoreS3 板卡
    idf.py reconfigure                # 让它生效
    idf.py build                      # 首次 10~20 分钟
    python3 tools/verify_artifact.py build/xiaozhi.bin        # 校验产物
    idf.py -p <串口> flash
    idf.py -p <串口> monitor

  ★ 想合成一个整机 bin（给别人刷）：idf.py merge-bin
     （⛔ 不要写成 -o build/xxx.bin —— idf.py 是在 build/ 里执行的，
       那样会变成 build/build/xxx.bin 直接报错；要指定就用裸文件名）
  ★ 服务器地址：本仓库不写死，用设备配网页填或语音说（见 INSTALL.md 1.5）
────────────────────────────────────────────────────────────────""")


if __name__ == "__main__":
    main()
