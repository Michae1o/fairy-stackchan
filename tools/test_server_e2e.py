#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""模拟设备做端到端测试 —— **不用刷固件**，先证明「服务器那条线」是通的。

和设备说一句话的流程完全一样：
    ① WebSocket 连接（带 device-id 头）
    ② 发 hello → 收服务器 hello      ★ 这一步超时是最隐蔽的失败点
    ③ listen/start → 发 Opus 音频帧（16k / mono / 60ms）→ listen/stop
    ④ 看服务器回什么：stt（识别文字）→ llm（回答 + 情绪）→ tts（Opus 音频帧）

用法：
    python3 tools/test_server_e2e.py --audio ref.wav
    python3 tools/test_server_e2e.py --audio ref.wav --url ws://192.168.1.10:8000/xiaozhi/v1/

    # 没有音频文件？随便一段 5~10 秒人声即可，先转成 16k 单声道：
    ffmpeg -i any.wav -ac 1 -ar 16000 ref.wav
    # 也可以先跑 tools/make_face.py 那个流程里用的参考音频（voice-package/README.md §5）

判据（四条都要 ✅ 才算服务器线通了）：
    服务器 hello ✅ ｜ ASR 识别 ✅ ｜ LLM 回答 ✅ ｜ TTS 音频帧 > 0 ✅

依赖：websockets、opuslib_next（装服务器依赖时已带）、标准库。
退出码：0 = 全通；1 = 有环节没过。
"""
import argparse
import asyncio
import json
import os
import struct
import sys
import time
import wave

# ── 找不到 opus 动态库时的自救（Windows 上常见）──────────────────
#   服务器自己会检查 ffmpeg，但不检查 libopus；独立跑本脚本必须自己加搜索路径。
#   ★ 通用做法：当前 Python 环境（sys.prefix / CONDA_PREFIX）下的 Library\bin
#     —— 不写死任何本机路径，换机器、换环境名都有效。
for _base in (os.environ.get("CONDA_PREFIX", ""), sys.prefix,
              os.path.dirname(sys.executable)):
    _d = os.path.join(_base, "Library", "bin") if _base else ""
    if _d and os.path.isdir(_d) and _d not in os.environ.get("PATH", ""):
        os.environ["PATH"] = _d + os.pathsep + os.environ.get("PATH", "")
        if hasattr(os, "add_dll_directory"):
            try:
                os.add_dll_directory(_d)
            except Exception:
                pass


def pcm16_mono_16k(wav_path):
    """读 WAV → 16kHz / 单声道 / 16bit PCM（纯标准库，不依赖 audioop）。"""
    with wave.open(wav_path, "rb") as w:
        ch, sw, fr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    print("    源音频: %dch %dHz %dbit  %.2fs"
          % (ch, fr, sw * 8, len(raw) / max(fr * ch * sw, 1)))

    # ① 统一成 16bit 有符号样本
    if sw == 1:                                  # 8bit 无符号
        raw = b"".join(struct.pack("<h", (b - 128) << 8) for b in raw)
    elif sw == 3:                                # 24bit 小端有符号 → 取高 16 位
        out = bytearray()
        for i in range(0, len(raw) - 2, 3):
            v = raw[i] | (raw[i + 1] << 8) | (raw[i + 2] << 16)
            if v & 0x800000:
                v -= 0x1000000
            out += struct.pack("<h", v >> 8)
        raw = bytes(out)
    elif sw != 2:
        raise SystemExit("不支持的位深：%d bit" % (sw * 8))

    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)

    # ② 多声道 → 单声道
    if ch > 1:
        samples = [sum(samples[i:i + ch]) // ch for i in range(0, len(samples) - ch + 1, ch)]
        print("    已混合为单声道")

    # ③ 重采样到 16kHz（线性插值，够用；要更好用 ffmpeg 先转 16k 单声道）
    if fr != 16000:
        ratio = 16000.0 / fr
        out_n = int(len(samples) * ratio)
        samples = [samples[min(int(i / ratio), len(samples) - 1)] for i in range(out_n)]
        print("    已重采样 %dHz → 16000Hz" % fr)

    pcm = struct.pack("<%dh" % len(samples), *samples)
    print("    得到 16kHz mono 16bit PCM: %d 字节（%.2f 秒）"
          % (len(pcm), len(samples) / 16000.0))
    return pcm


def pcm_to_opus(pcm, frame_ms=60):
    """PCM → Opus 帧列表（60ms/帧，和设备上行一致）。"""
    try:
        import opuslib_next as opuslib
    except Exception as e:
        raise SystemExit(
            "导入 opuslib_next 失败：%r\n"
            "  Windows 常见原因：找不到 opus.dll ⇒ 把 conda 环境的 Library\\bin 加进 PATH，\n"
            "  或把 opus.dll 放到本脚本同目录/系统 PATH 里。" % (e,))
    enc = opuslib.Encoder(16000, 1, opuslib.APPLICATION_AUDIO)
    spf = 16000 * frame_ms // 1000                 # 每帧样本数 = 960
    bpf = spf * 2                                  # 每帧字节数
    frames = []
    for i in range(0, len(pcm), bpf):
        chunk = pcm[i:i + bpf]
        if len(chunk) < bpf:
            chunk += b"\x00" * (bpf - len(chunk))
        frames.append(enc.encode(chunk, spf))
    print("    已编码 %d 个 Opus 帧（%dms/帧，共 %.1f 秒）"
          % (len(frames), frame_ms, len(frames) * frame_ms / 1000.0))
    return frames


async def run_test(args):
    import websockets

    audio = args.audio
    if not audio:
        for cand in ("fairy-ref", os.path.join("..", "fairy-ref")):
            if os.path.isdir(cand):
                wavs = sorted(f for f in os.listdir(cand) if f.lower().endswith(".wav"))
                if wavs:
                    audio = os.path.join(cand, wavs[0])
                    break
    if not audio or not os.path.exists(audio):
        raise SystemExit(
            "没有可用音频。用 --audio <wav> 指定一段 5~10 秒人声：\n"
            "  ffmpeg -i any.wav -ac 1 -ar 16000 ref.wav\n"
            "（参考音频怎么来见 voice-package/README.md §5）")

    print("=" * 74)
    print("模拟设备端到端测试（不刷固件，验服务器那条线）")
    print("=" * 74)
    print("  服务器 : %s" % args.url)
    print("  音频   : %s" % audio)
    if args.expect:
        print("  期望识别: %s" % args.expect[:46])
    print()

    print("[1/5] 准备音频（WAV → Opus 帧）")
    frames = pcm_to_opus(pcm16_mono_16k(audio))
    print()

    ev = {"stt": [], "llm": [], "tts_audio": 0, "server_hello": None, "errors": []}

    print("[2/5] 连接 WebSocket")
    headers = {"device-id": args.device_id, "client-id": "e2e-test",
               "Authorization": "Bearer " + args.token}
    async with websockets.connect(args.url, additional_headers=headers,
                                  max_size=10 * 1024 * 1024) as ws:
        print("    ✅ 已连接\n")

        print("[3/5] 发 hello → 收服务器 hello")
        await ws.send(json.dumps({
            "type": "hello", "version": 1, "transport": "websocket",
            "audio_params": {"format": "opus", "sample_rate": 16000,
                             "channels": 1, "frame_duration": 60},
            "features": {"mcp": False, "aec": False}}))
        try:
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
            ev["server_hello"] = msg
            print("    ✅ 收到服务器 hello ｜ type=%s version=%s session=%s"
                  % (msg.get("type"), msg.get("version"), msg.get("session_id")))
            if "audio_params" in msg:
                print("       audio_params=%s"
                      % json.dumps(msg["audio_params"], ensure_ascii=False))
        except asyncio.TimeoutError:
            print("    ❌ 服务器 hello 超时 —— 最隐蔽的失败点")
            print("       （服务器 hello 的 transport 必须是字符串 \"websocket\"）")
            ev["errors"].append("hello timeout")
        print()

        print("[4/5] listen/start → 发音频 → listen/stop")
        await ws.send(json.dumps({"type": "listen", "state": "start", "mode": "manual"}))
        await asyncio.sleep(0.3)
        for fr in frames:
            await ws.send(fr)
            await asyncio.sleep(0.06)               # 按实时节奏推
        await asyncio.sleep(1.0)
        await ws.send(json.dumps({"type": "listen", "state": "stop"}))
        print("    已发送 %d 帧（%.1f 秒）\n" % (len(frames), len(frames) * 0.06))

        print("[5/5] 等服务器响应（ASR → LLM → TTS，最多等 %ds）" % args.timeout)
        t0 = last = time.time()
        while time.time() - t0 < args.timeout:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=5)
            except asyncio.TimeoutError:
                if time.time() - last > 60:
                    print("    （60 秒无活动，结束等待）")
                    break
                continue
            last = time.time()
            if isinstance(msg, bytes):
                ev["tts_audio"] += 1
                if ev["tts_audio"] <= 3 or ev["tts_audio"] % 20 == 0:
                    print("    [音频] +1 帧（%d 字节）  累计 %d 帧" % (len(msg), ev["tts_audio"]))
                continue
            try:
                j = json.loads(msg)
            except Exception:
                continue
            t = j.get("type")
            if t == "stt":
                ev["stt"].append(j.get("text", ""))
                print("    [STT] %s" % j.get("text", ""))
            elif t == "llm":
                ev["llm"].append(j)
                print("    [LLM] emotion=%s text=%s"
                      % (j.get("emotion"), (j.get("text") or "")[:40]))
            elif t == "tts":
                print("    [TTS] state=%s %s" % (j.get("state"), (j.get("text") or "")[:40]))
                if j.get("state") == "stop":
                    break
            else:
                print("    [%s] %s" % (t, json.dumps(j, ensure_ascii=False)[:80]))

        print()
        print("=" * 74)
        print("结果")
        print("=" * 74)
        ok_hello, ok_stt = bool(ev["server_hello"]), bool(ev["stt"])
        ok_llm, ok_tts = bool(ev["llm"]), ev["tts_audio"] > 0
        print("  服务器 hello : %s" % ("✅" if ok_hello else "❌"))
        print("  ASR 识别     : %s  %s" % ("✅" if ok_stt else "❌", ev["stt"][:1]))
        print("  LLM 回答     : %s  %s"
              % ("✅" if ok_llm else "❌",
                 (ev["llm"][0].get("text", "")[:50] if ev["llm"] else "")))
        print("  TTS 音频     : %s  %d 帧 Opus"
              % ("✅" if ok_tts else "❌", ev["tts_audio"]))
        if args.expect and ok_stt and args.expect[:6] not in "".join(ev["stt"]):
            print("  （识别结果和你给的 --expect 不完全一致，一般是 ASR 用词差异，不一定是错）")
        print()

        allok = ok_hello and ok_stt and ok_llm and ok_tts
        if allok:
            print("  🎉 服务器那条线通了 —— 接下来才是固件（INSTALL §1）。")
        else:
            print("  ⚠️ 有环节没过，按下面顺序查：")
            if not ok_hello:
                print("     · 服务器 hello 超时 ⇒ 协议/端口问题（不是固件问题）")
            if not ok_stt:
                print("     · ASR 没结果 ⇒ 音频格式/时长，或服务器 ASR 配置")
            if not ok_llm:
                print("     · LLM 没回答 ⇒ 大模型 Key / 余额 / 配置")
            if not ok_tts:
                print("     · TTS 没音频 ⇒ 音色服务（如 GPT-SoVITS :9880）没起或地址写错")
            print("     · 也别忘看服务器日志里对应的报错")
        return allok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", help="一段 5~10 秒人声 WAV（不填则自动找 ./fairy-ref/*.wav）")
    ap.add_argument("--url", default="ws://127.0.0.1:8000/xiaozhi/v1/",
                    help="服务器 WebSocket 地址（默认本机 8000）")
    ap.add_argument("--expect", default=None, help="期望识别到的文字（可选，只作提示）")
    ap.add_argument("--device-id", default="aa:bb:cc:dd:ee:ff", help="模拟的设备 MAC")
    ap.add_argument("--token", default="test-token", help="Authorization token（auth 关掉时随便填）")
    ap.add_argument("--timeout", type=int, default=90, help="等响应总时长（秒）")
    args = ap.parse_args()
    try:
        sys.exit(0 if asyncio.run(run_test(args)) else 1)
    except KeyboardInterrupt:
        print("\n中断")
        sys.exit(130)


if __name__ == "__main__":
    main()
