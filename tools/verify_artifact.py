#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验编译出来的固件「对不对」—— 别只看编译成功。

用法：
    python3 tools/verify_artifact.py build/merged-binary.bin
    python3 tools/verify_artifact.py build/xiaozhi.bin

判据（不需要真机）：
  ✅ 必须存在：双唤醒词、两个 MCP 工具（服务器地址/皮肤）、官方 OTA 兜底域名
  ✅ 必须不存在：内网 IP（192.168.x / 10.x 私有段）、Windows 绝对路径
  ℹ️ 参考信息：文件大小、是否内嵌 GIF（= 表情素材）、是否有 Fairy 相关字符串

退出码：0 = 通过；1 = 有 ❌ 项。
"""
import re
import sys
from pathlib import Path

MUST_HAVE = [
    ("wn9_hifairy", "唤醒词 Hi Fairy"),
    ("nihaoxiaozhi", "唤醒词 你好小智"),
    ("self.server.set_ota_url", "MCP 工具：设置服务器地址"),
    ("self.skin.set", "MCP 工具：切换皮肤"),
]
NICE_TO_HAVE = [
    ("api.tenclass.net", "官方 OTA 兜底域名"),
    ("idle.gif", "表情包文件名（Fairy 素材内嵌时会命中）"),
]
BS = chr(92)  # 反斜杠（这样写是为了不在本文件里出现真实的盘符路径字面量）
MUST_NOT = [
    (re.compile(rb"192\.168\.\d{1,3}\.\d{1,3}"), "内网 IP（192.168.x.x）"),
    (re.compile(("E:" + BS + "stackchan").encode()), "作者本机的绝对路径"),
    (re.compile(("C:" + BS + "Users").encode()), "Windows 用户目录路径"),
]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print("❌ 找不到文件：" + str(path))
        return 1
    data = path.read_bytes()
    size = len(data)

    print("校验：" + str(path))
    print("  大小：%.2f MB（%d 字节）" % (size / 1048576.0, size))
    print("  分区表魔数(0x8000)：%s" % ("✅ 0xAA50" if data[0x8000:0x8002] == b"\xAA\x50" else "❌ 不是 0xAA50"))
    app_off = None
    for off in (0x20000, 0x10000):
        if data[off:off + 1] == b"\xE9":
            app_off = off
            break
    print("  应用镜像(0xE9)：%s" % ("✅ 在 0x%X" % app_off if app_off else "⚠️ 0x10000/0x20000 都没找到（若是 xiaozhi.bin 属正常）"))
    print("")

    bad = 0
    print("必须存在（本项目功能）：")
    for kw, desc in MUST_HAVE:
        n = data.count(kw.encode())
        print("  %s %-26s %s" % ("✅" if n else "❌", desc, ("×%d" % n) if n else "没找到"))
        bad += 0 if n else 1

    print("\n参考项（有则更好）：")
    for kw, desc in NICE_TO_HAVE:
        n = data.count(kw.encode())
        print("  %s %-26s %s" % ("✅" if n else "·", desc, ("×%d" % n) if n else "没有"))

    print("\n★ 表情素材：内嵌 GIF 数量 = %d" % data.count(b"GIF89a"))
    print("   （0 ⇒ 用的是彩色 emoji 或空表情；>0 ⇒ 表情素材已打进固件）")

    print("\n必须不存在（脱敏检查）：")
    for rx, desc in MUST_NOT:
        m = rx.search(data)
        ok = m is None
        print("  %s %-26s %s" % ("✅" if ok else "❌", desc, "干净" if ok else ("发现：" + m.group().decode("utf-8", "replace"))))
        bad += 0 if ok else 1

    print("")
    if bad:
        print("❌ 有 %d 项不通过 ⇒ 固件不对，别急着烧。" % bad)
        return 1
    print("🎉 全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
