#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从【微软 Update Catalog】下载 FTDI 的 USB 转串口驱动（Windows 用）。

为什么需要这个脚本
------------------
· 官方 K151-R 遥控器用的是 **FTDI FT232R** 芯片（硬件 ID `USB\\VID_0403&PID_6001`）
· **Windows 不自带 FTDI 驱动**，而且设备管理器的「自动搜索」也找不到
  （微软没把它放进 Windows Update）
· FTDI 官网（ftdichip.com）有 **Cloudflare 人机验证**，脚本抓下来会被 403 挡掉
· 微软的 **Update Catalog** 里有官方发布的 FTDI 驱动，且不拦爬虫  ⇒ 走这里

★ 关键陷阱：微软按**芯片型号**把 FTDI 驱动拆成了好几个包
  （6001 / 7001 / E6B0 / 6007…），搜索页标题全都长一样，肉眼认不出来。
  本脚本把 FTDI 相关的包**全部**下下来、逐个解开，只保留 inf 里真的含
  `VID_0403&PID_6001` 的那个 —— 装错包的话设备照样不认。

用法
----
    python fetch-ftdi-driver.py [--out DIR]

默认输出到 `./ftdi-driver/`。装的时候：

    设备管理器 → 右键那台带黄色感叹号的 `M5stack`（在其他设备下）
    → 更新驱动程序 → **浏览我的电脑以查找驱动程序** → 指向该目录

（★ 是「浏览我的电脑」，不是「自动搜索」—— 自动搜索只会去 Windows Update 问，那条路是死的。）
"""
import argparse
import json
import os
import re
import shutil
import ssl
import subprocess
import sys
import urllib.parse
import urllib.request

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/124.0 Safari/537.36")
SEARCH = "https://www.catalog.update.microsoft.com/Search.aspx?q=FTDI"
DIALOG = "https://www.catalog.update.microsoft.com/DownloadDialog.aspx"
WANT = "VID_0403&PID_6001"          # FT232R —— K151-R 遥控器里的那颗

CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE


def fetch(url, data=None, extra=None):
    headers = {"User-Agent": UA}
    if extra:
        headers.update(extra)
    req = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=180, context=CTX) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="ftdi-driver",
                    help="解压后的驱动放这里（默认 ./ftdi-driver）")
    args = ap.parse_args()
    out_final = os.path.abspath(args.out)
    work = out_final + ".work"          # 中间产物（每个包的原始解压）

    # ---------- 1. 列 FTDI 条目 ----------
    print("[1/5] 查微软 Update Catalog 里的 FTDI 条目 ...")
    html = fetch(SEARCH).decode("utf-8", "ignore")
    uids = []
    for row in re.findall(r"(?s)<tr[^>]*>(.*?)</tr>", html):
        m = re.search(r'goToDetails\("([0-9a-f\-]{36})"\)', row)
        if not m:
            continue
        cells = [re.sub(r"<[^>]+>", "", c).strip()
                 for c in re.findall(r"(?s)<td[^>]*>(.*?)</td>", row)]
        if "FTDI" in " ".join(cells).upper():
            uids.append(m.group(1))
    if not uids:
        print("   X 没拿到任何条目（网络？）")
        return 1
    print("      拿到 %d 个条目" % len(uids))

    # ---------- 2. 换真实下载链接 ----------
    print("[2/5] 换下载链接 ...")
    payload = [{"size": 0, "languages": "", "uidInfo": u, "updateID": u}
               for u in uids]
    body = urllib.parse.urlencode({
        "updateIDs": json.dumps(payload),
        "updateIDsLength": "0",
    }).encode()
    txt = fetch(DIALOG, data=body, extra={
        "Content-Type": "application/x-www-form-urlencoded",
        "Referer": SEARCH,
    }).decode("utf-8", "ignore")
    urls = []
    for u in re.findall(r"(https?://[^\s\"'<>]+\.(?:cab|msu|exe|zip))", txt):
        if u not in urls:
            urls.append(u)
    print("      %d 个不同的文件" % len(urls))

    # ---------- 3. 下载 ----------
    print("[3/5] 下载 .cab ...")
    os.makedirs(work, exist_ok=True)
    cabs = []
    for i, u in enumerate(urls, 1):
        name = os.path.basename(urllib.parse.urlparse(u).path) or ("pkg%d.cab" % i)
        dst = os.path.join(work, name)
        if not os.path.exists(dst):
            try:
                open(dst, "wb").write(fetch(u))
            except Exception as e:
                print("      ! 跳过（%s）: %s" % (name[:36], e))
                continue
        cabs.append(dst)
        print("      %s  (%d KB)" % (name[:56], os.path.getsize(dst) // 1024))

    # ---------- 4. 解开 + 挑对的包 ----------
    print("[4/5] 解开并检查哪个包认 %s ..." % WANT)
    hit = None
    for i, cab in enumerate(cabs, 1):
        d = os.path.join(work, "pkg_%02d" % i)
        if os.path.isdir(d):
            shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)
        subprocess.run(["expand", cab, "-F:*", d],
                       capture_output=True, text=True, timeout=300)
        ids, matched = set(), False
        for root, _, files in os.walk(d):
            for f in files:
                if not f.lower().endswith(".inf"):
                    continue
                try:
                    t = open(os.path.join(root, f), errors="ignore").read()
                except Exception:
                    continue
                if WANT.lower() in t.lower():
                    matched = True
                ids |= set(re.findall(r"VID_0403&PID_[0-9A-Fa-f]{4}", t))
        print("      pkg_%02d: %s %s" % (i, ",".join(sorted(ids)) or "-",
                                         "  <<== 认你的遥控器" if matched else ""))
        if matched and hit is None:
            hit = d

    # ---------- 5. 只留对的那个 ----------
    if not hit:
        print("[5/5] X 所有包里都没有 %s" % WANT)
        return 1
    if os.path.isdir(out_final):
        shutil.rmtree(out_final, ignore_errors=True)
    shutil.copytree(hit, out_final)
    n_inf = sum(1 for r, _, fs in os.walk(out_final)
                for f in fs if f.lower().endswith(".inf"))
    n_sys = sum(1 for r, _, fs in os.walk(out_final)
                for f in fs if f.lower().endswith(".sys"))
    print("[5/5] OK -> %s" % out_final)
    print("      %d 个 .inf / %d 个 .sys" % (n_inf, n_sys))
    print()
    print("接下来（Windows）：")
    print("  设备管理器 -> 右键带感叹号的 M5stack -> 更新驱动程序")
    print("  -> 浏览我的电脑以查找驱动程序 -> 指向上面这个目录")
    print("  成功标志：端口 (COM 和 LPT) 下出现 USB Serial Port (COMx)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
