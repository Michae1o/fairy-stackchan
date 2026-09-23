#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把本仓库的服务器改动应用到一份【干净的上游 xiaozhi-esp32-server】上。

用法：
    python3 tools/apply_to_server.py <上游服务器目录>
    python3 tools/apply_to_server.py <上游服务器目录> --dry-run

做的事 = INSTALL.md 第 2 节：
    ① 覆盖 server/patches/ 的 4 个文件到 core/api/
    ② 改 core/http_server.py（3 处）—— ★ 没有它，控制台 /admin 会 404
         · import AdminHandler
         · __init__ 里构造 self.admin_handler
         · 启动时 self.admin_handler.register(app)
    ③ 改 core/connection.py（3 处）—— ★ 没有它，皮肤/硬件下发/在线状态全失效
         · import device_registry
         · 连接建立时 register(self)
         · 连接断开时 unregister(self)

★ 幂等：重复执行不会重复插入。
★ 锚点找不到就报错退出（上游改了结构时别静默跳过）。
★ 它不改 data/.config.yaml（含你的 API Key，请自行配置）。
"""
import argparse
import shutil
import sys
from pathlib import Path

PATCHES = Path(__file__).resolve().parents[1] / "server" / "patches"

# ── http_server.py 的三处插入 ────────────────────────────────────────
HS_IMPORT_ANCHOR = "from core.api.vision_handler import VisionHandler\n"
HS_IMPORT_ADD = "from core.api.admin_handler import AdminHandler\n"

HS_INIT_ANCHOR = "        self.vision_handler = VisionHandler(config)\n"
HS_INIT_ADD = """        # Fairy 控制台（Web 管理界面，挂在 /admin）
        try:
            self.admin_handler = AdminHandler(config, self.logger)
        except Exception as e:
            self.admin_handler = None
            self.logger.bind(tag=TAG).warning(
                f"Fairy 控制台初始化失败（不影响其他接口）: {e}")
"""

HS_ROUTE_ANCHOR = "                # 运行服务\n"
HS_ROUTE_ADD = """                # Fairy 控制台（/admin）—— 默认只允许本机访问
                if self.admin_handler is not None:
                    self.admin_handler.register(app)

"""

# ── connection.py 的三处插入 ─────────────────────────────────────────
CN_IMPORT_ANCHOR = "from collections import deque\n"
CN_IMPORT_ADD = "from core.api import device_registry\n"

CN_CONN_ANCHOR = "    async def handle_connection(self, ws: websockets.ServerConnection):\n"
CN_CONN_ADD = """        # ★ 登记到设备注册表（控制台查在线状态用）
        try:
            device_registry.register(self)
            self.logger.bind(tag=TAG).info(
                f"[registry] 设备已登记: {self.session_id}")
        except Exception as _e:
            self.logger.bind(tag=TAG).warning(
                f"[registry] 登记失败: {_e!r}")
"""

CN_FINALLY_ANCHOR = ("        finally:\n"
                     "            try:\n"
                     "                await self._save_and_close(ws)\n")
CN_FINALLY_REPLACE = ("        finally:\n"
                      "            # ★ 从注册表注销\n"
                      "            try:\n"
                      "                device_registry.unregister(self)\n"
                      "                self.logger.bind(tag=TAG).info(\n"
                      "                    f\"[registry] 设备已注销: {self.session_id}\")\n"
                      "            except Exception:\n"
                      "                pass\n"
                      "            try:\n"
                      "                await self._save_and_close(ws)\n")

# connection.py：对话开始时通知控制台（眼睛切 thinking）
CN_THINK_ANCHOR = '            self.dialogue.put(Message(role="user", content=query))\n'
CN_THINK_ADD = """            # ★ 通知 Web 控制台：开始思考（眼睛切 thinking）
            try:
                from core.utils import chat_log
                chat_log.set_state("thinking", "llm")
            except Exception:
                pass
"""

# connection.py：声纹配置【实时读文件】
# 控制台「声纹」页可以随时增删说话人，而 self.config 是进程启动时加载并缓存的
# ⇒ 不实时读的话，用户加完人必须重启服务器。
# 这个函数【每个连接都会走一次】⇒ 改完配置，设备下次对话即生效。
CN_VP_ANCHOR = ('        try:\n'
                '            voiceprint_config = self.config.get("voiceprint", {})\n')
CN_VP_ADD = '''            # ★ 实时读 data/.config.yaml 里的 voiceprint 段（控制台改完不用重启）
            try:
                import yaml as _yaml
                _vp_cfg = os.path.join(
                    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "data", ".config.yaml")
                if os.path.isfile(_vp_cfg):
                    with open(_vp_cfg, encoding="utf-8") as _f:
                        _live = (_yaml.safe_load(_f.read()) or {}).get("voiceprint")
                    if _live:
                        voiceprint_config = _live
            except Exception as _e:
                self.logger.bind(tag=TAG).warning(
                    f"读取实时 voiceprint 配置失败: {_e!r}")
'''


def say(msg=""):
    print(msg, flush=True)


def die(msg):
    say("\n❌ " + msg)
    sys.exit(1)


def edit(path, anchor, payload, marker, dry, label):
    """anchor 后面插入 payload（marker 已存在则跳过）。"""
    if not path.is_file():
        die("找不到文件：" + str(path))
    text = path.read_text(encoding="utf-8")
    if marker in text:
        say("      = 已经改过（%s），跳过" % label)
        return
    if anchor not in text:
        die("锚点没找到 ⇒ %s\n       期望锚点: %r\n       上游结构可能变了，请手工按 INSTALL.md 第 2 节处理。"
            % (label, anchor.strip()[:70]))
    if dry:
        say("      [dry-run] 会改 %s（%s）" % (path.name, label))
        return
    path.write_text(text.replace(anchor, anchor + payload, 1),
                    encoding="utf-8", newline="\n")
    say("      ✅ 已改 %s（%s）" % (path.name, label))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target", help="上游 xiaozhi-esp32-server 目录")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    dry = args.dry_run
    tgt = Path(args.target).expanduser().resolve()
    say("上游服务器目录: " + str(tgt) + ("   [dry-run]" if dry else ""))

    api_dir = tgt / "main" / "xiaozhi-server" / "core" / "api"
    if not api_dir.is_dir():
        die("找不到 main/xiaozhi-server/core/api ⇒ 目录不对（或上游结构变了）")
    say("✅ 目标校验通过")

    # ── ① 覆盖 patch 文件（镜像目录：patches/core/api/x.py → main/xiaozhi-server/core/api/x.py）──
    say("\n【1/3】覆盖 server/patches/ → main/xiaozhi-server/（按目录镜像）")
    srv_root = tgt / "main" / "xiaozhi-server"
    files = sorted(p for p in PATCHES.rglob("*") if p.is_file())
    for f in files:
        rel = f.relative_to(PATCHES)
        if not dry:
            dst = srv_root / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, dst)
        say("      ✅ " + str(rel).replace("\\", "/"))
    say("      （%d 个文件：新增文件 + 覆盖上游的同名文件）" % len(files))

    # ── ② http_server.py ─────────────────────────────────────────
    say("\n【2/3】改 core/http_server.py（不接 ⇒ 控制台 /admin 404）")
    hs = tgt / "main" / "xiaozhi-server" / "core" / "http_server.py"
    edit(hs, HS_IMPORT_ANCHOR, HS_IMPORT_ADD,
         "from core.api.admin_handler import AdminHandler", dry, "import AdminHandler")
    edit(hs, HS_INIT_ANCHOR, HS_INIT_ADD,
         "self.admin_handler = AdminHandler(", dry, "__init__ 构造 admin_handler")
    edit(hs, HS_ROUTE_ANCHOR, HS_ROUTE_ADD,
         "self.admin_handler.register(app)", dry, "启动时注册 /admin 路由")

    # ── ③ connection.py ──────────────────────────────────────────
    say("\n【3/3】改 core/connection.py（不接 ⇒ 皮肤/硬件下发/在线检测失效）")
    cn = tgt / "main" / "xiaozhi-server" / "core" / "connection.py"
    edit(cn, CN_IMPORT_ANCHOR, CN_IMPORT_ADD,
         "from core.api import device_registry", dry, "import device_registry")
    edit(cn, CN_CONN_ANCHOR, CN_CONN_ADD,
         "device_registry.register(self)", dry, "连接建立时 register(self)")
    if not dry:
        text = cn.read_text(encoding="utf-8")
        if "device_registry.unregister" in text:
            say("      = 已经改过（连接断开时 unregister），跳过")
        elif CN_FINALLY_ANCHOR not in text:
            die("锚点没找到 ⇒ 连接断开处 unregister\n"
                "       期望锚点: 'finally: / try: / await self._save_and_close(ws)'\n"
                "       上游结构可能变了，请手工按 INSTALL.md 第 2 节处理。")
        else:
            cn.write_text(text.replace(CN_FINALLY_ANCHOR, CN_FINALLY_REPLACE, 1),
                          encoding="utf-8", newline="\n")
            say("      ✅ 已改 connection.py（连接断开时 unregister(self)）")
    else:
        say("      [dry-run] 会在连接断开处插入 unregister")

    edit(cn, CN_THINK_ANCHOR, CN_THINK_ADD, 'chat_log.set_state("thinking"', dry,
         "对话开始时通知控制台（thinking 状态）")

    edit(cn, CN_VP_ANCHOR, CN_VP_ADD, "读取实时 voiceprint 配置失败", dry,
         "声纹配置实时读文件（控制台加完人不用重启）")

    # ── 自检：接线 + 关键文件是否真的到位 ────────────────────────
    say("\n【自检】核对（下面每一项都必须 ✅，否则功能是坏的）")
    checks = [
        (hs, "from core.api.admin_handler import AdminHandler", "http_server: import 控制台后端"),
        (hs, "self.admin_handler = AdminHandler(", "http_server: 构造 admin_handler"),
        (hs, "self.admin_handler.register(app)", "http_server: 注册 /admin 路由"),
        (cn, "from core.api import device_registry", "connection: import 注册表"),
        (cn, "device_registry.register(self)", "connection: 连接建立时登记"),
        (cn, "device_registry.unregister(self)", "connection: 连接断开时注销"),
        (cn, 'chat_log.set_state("thinking"', "connection: 对话开始通知控制台"),
        (cn, "读取实时 voiceprint 配置失败", "connection: 声纹配置实时读取（免重启）"),
        (srv_root / "core" / "api" / "admin_handler.py", "/admin/api/voiceprint",
         "admin_handler.py（声纹注册/识别接口）"),
        (srv_root / "core" / "utils" / "chat_log.py", "def set_state(", "chat_log.py（控制台对话记录/状态灯）"),
        (srv_root / "core" / "api" / "chat_llm.py", "def chat(", "chat_llm.py（控制台网页对话）"),
        (srv_root / "core" / "api" / "ota_handler.py", "_align_skin", "ota_handler.py（皮肤自动对齐）"),
        (srv_root / "core" / "utils" / "dialogue.py", "_log_chat", "dialogue.py（真实对话落记录）"),
        (srv_root / "core" / "handle" / "sendAudioHandle.py", 'chat_log.set_state("speaking"',
         "sendAudioHandle.py（说话状态）"),
        (srv_root / "core" / "providers" / "tts" / "gpt_sovits_v2.py", "_fairy_postprocess",
         "gpt_sovits_v2.py（Fairy 音色后处理）"),
    ]
    bad = 0
    for path, needle, desc in checks:
        ok = path.is_file() and needle in path.read_text(encoding="utf-8", errors="replace")
        say("    %s %s" % ("✅" if ok else "❌", desc))
        bad += 0 if ok else 1
    if bad and not dry:
        say("\n❌ 有 %d 处没接上 —— 别继续，先把这几处补上。" % bad)
        sys.exit(1)
    if dry:
        say("\n（dry-run：以上是预计结果）")
        return

    say("""
────────────────────────────────────────────────────────────────
  完成。接下来：
    cd <上游服务器目录>/main/xiaozhi-server
    pip install -r requirements.txt      # 首次
    python app.py
    # 浏览器打开 http://<你的IP>:8003/admin   ← 出控制台就说明接线成功（不是 404）
    #            http://<你的IP>:8003/m       ← 手机版
  ★ 别忘了在 data/.config.yaml 里配 auth_key（否则设备拍照识图会失败）
  ★ 控制台只在内网可用（默认放行 127.0.0.1 + 局域网，拒绝公网）
────────────────────────────────────────────────────────────────""")


if __name__ == "__main__":
    main()
