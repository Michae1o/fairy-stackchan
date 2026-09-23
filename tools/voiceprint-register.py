# -*- coding: utf-8 -*-
"""注册 / 管理说话人声纹（Fairy 声纹识别）

配套服务：voiceprint-api（3D-Speaker），本机 8005 端口
配套固件/服务器：小智服务器 data/.config.yaml 里的 voiceprint.speakers

用法（一般不用手敲，双击「录音并注册声纹.bat」即可）：
    python tools/voiceprint-register.py                 交互式：选说话人 → 录音 → 注册
    python tools/voiceprint-register.py --file x.wav    用现成 wav 注册
    python tools/voiceprint-register.py --list          查看已注册的声纹
    python tools/voiceprint-register.py --delete ID     删除某个声纹

★ 关键约束（踩过才发现）：
  注册用的 speaker_id 必须与小智服务器 data/.config.yaml 里 speakers 列表中的
  speaker_id 【完全一致】，否则识别出来了也映射不到名字。
  ⇒ 本脚本默认从那份配置里读取候选列表让你选，不让你手打 ID。
"""
import argparse
import json
import os
import sys
import time
import urllib.request
import urllib.error
import uuid

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

# ★ 仓库根：从本脚本所在目录（<仓库根>/tools/）上溯一层，不写死路径
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VP_CONF = os.path.join(ROOT, "voiceprint-api", "data", ".voiceprint.yaml")
XZ_CONF = os.path.join(ROOT, "xiaozhi-server", "main", "xiaozhi-server", "data", ".config.yaml")
BASE = "http://127.0.0.1:8005"


def load_token():
    """从 voiceprint-api 的配置里取接口令牌"""
    import yaml
    with open(VP_CONF, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    token = (cfg.get("server") or {}).get("authorization") or ""
    if not token:
        print("× 声纹服务配置里没有 authorization，请先启动一次声纹服务（它会自动生成）")
        sys.exit(1)
    return token


def load_speakers():
    """从小智服务器配置里读候选说话人：[(id, 名称, 描述), ...]"""
    import yaml
    if not os.path.isfile(XZ_CONF):
        return []
    with open(XZ_CONF, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}
    out = []
    for s in ((cfg.get("voiceprint") or {}).get("speakers") or []):
        parts = str(s).split(",", 2)
        if len(parts) >= 2:
            out.append((parts[0].strip(), parts[1].strip(),
                        parts[2].strip() if len(parts) > 2 else ""))
    return out


def health(token):
    url = "%s/voiceprint/health?key=%s" % (BASE, token)
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            return json.loads(r.read().decode("utf-8"))
    except Exception as e:
        print("× 连不上声纹服务（%s）：%r" % (BASE, e))
        print("  请先启动它：双击 <你的项目根>\\启动Fairy.bat（声纹服务随它一起启动）")
        sys.exit(1)


def post_multipart(path, token, fields, file_field, file_path):
    """用标准库手写 multipart 上传（避免额外依赖）"""
    boundary = "----hermes" + uuid.uuid4().hex
    with open(file_path, "rb") as f:
        file_data = f.read()
    fname = os.path.basename(file_path)

    body = b""
    for k, v in fields.items():
        body += ("--%s\r\n" % boundary).encode()
        body += ('Content-Disposition: form-data; name="%s"\r\n\r\n' % k).encode()
        body += ("%s\r\n" % v).encode()
    body += ("--%s\r\n" % boundary).encode()
    body += ('Content-Disposition: form-data; name="%s"; filename="%s"\r\n'
             % (file_field, fname)).encode()
    body += b"Content-Type: audio/wav\r\n\r\n"
    body += file_data + b"\r\n"
    body += ("--%s--\r\n" % boundary).encode()

    req = urllib.request.Request(
        BASE + path, data=body, method="POST",
        headers={
            "Content-Type": "multipart/form-data; boundary=%s" % boundary,
            "Authorization": "Bearer %s" % token,
        })
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return True, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")
        return False, {"status": e.code, "detail": detail}
    except Exception as e:
        return False, {"error": repr(e)}


def record_wav(seconds, out_path):
    """用麦克风录一段 mono/16kHz/16bit 的 wav"""
    import sounddevice as sd
    import soundfile as sf

    devs = sd.query_devices()
    ins = [(i, d) for i, d in enumerate(devs) if d["max_input_channels"] > 0]
    if not ins:
        print("× 没找到可用的麦克风")
        return False

    # 默认挑一个「像物理麦克风」的：名字里有麦克风/mic，且不是虚拟设备
    default = None
    for i, d in ins:
        n = d["name"]
        if ("麦克风" in n or "microphone" in n.lower() or "Razer" in n) \
                and not any(x in n for x in ("Broadcast", "Steam", "Virtual", "Sound Mapper")):
            default = i
            break
    if default is None:
        default = ins[0][0]

    print("  可用输入设备：")
    for i, d in ins:
        mark = "  ← 默认" if i == default else ""
        print("    [%d] %s%s" % (i, d["name"], mark))
    raw = input("  选择麦克风编号（回车=默认 %d）：" % default).strip()
    if raw:
        try:
            default = int(raw)
        except ValueError:
            pass

    print("\n  请对着麦克风说 %d 秒话（用平常的语速和音调，正常内容即可）" % seconds)
    for i in range(3, 0, -1):
        print("  %d..." % i, end="", flush=True)
        time.sleep(1)
    print("\n  ● 录音中 ...", flush=True)
    audio = sd.rec(int(seconds * 16000), samplerate=16000, channels=1,
                   dtype="int16", device=default)
    sd.wait()
    print("  ● 录音结束")
    sf.write(out_path, audio, 16000)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--speaker-id", default=None, help="说话人ID（默认交互选择）")
    ap.add_argument("--file", default=None, help="用现成的 wav 文件（跳过录音）")
    ap.add_argument("--seconds", type=int, default=6, help="录音秒数（默认6）")
    ap.add_argument("--list", action="store_true", help="只查看已注册声纹")
    ap.add_argument("--delete", default=None, help="删除指定 speaker_id 的声纹")
    args = ap.parse_args()

    token = load_token()

    if args.list:
        h = health(token)
        print("声纹服务状态: %s，已注册声纹数: %s"
              % (h.get("status"), h.get("total_voiceprints")))
        for sid, name, desc in load_speakers():
            print("    %-10s %s  %s" % (sid, name, desc))
        print("\n  （上面是配置里声明的候选说话人；实际已注册了几个看上方的数字）")
        return 0

    if args.delete:
        req = urllib.request.Request(
            "%s/voiceprint/%s" % (BASE, args.delete), method="DELETE",
            headers={"Authorization": "Bearer %s" % token})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                print("✅ 已删除:", r.read().decode("utf-8"))
        except Exception as e:
            print("× 删除失败: %r" % e)
            return 1
        return 0

    # ── 选说话人 ──
    speakers = load_speakers()
    sid = args.speaker_id
    if not sid:
        if not speakers:
            print("× 小智服务器配置里还没有 voiceprint.speakers 列表")
            print("  请在 %s 里加上，例如：" % XZ_CONF)
            print('    voiceprint:\n      url: http://127.0.0.1:8005?key=<token>')
            print('      speakers:\n        - "me,你的名字,一句话描述你自己"')
            return 1
        print("=== 可注册的说话人（来自小智服务器配置）===")
        for n, (i, nm, desc) in enumerate(speakers, 1):
            print("  %d) %-10s %s  %s" % (n, i, nm, desc))
        raw = input("选一个（直接回车=1）：").strip() or "1"
        try:
            sid = speakers[int(raw) - 1][0]
        except Exception:
            print("× 选择无效")
            return 1
        print("  将注册: %s" % sid)

    # ── 取音频 ──
    wav = args.file
    if not wav:
        tmp = os.path.join(ROOT, "voiceprint-api", "tmp")
        os.makedirs(tmp, exist_ok=True)
        wav = os.path.join(tmp, "register-%s.wav" % uuid.uuid4().hex[:8])
        if not record_wav(args.seconds, wav):
            return 1
    elif not os.path.isfile(wav):
        print("× 找不到音频文件: %s" % wav)
        return 1

    # ── 上传注册 ──
    print("\n  上传注册中 ...")
    ok, res = post_multipart("/voiceprint/register", token,
                             {"speaker_id": sid}, "file", wav)
    if ok:
        print("  ✅ 注册成功: %s" % res)
        h = health(token)
        print("  当前已注册声纹数: %s" % h.get("total_voiceprints"))
        print("\n  ⚠ 别忘了：设备要连的是这台小智服务器（Fairy 皮肤），")
        print("     然后直接问它「你知道我是谁吗」试试。")
    else:
        print("  ❌ 注册失败: %s" % res)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
