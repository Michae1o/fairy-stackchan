# -*- coding: utf-8 -*-
"""声纹全链路自测（模拟浏览器录音）

为什么要用 webm：浏览器 MediaRecorder 录出来是 **webm/opus**，
服务器收到后用 ffmpeg 转 16k 单声道 wav 再交给声纹服务。
所以只有拿 webm 走一遍，才算真正等同「在网页上点录音」。

跑法：  python tools\\test-voiceprint-e2e.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import uuid

# ★ 路径一律自动推导，不写死（本脚本放在 <仓库根>/tools/ 下）
_HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(_HERE)                       # 仓库根
TMPDIR = os.path.join(tempfile.gettempdir(), "vp-test")
os.makedirs(TMPDIR, exist_ok=True)


def _find_ffmpeg():
    """按 环境变量 → PATH → 同目录 python 的 conda 环境 的顺序找一个能用的 ffmpeg"""
    p = os.environ.get("FFMPEG")
    if p and os.path.isfile(p):
        return p
    p = shutil.which("ffmpeg")
    if p:
        return p
    cand = os.path.abspath(os.path.join(
        os.path.dirname(sys.executable), "..", "Library", "bin", "ffmpeg.exe"))
    return cand if os.path.isfile(cand) else "ffmpeg"


FFMPEG = _find_ffmpeg()
SRC = os.path.join(TMPDIR, "vp-test.wav")
WEBM = os.path.join(TMPDIR, "vp-test.webm")
WAV16K = os.path.join(TMPDIR, "vp-test16k.wav")
CTRL = os.environ.get("CTRL_URL", "http://127.0.0.1:8003")
SVC = os.environ.get("SVC_URL", "http://127.0.0.1:8005")
CONF = os.path.join(ROOT, "voiceprint-api", "data", ".voiceprint.yaml")
TEST_ID = os.environ.get("TEST_ID", "tstvp")
CRLF = "\r\n"


def post_json(url, obj):
    req = urllib.request.Request(url, data=json.dumps(obj).encode("utf-8"),
                                 method="POST",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode("utf-8"))


def get_json(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def multipart(url, fields, filename, data, token=None, ctype="audio/webm"):
    b = "----vp" + uuid.uuid4().hex
    body = b""
    for k, v in fields.items():
        body += ("--" + b + CRLF + 'Content-Disposition: form-data; name="'
                 + k + '"' + CRLF + CRLF + v + CRLF).encode("utf-8")
    body += ("--" + b + CRLF + 'Content-Disposition: form-data; name="file"; filename="'
             + filename + '"' + CRLF + "Content-Type: " + ctype + CRLF + CRLF).encode("utf-8")
    body += data + CRLF.encode("utf-8")
    body += ("--" + b + "--" + CRLF).encode("utf-8")
    headers = {"Content-Type": "multipart/form-data; boundary=" + b}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=body, method="POST", headers=headers)
    with urllib.request.urlopen(req, timeout=180) as r:
        return json.loads(r.read().decode("utf-8"))


def svc_token():
    """token 在 server.authorization（实测结构，不是顶层 key）"""
    import yaml
    with open(CONF, encoding="utf-8") as f:
        c = yaml.safe_load(f.read()) or {}
    return ((c.get("server") or {}).get("authorization") or "").strip()


def main():
    print("=" * 64)
    print("1) 造 webm/opus（模拟浏览器 MediaRecorder 的产物）")
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", SRC,
                    "-c:a", "libopus", "-b:a", "32k", WEBM], check=True)
    webm = open(WEBM, "rb").read()
    print("   webm 大小:", len(webm), "bytes")

    print("2) 用 webm 走控制台注册接口（== 网页点录音）")
    print("   ", json.dumps(multipart(CTRL + "/admin/api/voiceprint/register",
                                     {"speaker_id": TEST_ID}, "record.webm", webm),
                           ensure_ascii=False))

    print("3) 列表确认")
    lst = get_json(CTRL + "/admin/api/voiceprint")
    for s in lst.get("speakers", []):
        print("    id=%-8s name=%-8s registered=%s" % (s["id"], s["name"], s["registered"]))
    print("    服务端已注册:", lst.get("registered"))

    print("4) 识别（同一段音频，跟三个人一起比对，看认出谁）")
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", SRC, "-ar", "16000",
                    "-ac", "1", "-sample_fmt", "s16", WAV16K], check=True)
    wav = open(WAV16K, "rb").read()
    try:
        r = multipart(SVC + "/voiceprint/identify", {"speaker_ids": "tstvp,me,nvzhu"},
                      "probe.wav", wav, token=svc_token(), ctype="audio/wav")
        print("   ", json.dumps(r, ensure_ascii=False))
    except Exception as e:
        print("    识别调用失败:", e)

    print("5) 清理测试数据")
    print("   删声纹:", post_json(CTRL + "/admin/api/voiceprint/delete", {"id": TEST_ID}))
    print("   剩余已注册:", get_json(CTRL + "/admin/api/voiceprint")["registered"])
    print("=" * 64)


if __name__ == "__main__":
    main()
