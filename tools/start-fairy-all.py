#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""一键启动 Fairy 全套服务（GPT-SoVITS + Fairy 服务器）

用法:
    python tools/start-fairy-all.py            # 启动（若已启动则跳过）
    python tools/start-fairy-all.py --restart  # 强制重启全部
    python tools/start-fairy-all.py --status   # 只看状态，不启动

它做的事：
    1. 检查两个服务是否已在跑（幂等，不会重复启动）
    2. 按顺序启动：GPT-SoVITS(:9880) → 等待就绪 → Fairy服务器(:8000/8003)
    3. 每个服务独立后台进程 + 独立日志文件
    4. 最后做健康检查并打印结论

★ 关于"黑框"：本脚本用 pythonw 或 DETACHED 方式启动子进程，
   自身退出后子进程继续跑，不会占着终端。
"""
import argparse
import os
import socket
import subprocess
import sys
import time

# ★ 关键：输出实时刷新。
#   被别的程序调用（无终端）时 Python 默认全缓冲 → 进度全卡在缓冲里，
#   用户看到的是"跑了几分钟一个字都没输出"。启动这种长流程必须 line-buffered。
try:
    sys.stdout.reconfigure(line_buffering=True)
    sys.stderr.reconfigure(line_buffering=True)
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GPTDIR = os.path.join(ROOT, "gpt-sovits")
CONDAPY = os.path.join(ROOT, "miniconda3", "envs", "xiaozhi", "python.exe")
CONDABIN = os.path.join(ROOT, "miniconda3", "envs", "xiaozhi", "Library", "bin")
SRVPROJ = os.path.join(ROOT, "xiaozhi-server", "main", "xiaozhi-server")
# ★ 声纹识别服务（3D-Speaker，2026-09-22 接入）
VPDIR = os.path.join(ROOT, "voiceprint-api")
VPVENV = os.path.join(VPDIR, "venv", "Scripts", "pythonw.exe")
VPVENV_FALLBACK = os.path.join(VPDIR, "venv", "Scripts", "python.exe")
VP_LOG = os.path.join(ROOT, "logs", "voiceprint.log")
VP_PORT = 8005

# ★★ 用 pythonw.exe 启动服务 —— 这才是"无黑框"的正解
#   python.exe 是控制台程序，即使传 CREATE_NO_WINDOW，Windows 仍可能给它分配
#   一个 Windows Terminal 窗口（用户实测会弹出，且关掉窗口=杀掉服务）。
#   pythonw.exe 是【Windows 子系统】程序，天然不带控制台。
GPTVENV = os.path.join(GPTDIR, "venv", "Scripts", "pythonw.exe")   # ★ pythonw
GPTVENV_FALLBACK = os.path.join(GPTDIR, "venv", "Scripts", "python.exe")
CONDAPYW = os.path.join(ROOT, "miniconda3", "envs", "xiaozhi", "pythonw.exe")
if not os.path.exists(CONDAPYW):
    CONDAPYW = CONDAPY

GPT_LOG = os.path.join(ROOT, "logs", "gpt-sovits.log")
SRV_LOG = os.path.join(ROOT, "logs", "fairy-server.log")

GPT_PORT, SRV_PORT_WS, SRV_PORT_HTTP = 9880, 8000, 8003

# Windows 上让子进程脱离父进程控制台（不占黑框、父进程退出也不被杀）
if os.name == "nt":
    DETACHED = 0x00000008 | 0x00000200      # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    NOWINDOW = 0x08000000                   # CREATE_NO_WINDOW
    FLAGS = DETACHED | NOWINDOW
else:
    FLAGS = 0


def port_open(port, host="127.0.0.1", timeout=2):
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        return True
    except Exception:
        return False
    finally:
        s.close()


def pids_on(port):
    """返回占用该端口的 PID 列表"""
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "(Get-NetTCPConnection -LocalPort %d -State Listen -EA "
             "SilentlyContinue).OwningProcess" % port],
            capture_output=True, text=True)
        return [int(x) for x in (r.stdout or "").split() if x.strip().isdigit()]
    except Exception:
        return []


def all_service_pids():
    """返回所有【与本项目相关】的 python 进程 PID（含不占端口的父进程/僵尸）

    ★ 为什么不能只看端口：api_v2.py / app.py 常有【父进程 + 子进程】两层，
      父进程不占端口。只按端口判定会漏掉父进程，而杀了子进程后
      父进程可能留下，或反之杀父进程会带走子进程（本会话踩过这个坑）。
    """
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "Get-CimInstance Win32_Process -Filter \"Name='pythonw.exe' OR "
             "Name='python.exe'\" | "
             "Where-Object { $_.CommandLine -match 'api_v2|app\\.py|voiceprint-api' } | "
             "Select-Object -ExpandProperty ProcessId"],
            capture_output=True, text=True)
        # ★ 排除自身：--restart 是自己启动的，若不排除，
        #   杀进程那一步会把【自己】也算进去（实测踩过）
        me = os.getpid()
        return sorted({int(x) for x in (r.stdout or "").split()
                       if x.strip().isdigit() and int(x) != me})
    except Exception:
        return []


def kill_port(port):
    """按端口杀（用于常规 restart）"""
    ps = pids_on(port)
    for p in set(ps):
        subprocess.run(["taskkill", "/PID", str(p), "/T", "/F"],
                       capture_output=True)
    if ps:
        time.sleep(2)


def kill_all_service_pids(keep=None, graceful=False):
    """杀掉所有本项目服务进程（含父/子/僵尸），返回被杀的 PID

    ★ 正确做法：对每个 PID 用 taskkill /T（杀整棵树）。
      不要按"是否占端口"筛选 —— 父进程不占端口但必须一起清。

    graceful=True 时先【温和停止】（taskkill 不带 /F，发 WM_CLOSE），
    等 3 秒让进程自己释放端口/连接；还没退的再强制杀。
    ★ 为什么温和优先：/F 是硬杀，TCP 连接来不及正常关闭（FIN/RST），
      会留下 TIME_WAIT，下次启动可能撞端口。用户遇到过一次"刷机后
      8003 被僵尸进程占着"的问题。
    """
    keep = set(keep or [])
    targets = [p for p in all_service_pids() if p not in keep]
    soft_killed, hard_killed = [], []
    if graceful and targets:
        for p in targets:
            subprocess.run(["taskkill", "/PID", str(p), "/T"],
                           capture_output=True)
        time.sleep(3)
        # 复查：还没死的再硬杀
        alive = [p for p in targets if p in set(all_service_pids())]
        for p in alive:
            subprocess.run(["taskkill", "/PID", str(p), "/T", "/F"],
                           capture_output=True)
            hard_killed.append(p)
        soft_killed = [p for p in targets if p not in alive]
        if soft_killed or hard_killed:
            time.sleep(2)
        return targets
    for p in targets:
        subprocess.run(["taskkill", "/PID", str(p), "/T", "/F"],
                       capture_output=True)
    if targets:
        time.sleep(3)
    return targets


def launch(cmd, cwd, logfile, env=None):
    """后台启动进程（无黑框、脱离父进程）

    调用点（两处）：
        launch(cmd, GPTDIR, GPT_LOG)
        launch(cmd, SRVPROJ, SRV_LOG, env=env)
    """
    os.makedirs(os.path.dirname(logfile), exist_ok=True)
    lf = open(logfile, "ab")
    p = subprocess.Popen(cmd, cwd=cwd, stdout=lf, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, env=env,
                         creationflags=FLAGS, close_fds=True)
    return p


def wait_port(port, timeout, label):
    t0 = time.time()
    n = 0
    while time.time() - t0 < timeout:
        if port_open(port):
            print("      ✅ %s 就绪（%.0f 秒）" % (label, time.time() - t0))
            return True
        n += 1
        if n % 5 == 0:
            print("      ... 等待 %s（%.0f 秒）" % (label, time.time() - t0))
        time.sleep(2)
    print("      ❌ %s 超时未就绪（等了 %.0f 秒）" % (label, timeout))
    return False


def status():
    print("=" * 66)
    print("  Fairy 服务状态")
    print("=" * 66)
    rows = [("GPT-SoVITS", GPT_PORT, "9880"),
            ("Fairy服务器(WS)", SRV_PORT_WS, "8000"),
            ("Fairy服务器(HTTP/OTA)", SRV_PORT_HTTP, "8003"),
            ("声纹识别服务(3D-Speaker)", VP_PORT, "8005")]
    allok = True
    for name, port, label in rows:
        ok = port_open(port)
        pids = pids_on(port)
        print("  %-24s :%-5s %s %s"
              % (name, label, "✅ 运行中" if ok else "❌ 未运行",
                 ("PID %s" % ",".join(map(str, pids))) if pids else ""))
        allok &= ok
    print("=" * 66)
    return allok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--restart", action="store_true", help="强制重启全部")
    ap.add_argument("--status", action="store_true", help="只看状态")
    ap.add_argument("--clean", action="store_true",
                    help="清理所有残留服务进程（含不占端口的僵尸）后退出")
    ap.add_argument("--stop", action="store_true",
                    help="温和停止所有服务（先请求退出，不退再强制）")
    args = ap.parse_args()

    print("=" * 66)
    print("  Fairy 一键启动")
    print("=" * 66)

    if args.status:
        sys.exit(0 if status() else 1)

    if args.stop:
        before = all_service_pids()
        print("\n[停止] 发现 %d 个相关进程: %s" % (len(before), before))
        if not before:
            print("       服务本来就未运行")
        else:
            kill_all_service_pids(graceful=True)
            print("       已停止")
        print("\n" + "=" * 66)
        print("  停止后状态")
        print("=" * 66)
        status()
        sys.exit(0)

    if args.clean:
        before = all_service_pids()
        print("\n[清理] 发现 %d 个相关进程: %s" % (len(before), before))
        killed = kill_all_service_pids()
        print("       已清理: %s" % killed)
        print("\n" + "=" * 66)
        print("  清理后状态")
        print("=" * 66)
        status()
        sys.exit(0)

    # ── 前置检查 ──
    for f, desc in [(GPTVENV, "GPT-SoVITS 的 pythonw"),
                    (CONDAPY, "conda 环境 python")]:
        if not os.path.exists(f):
            print("  ⚠️ 找不到 %s: %s（将尝试回退）" % (desc, f))
    if not os.path.exists(GPTVENV):
        if os.path.exists(GPTVENV_FALLBACK):
            print("  ⚠️ pythonw 不可用，回退到 python.exe（可能出现黑框）")
        else:
            print("  ❌ GPT-SoVITS venv 不可用")
            sys.exit(1)

    if args.restart:
        print("\n[强制重启] 清理所有服务进程（含不占端口的残留）")
        PORTS = (GPT_PORT, SRV_PORT_WS, SRV_PORT_HTTP, VP_PORT)   # 9880 / 8000 / 8003 / 8005
        before = all_service_pids()
        if before:
            print("      待清理: %s" % before)
        killed = kill_all_service_pids()
        print("      已清理 %d 个" % len(killed))

        # ★ 用户要求：「3 个都要关闭再重启」
        #   实际做法：杀完【必须验证三个端口全部释放】，
        #   没释放就定向再杀 + 最多重试 3 轮；仍不释放则明确报错退出
        #   （宁可报错，也不要"以为重启了其实旧的还在"）
        for attempt in range(1, 4):
            still = [p for p in PORTS if port_open(p)]
            if not still:
                break
            print("      ⚠️ 第 %d 轮：端口 %s 仍占用，定向清理"
                  % (attempt, still))
            for p in still:
                kill_port(p)
            time.sleep(1.5)
        still = [p for p in PORTS if port_open(p)]
        if still:
            print("      \u2605 无法释放端口 %s \u2014\u2014 停止重启，"
                  "避免新旧进程抢端口" % still)
            sys.exit(1)
        print("      \u2713 四个端口已全部释放（9880 / 8000 / 8003 / 8005）")

    # ── ① GPT-SoVITS ──
    print("\n[1/4] GPT-SoVITS (:9880)")
    if port_open(GPT_PORT) and not args.restart:
        print("      ⏭  已在运行，跳过")
    else:
        gpt_py = GPTVENV if os.path.exists(GPTVENV) else GPTVENV_FALLBACK
        cmd = [gpt_py, "api_v2.py", "-a", "127.0.0.1", "-p", str(GPT_PORT),
               "-c", "GPT_SoVITS/configs/tts_infer.yaml"]
        print("      启动器: %s" % os.path.basename(gpt_py))
        launch(cmd, GPTDIR, GPT_LOG)
        print("      已启动，等待加载模型…")
        if not wait_port(GPT_PORT, 120, "GPT-SoVITS"):
            print("      日志尾部:")
            try:
                print(open(GPT_LOG, encoding="utf-8", errors="replace")
                      .read()[-800:])
            except Exception:
                pass
            sys.exit(1)
        time.sleep(5)       # 让模型完全载入

    # ── ② Fairy 服务器 ──
    print("\n[2/4] Fairy 服务器 (:8000 / :8003)")
    if port_open(SRV_PORT_HTTP) and not args.restart:
        print("      ⏭  已在运行，跳过")
    else:
        env = dict(os.environ)
        # ★ 修正：不再置顶（那会让 conda 目录里的
        #   坏 ffprobe.exe 优先命中，导致 TTS 弹窗 0xC0000139）
        #   改为追加到末尾：系统/venv 的工具优先，conda 兜底
        env["PATH"] = env.get("PATH", "") + os.pathsep + CONDABIN
        cmd = [CONDAPYW, "app.py"]
        print("      启动器: %s" % os.path.basename(CONDAPYW))
        launch(cmd, SRVPROJ, SRV_LOG, env=env)
        print("      已启动，等待加载 SenseVoiceSmall…")
        ok = wait_port(SRV_PORT_HTTP, 180, "Fairy服务器")
        if not ok:
            print("      日志尾部:")
            try:
                print(open(SRV_LOG, encoding="utf-8", errors="replace")
                      .read()[-1200:])
            except Exception:
                pass
            sys.exit(1)

    # ── ③ 声纹识别服务（3D-Speaker，自建）──
    print("\n[3/4] 声纹识别服务 (:8005)")
    if port_open(VP_PORT) and not args.restart:
        print("      ⏭  已在运行，跳过")
    else:
        vp_py = VPVENV if os.path.exists(VPVENV) else VPVENV_FALLBACK
        if not os.path.exists(vp_py):
            print("      ⚠️ 找不到声纹服务的 venv: %s" % vp_py)
            print("         已跳过（不影响其它服务）")
        else:
            cmd = [vp_py, "start_server.py"]
            print("      启动器: %s" % os.path.basename(vp_py))
            # ★ 工作目录必须是 voiceprint-api：它按【相对路径】读
            #   data/.voiceprint.yaml（见 app/core/config.py）
            launch(cmd, VPDIR, VP_LOG)
            print("      已启动，等待加载声纹模型…")
            if not wait_port(VP_PORT, 180, "声纹识别服务"):
                print("      日志尾部:")
                try:
                    print(open(VP_LOG, encoding="utf-8", errors="replace")
                          .read()[-1200:])
                except Exception:
                    pass
                print("      ⚠️ 声纹服务未就绪（其它服务不受影响）")

    # ── ④ 健康检查 ──
    print("\n[4/4] 健康检查")
    if not status():
        print("  ⚠️ 有服务未就绪")
        sys.exit(1)

    print("\n  🎉 全部就绪，可以让设备连上来了")
    # ★ 重启自检（明确成败，避免"以为重启了其实没有"）
    _ok = all(port_open(p) for p in (GPT_PORT, SRV_PORT_WS, SRV_PORT_HTTP))
    # ★ 声纹服务是「附加」功能：它没起来不影响语音对话，
    #   所以只提示、不把它算进致命成败（避免拖累原有三个服务）
    if not port_open(VP_PORT):
        print("      ⚠️ 声纹识别服务(:8005)未就绪（不影响语音对话）")
    if not _ok:
        print("      ★ 自检失败：有端口未就绪")
        sys.exit(1)

    print("     设备 OTA 地址: http://<你的局域网IP>:8003/xiaozhi/ota/")
    print("     日志目录:      %s" % os.path.join(ROOT, "logs"))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n中断")
        sys.exit(130)
