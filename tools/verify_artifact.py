#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验编译出来的固件「对不对」—— 别只看编译成功。

用法：
    python3 tools/verify_artifact.py build/merged-binary.bin   # 整机固件（推荐）
    python3 tools/verify_artifact.py build/xiaozhi.bin         # 只有 app 也能查

判据（不需要真机）：
  ✅ 必须存在：双唤醒词、两个 MCP 工具（服务器地址/皮肤）
  ✅ 必须不存在：作者私有的内网 IP、作者本机路径
  ℹ️ 参考信息：大小、内嵌 GIF 数量（= 表情素材有没有打进去）

★ 注意：唤醒词和表情都在【assets 分区】里，不在 app 本体里 ——
  所以查 app 单体时会自动去找同目录的 generated_assets.bin。

退出码：0 = 通过；1 = 有 ❌ 项。
"""
import re
import sys
from pathlib import Path

MUST_HAVE = [
    (b"self.server.set_ota_url", "MCP 工具：设置服务器地址"),
    (b"self.skin.set", "MCP 工具：切换皮肤"),
]
WAKE_WORDS = [
    (b"wn9_hifairy", "唤醒词 Hi Fairy"),
    (b"nihaoxiaozhi", "唤醒词 你好小智"),
]
NICE = [(b"api.tenclass.net", "官方 OTA 兜底域名")]

# ★ 192.168.4.1 是 ESP32 SoftAP（配网热点）的默认地址，来自上游组件，不是作者私货
ALLOWED_IPS = {b"192.168.4.1"}
IP_RX = re.compile(rb"192\.168\.\d{1,3}\.\d{1,3}")
BS = chr(92)  # 反斜杠（不出现在本文件里的真实路径字面量形式）
MUST_NOT = [
    (re.compile(re.escape(("E:" + BS + "stackchan").encode())), "作者本机的绝对路径"),
    (re.compile(re.escape(("C:" + BS + "Users").encode())), "Windows 用户目录路径"),
]


def find_assets(path):
    """app 单体旁边/上一级的 generated_assets.bin。"""
    for cand in (path.parent / "generated_assets.bin",
                 path.parent / "assets" / "generated_assets.bin"):
        if cand.is_file():
            return cand
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print("❌ 找不到文件：" + str(path))
        return 1
    data = path.read_bytes()
    bad = 0

    merged = len(data) > 0x8002 and data[0x8000:0x8002] == b"\xAA\x50"
    print("校验：" + str(path))
    print("  类型：%s ｜ 大小：%.2f MB（%d 字节）"
          % ("整机合并固件" if merged else "app 单体（只含应用）",
             len(data) / 1048576.0, len(data)))
    if merged:
        ok = data[0x8000:0x8002] == b"\xAA\x50"
        print("  分区表魔数(0x8000)：%s" % ("✅ 0xAA50" if ok else "❌ 不是 0xAA50"))
        bad += 0 if ok else 1
        for off in (0x20000, 0x10000):
            if data[off:off + 1] == b"\xE9":
                print("  应用镜像(0xE9)：✅ 在 0x%X" % off)
                break
        else:
            print("  应用镜像(0xE9)：⚠️ 没在 0x10000/0x20000 找到")
    else:
        ok = data[:1] == b"\xE9"
        print("  应用镜像头(0xE9)：%s" % ("✅ 对" if ok else "❌ 不对（这个文件不是 app 固件？）"))
        bad += 0 if ok else 1

    # ── MCP 工具（app 本体里）───────────────────────────────
    print("\n必须存在 —— 本项目功能：")
    for kw, desc in MUST_HAVE:
        n = data.count(kw)
        print("  %s %-24s %s" % ("✅" if n else "❌", desc, ("×%d" % n) if n else "没找到"))
        bad += 0 if n else 1

    # ── 唤醒词（在 assets 里）───────────────────────────────
    assets = find_assets(path)
    if assets:
        adata = assets.read_bytes()
        print("  （唤醒词查的是 assets：%s，%.1f MB）" % (assets.name, len(adata) / 1048576.0))
        for kw, desc in WAKE_WORDS:
            n = adata.count(kw) + data.count(kw)
            print("  %s %-24s %s" % ("✅" if n else "❌", desc, ("×%d" % n) if n else "没找到"))
            bad += 0 if n else 1
    else:
        for kw, desc in WAKE_WORDS:
            n = data.count(kw)
            print("  %s %-24s %s" % ("✅" if n else "⚠️", desc,
                                    ("×%d" % n) if n else "没找到（找不到 assets 文件，无法确认；建议对整机固件跑）"))

    print("\n参考项：")
    for kw, desc in NICE:
        n = data.count(kw)
        print("  %s %-24s %s" % ("✅" if n else "·", desc, ("×%d" % n) if n else "没有"))

    print("\n★ 表情素材：内嵌 GIF 数量 = %d" % data.count(b"GIF89a"))
    print("   （0 ⇒ 用彩色 emoji / 空表情；>0 ⇒ 表情素材已打进固件）")

    print("\n必须不存在 —— 脱敏检查：")
    for rx, desc in MUST_NOT:
        m = rx.search(data)
        ok = m is None
        print("  %s %-24s %s" % ("✅" if ok else "❌", desc,
                                "干净" if ok else ("发现：" + m.group().decode("utf-8", "replace"))))
        bad += 0 if ok else 1
    ips = sorted({m.group().decode() for m in IP_RX.finditer(data)})
    stray = [i for i in ips if i.encode() not in ALLOWED_IPS]
    print("  %s %-24s %s" % ("✅" if not stray else "❌", "内网 IP 泄漏",
                            "没有" if not stray else ("发现：" + ", ".join(stray))))
    if ips:
        print("     （全部 192.168.x 匹配：%s；其中 192.168.4.1 是 SoftAP 默认地址，允许）" % ", ".join(ips))
    bad += 0 if not stray else 1

    print("")
    if bad:
        print("❌ 有 %d 项不通过 ⇒ 固件不对，别急着烧。" % bad)
        return 1
    print("🎉 全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
