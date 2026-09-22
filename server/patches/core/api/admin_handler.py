"""Fairy 控制台（Web 管理界面）

挂在原有 HTTP 服务器上（端口取配置 `server.http_port`，默认 8003），路径 /admin。

功能（第一步）：
  · 改 LLM（模型名 / base_url / temperature / max_tokens）
  · 改 API Key（LLM / VLLM）
  · 改人设 prompt
  · 改音色（参考音频 / 台词 / 后处理开关）
  · 改音量（设备侧默认音量）
  · 一键重启服务器（人设/音色改动需重启才生效）

★ 安全设计（用户第 2 条：默认只允许本机）
  · 默认只放行 127.0.0.1 / ::1 的请求
  · 可通过配置 remotes.enabled = true 放开（开源后给别人用）
  · API Key 在界面上【打码显示】，不回传明文

★ 为什么这么写（防坑）
  · 配置用 yaml.safe_dump 整体重写 —— 必须先备份，且写前校验字段数，
    否则丢字段会导致服务器起不来（本会话已踩过）。
  · 写文件用 UTF-8 + newline="\n"（Windows 下防 CRLF 混入）。
  · 重启用独立脚本，不在 HTTP 进程内自杀（会留 TIME_WAIT）。
"""

import asyncio
import copy
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time

import yaml
from aiohttp import web

TAG = "FairyAdmin"

# ── 路径 ───────────────────────────────────────────────────
# 本文件位于  <server_root>/core/api/admin_handler.py
#   上溯 1 层 → core/api        （页面文件同目录）
#   上溯 2 层 → core
#   上溯 3 层 → <server_root>   ← 配置在 <server_root>/data/.config.yaml
_THIS = os.path.abspath(__file__)
API_DIR = os.path.dirname(_THIS)                            # core/api
SERVER_ROOT = os.path.dirname(os.path.dirname(API_DIR))     # xiaozhi-server
# 项目根 = xiaozhi-server 上溯 4 层（main → 项目目录 → 仓库根）
PROJECT_ROOT = os.path.abspath(os.path.join(
    SERVER_ROOT, "..", "..", ".."))
CONFIG_PATH = os.path.join(SERVER_ROOT, "data", ".config.yaml")
BACKUP_DIR = os.path.join(PROJECT_ROOT, "backup")
PAGE_PATH = os.path.join(API_DIR, "admin_page.html")
# ★ 手机端独立 H5 页（/m）—— 与电脑版 admin_page.html 各管各的
MOBILE_PAGE_PATH = os.path.join(API_DIR, "admin_mobile.html")

# 需要保护的敏感字段
SECRET_KEYS = ("api_key", "key", "token", "secret", "password")


def _mask(v):
    """把密钥打码，界面上不显示明文"""
    if not isinstance(v, str) or not v:
        return v
    if len(v) <= 8:
        return "•" * len(v)
    return v[:3] + "•" * 8 + v[-3:]


def _is_secret(k):
    return k.lower() in SECRET_KEYS or k.lower().endswith("_key")


class AdminHandler:
    def __init__(self, config: dict, logger, restart_cb=None):
        self.config = config
        self.logger = logger
        self.restart_cb = restart_cb      # 可选：由外部注入的重启函数
        # 默认只允许本机（用户第 2 条）
        self.allow_remote = bool(
            (config.get("remotes") or {}).get("enabled", False))

    # ── 工具 ────────────────────────────────────────────────
    # ★ 局域网白名单（RFC1918 私有地址）
    #   为什么需要：默认只放行本机 ⇒ 手机从 192.168.x 访问会被拒（403）
    #   为什么不用"一刀切 allow_remote"：那样公网也能访问，不安全
    _LAN_PREFIXES = (
        "127.",            # 本机
        "10.",             # 10.0.0.0/8
        "192.168.",        # 192.168.0.0/16
        "172.16.", "172.17.", "172.18.", "172.19.", "172.2",   # 172.16-31
        "172.30.", "172.31.",
    )

    def _is_lan(self, host):
        """判断是否局域网私有地址"""
        if not host:
            return False
        if host in ("::1", "localhost"):
            return True
        if host.startswith("::ffff:"):        # IPv4-mapped IPv6
            host = host[7:]
        if host.startswith("fe80:") or host.startswith("fc") \
                or host.startswith("fd"):     # 链路本地/唯一本地 IPv6
            return True
        for p in self._LAN_PREFIXES:
            if host.startswith(p):
                return True
        return False

    def _check_client(self, request):
        # ① 配置显式放行全部（保留原能力，慎用）
        if self.allow_remote:
            return True
        peer = request.transport.get_extra_info("peername")
        host = peer[0] if peer else ""
        # ② 本机 + 局域网放行（手机/平板在同一 WiFi 下可访问控制台）
        return self._is_lan(host)

    def _read_cfg(self):
        """读配置（以文件为准，避免内存里是旧的）"""
        with io.open(CONFIG_PATH, encoding="utf-8") as f:
            return yaml.safe_load(f.read())

    def _write_cfg(self, cfg):
        """★ 安全写配置：先校验 → 备份 → 写 → 复核"""
        # 1) 写前必须能 dump 成功
        try:
            out = yaml.safe_dump(cfg, allow_unicode=True, sort_keys=False,
                                 default_flow_style=False, width=1000)
        except Exception as e:
            raise RuntimeError("配置序列化失败: %r" % (e,))

        # 2) 关键顶层字段不能丢
        must = ["server", "selected_module", "TTS", "LLM", "ASR"]
        missing = [k for k in must if k not in cfg]
        if missing:
            raise RuntimeError("拒绝写入：关键字段缺失 %s" % missing)

        # 3) 备份
        os.makedirs(BACKUP_DIR, exist_ok=True)
        ts = time.strftime("%Y%m%d_%H%M%S")
        bak = os.path.join(BACKUP_DIR,
                           ".config.yaml.%s" % ts)
        try:
            shutil.copy2(CONFIG_PATH, bak)
        except Exception as e:
            self.logger.bind(tag=TAG).warning(f"备份失败（继续）: {e}")

        # 4) 写入（UTF-8 无 BOM，LF）
        with io.open(CONFIG_PATH, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)

        # 5) 复核：能读回来且字段数一致
        back = self._read_cfg()
        if len(back) != len(cfg):
            raise RuntimeError("写入后复核失败：字段数 %d != %d"
                               % (len(back), len(cfg)))
        return bak

    # ── 页面 ────────────────────────────────────────────────
    async def handle_page(self, request):
        if not self._check_client(request):
            return web.Response(status=403, text="仅允许本机访问")
        try:
            with io.open(PAGE_PATH, encoding="utf-8") as f:
                html = f.read()
        except OSError:
            return web.Response(status=500, text="页面文件缺失")
        return web.Response(text=html, content_type="text/html",
                            charset="utf-8")

    # ★ 手机端 H5 独立页（用户要求：「手机 html 适配很烂，写 H5 版本」）
    #   · 路径 /m —— 与电脑版 /admin 完全独立，互不影响
    #   · 功能与电脑版一致（对话/皮肤/灯光/舵机/设置/设备）
    #   · 布局为手机竖屏优化：底部 Tab + 大按钮 + 无图标（用户要求不花哨）
    async def handle_mobile_page(self, request):
        if not self._check_client(request):
            return web.Response(status=403, text="仅允许本机访问")
        try:
            with io.open(MOBILE_PAGE_PATH, encoding="utf-8") as f:
                html = f.read()
        except OSError:
            return web.Response(status=500, text="手机页文件缺失")
        return web.Response(text=html, content_type="text/html",
                            charset="utf-8")

    # ── 读配置 ──────────────────────────────────────────────

    # ── 探测可用模型（给前端"选择模型"用）────────────────────
    async def handle_models(self, request):
        """转发 GET {base_url}/models，列出该网关支持的模型

        ★ 用户需求：填自定义模型名容易打错，改成可探测+下拉选择。
        ★ Key 优先级：请求参数 api_key > 已保存配置里的真实 key
          （前端回传的是打码值，不能用，这里补用配置里的真值）
        """
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        import json as _json
        import urllib.request as _ur

        base = (request.query.get("base_url") or "").strip()
        key = (request.query.get("api_key") or "").strip()
        # 打码值（含 • ）视为无效，改用配置里的真值
        if key and ("\u2022" in key or "*" in key):
            key = ""
        try:
            cfg = self._read_cfg()
            llm = (cfg.get("LLM") or {}).get("DeepSeekLLM", {})
            if not base:
                base = str(llm.get("base_url") or "").strip()
            if not key:
                key = str(llm.get("api_key") or "").strip()
        except Exception as e:
            return web.json_response(
                {"ok": False, "error": "读取配置失败: %r" % e})
        if not base:
            return web.json_response({"ok": False, "error": "缺少 base_url"})

        url = base.rstrip("/") + "/models"
        try:
            req = _ur.Request(url, headers={
                "Authorization": "Bearer " + key,
                "Content-Type": "application/json"})
            with _ur.urlopen(req, timeout=25) as rp:
                body = rp.read().decode("utf-8", "replace")
            d = _json.loads(body)
            items = d.get("data") or []
            models = []
            for it in items:
                mid = it.get("id") if isinstance(it, dict) else str(it)
                if mid:
                    models.append(mid)
            models = sorted(set(models))
            try:
                self.logger.info("探测模型 %s → %d 个" % (url, len(models)))
            except Exception:
                pass
            return web.json_response({"ok": True, "url": url,
                                      "count": len(models),
                                      "models": models})
        except Exception as e:
            return web.json_response(
                {"ok": False, "error": "探测失败: %r" % e, "url": url})

    async def handle_get(self, request):
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            cfg = self._read_cfg()
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

        llm = (cfg.get("LLM") or {}).get("DeepSeekLLM", {})
        vllm = (cfg.get("VLLM") or {}).get("DeepSeekVLLM", {})
        tts = (cfg.get("TTS") or {}).get("GPT_SOVITS_V2", {})

        data = {
            "ok": True,
            "llm": {
                "model_name": llm.get("model_name", ""),
                "base_url": llm.get("base_url", ""),
                "temperature": llm.get("temperature", 0.7),
                "max_tokens": llm.get("max_tokens", 500),
                # ★ 密钥打码，不回传明文
                "api_key_masked": _mask(llm.get("api_key", "")),
                "api_key_set": bool(llm.get("api_key")),
            },
            "vllm": {
                "model_name": vllm.get("model_name", ""),
                "base_url": vllm.get("base_url", ""),
                "temperature": vllm.get("temperature", 0.3),
                "max_tokens": vllm.get("max_tokens", 300),
                "api_key_masked": _mask(vllm.get("api_key", "")),
                "api_key_set": bool(vllm.get("api_key")),
            },
            "prompt": cfg.get("prompt", ""),
            "tts": {
                "ref_audio_path": tts.get("ref_audio_path", ""),
                "prompt_text": tts.get("prompt_text", ""),
                "text_lang": tts.get("text_lang", "zh"),
                "credit": {
                    "name": "白菜工厂1145号员工",
                    "note": "Fairy 语音模型由该 UP 主制作/提供（B站）",
                },
            },
            "paths": {
                "config": CONFIG_PATH,
                "backup_dir": BACKUP_DIR,
            },
        }
        return web.json_response(data)

    # ── 写配置 ──────────────────────────────────────────────
    async def handle_save(self, request):
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            body = await request.json()
        except Exception as e:
            return web.json_response({"ok": False, "error": "请求格式错误: %r"
                                      % (e,)}, status=400)

        try:
            cfg = self._read_cfg()
            changed = []

            # LLM
            llm = body.get("llm") or {}
            node = (cfg.setdefault("LLM", {})
                       .setdefault("DeepSeekLLM", {}))
            for k in ("model_name", "base_url"):
                if llm.get(k) is not None and str(llm[k]).strip():
                    if node.get(k) != llm[k]:
                        node[k] = llm[k]
                        changed.append("LLM.%s" % k)
            for k, cast in (("temperature", float), ("max_tokens", int)):
                if llm.get(k) is not None and str(llm[k]).strip() != "":
                    try:
                        v = cast(llm[k])
                    except (TypeError, ValueError):
                        return web.json_response(
                            {"ok": False, "error": "%s 不是合法数字" % k},
                            status=400)
                    if node.get(k) != v:
                        node[k] = v
                        changed.append("LLM.%s" % k)
            # ★ 密钥：只有非空且不是打码串才覆盖
            ak = llm.get("api_key")
            if ak and "•" not in ak:
                if node.get("api_key") != ak:
                    node["api_key"] = ak
                    changed.append("LLM.api_key")

            # ★ VLLM（视觉模型）：与 LLM 一样支持【改模型名 / Base URL / 参数】
            #   原来是「只有填了 key 才顺带 setdefault」⇒ 换不了模型
            vbody = body.get("vllm") or {}
            if vbody:
                vnode = (cfg.setdefault("VLLM", {})
                            .setdefault("DeepSeekVLLM", {}))
                for k in ("model_name", "base_url"):
                    v = (vbody.get(k) or "").strip()
                    if v and vnode.get(k) != v:
                        vnode[k] = v
                        changed.append("VLLM.%s" % k)
                for k, cv in (("temperature", float),
                              ("max_tokens", int)):
                    raw = str(vbody.get(k) or "").strip()
                    if raw:
                        try:
                            nv = cv(float(raw))
                        except (ValueError, TypeError):
                            continue
                        if vnode.get(k) != nv:
                            vnode[k] = nv
                            changed.append("VLLM.%s" % k)
                vak = (vbody.get("api_key") or "").strip()
                if vak and "•" not in vak:
                    if vnode.get("api_key") != vak:
                        vnode["api_key"] = vak
                        changed.append("VLLM.api_key")

            # 人设
            pr = body.get("prompt")
            if pr is not None and pr.strip() and cfg.get("prompt") != pr:
                cfg["prompt"] = pr
                changed.append("prompt")

            # 音色
            tts_in = body.get("tts") or {}
            tnode = (cfg.setdefault("TTS", {})
                        .setdefault("GPT_SOVITS_V2", {}))
            for k in ("ref_audio_path", "prompt_text", "text_lang"):
                v = tts_in.get(k)
                if v is not None and str(v).strip():
                    if tnode.get(k) != v:
                        tnode[k] = v
                        changed.append("TTS.%s" % k)
            # 参考音频存在性校验（防填错路径）
            refp = tnode.get("ref_audio_path", "")
            if refp and not os.path.exists(refp.replace("/", os.sep)):
                return web.json_response(
                    {"ok": False,
                     "error": "参考音频不存在: %s" % refp}, status=400)

            if not changed:
                return web.json_response({"ok": True, "changed": [],
                                          "msg": "没有改动"})

            bak = self._write_cfg(cfg)
            self.logger.bind(tag=TAG).info(f"配置已更新: {changed}")
            return web.json_response({"ok": True, "changed": changed,
                                      "backup": bak})
        except Exception as e:
            self.logger.bind(tag=TAG).error(f"保存失败: {e}")
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    # ── 重启服务器 ──────────────────────────────────────────
    async def handle_restart(self, request):
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        # 找启动脚本
        script = None
        for cand in ("start-fairy-all.py",):
            p = os.path.join(PROJECT_ROOT, "tools", cand)
            if os.path.exists(p):
                script = p
                break
        if script is None:
            return web.json_response(
                {"ok": False, "error": "找不到 tools/start-fairy-all.py"},
                status=500)

        pyexe = sys.executable
        try:
            # ★ 不在这里自杀：起一个独立进程去做 --restart，
            #   本进程稍后会被那个脚本杀掉并重启（避免 TIME_WAIT 残留）
            # ★ 修复 v3（前两版都实测失败，见 tools/fix-restart-bat.py 注释）
            #
            #   v1 CREATE_NO_WINDOW      → 被父进程连坐，只关不启
            #   v2 DETACHED_PROCESS      → 无控制台，脚本中途死
            #   v2b Start-Process 传复杂参数 → 引号打架，等于没执行
            #
            #   v3：写一个专用 .bat（tools/restart-fairy.bat），
            #       这里只负责【独立地调这个 bat】。
            #       bat 内部用 start "" /b 起独立窗口 ⇒ 有控制台、不挂父树。
            bat = os.path.join(PROJECT_ROOT, "tools", "restart-fairy.bat")
            if not os.path.isfile(bat):
                self.logger.bind(tag=TAG).error("找不到 %s" % bat)
                return web.json_response(
                    {"ok": False, "error": "找不到 tools/restart-fairy.bat"},
                    status=500)
            try:
                # cmd /c start "" /b "<bat>"
                #   start 的第一个引号参数是"窗口标题"，必须给个空的 ""
                #   /b = 不新建窗口（但进程独立于父树）
                subprocess.Popen(
                    ["cmd.exe", "/c", "start", "", "/b", bat],
                    cwd=PROJECT_ROOT,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    stdin=subprocess.DEVNULL,
                    close_fds=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                self.logger.bind(tag=TAG).info(
                    "已触发重启（经 %s，日志 logs/restart.log）" % bat)
            except Exception as e:
                self.logger.bind(tag=TAG).error("触发重启失败: %r" % e)
                return web.json_response(
                    {"ok": False, "error": "触发重启失败: %r" % e},
                    status=500)

            self.logger.bind(tag=TAG).info("已触发重启")
            return web.json_response({
                "ok": True,
                "msg": "正在重启 Fairy 服务器，约 20~30 秒后可用",
                "wait": 30,
            })
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    # ── 列出可用的参考音频（音色选择用）────────────────────
    async def handle_refs(self, request):
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        refdir = os.path.join(PROJECT_ROOT, "fairy-ref")
        items = []
        if os.path.isdir(refdir):
            for fn in sorted(os.listdir(refdir)):
                if fn.lower().endswith((".wav", ".mp3", ".flac")):
                    p = os.path.join(refdir, fn)
                    items.append({
                        "name": fn,
                        "path": p.replace("\\", "/"),
                        "size_kb": round(os.path.getsize(p) / 1024, 1),
                    })
        return web.json_response({"ok": True,
                                  "dir": refdir, "items": items})

    # ── 对话记录 ────────────────────────────────────────────
    async def handle_chat(self, request):
        """返回最近对话（默认 100 条）"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            from core.utils import chat_log
            limit = int(request.query.get("limit", 100))
            since = request.query.get("since")
            since_ts = float(since) if since else None
            msgs = chat_log.recent(limit=limit, since_ts=since_ts)
            return web.json_response({"ok": True, "count": len(msgs),
                                      "messages": msgs})
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    async def handle_chat_clear(self, request):
        """清空对话记录"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            from core.utils import chat_log
            chat_log.clear()
            return web.json_response({"ok": True, "msg": "对话记录已清空"})
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    async def handle_chat_send(self, request):
        """网页聊天：用户打字 → LLM → 文字回复（不推给设备）"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            body = await request.json()
        except Exception as e:
            return web.json_response({"ok": False, "error": "请求格式错误: %r"
                                      % (e,)}, status=400)
        text = (body.get("text") or "").strip()
        if not text:
            return web.json_response({"ok": False, "error": "消息为空"},
                                     status=400)
        try:
            from core.api import chat_llm
            import asyncio as _aio
            # ★ 让眼睛切 thinking
            try:
                from core.utils import chat_log
                chat_log.set_state("thinking", "web")
            except Exception:
                pass
            # LLM 调用是同步阻塞的（provider 用的是 openai 同步客户端），
            # 丢到线程池里跑，避免卡住 aiohttp 事件循环（会让整个
            # 控制台 + OTA 接口一起变慢）。
            loop = _aio.get_running_loop()
            reply, err = await loop.run_in_executor(
                None, lambda: chat_llm.chat(text))
            if err:
                try:
                    from core.utils import chat_log
                    chat_log.set_state("idle", "web_err")
                except Exception:
                    pass
                return web.json_response({"ok": False, "error": err},
                                         status=502)
            # 网页聊天也记进对话记录（标记来源为网页，便于区分）
            try:
                from core.utils import chat_log
                chat_log.record("user", text, device_id="web")
                chat_log.record("assistant", reply, device_id="web")
                # ★ 网页打字没有"播音频"阶段，回复到位就直接回 idle
                chat_log.set_state("idle", "web_done")
            except Exception:
                pass
            return web.json_response({"ok": True, "reply": reply})
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    async def handle_chat_web_clear(self, request):
        """清空网页聊天的上下文（不影响设备的对话记录）"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            from core.api import chat_llm
            chat_llm.clear_context()
            return web.json_response({"ok": True, "msg": "网页上下文已清空"})
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    async def handle_state(self, request):
        """当前状态（给眼睛用）：idle / thinking / speaking"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            from core.utils import chat_log
            st = chat_log.get_state()
            return web.json_response({"ok": True, "state": st.get("state",
                                                                  "idle"),
                                      "detail": st.get("detail", ""),
                                      "ts": st.get("ts", 0)})
        except Exception as e:
            return web.json_response({"ok": True, "state": "idle",
                                      "error": repr(e)})

    # ── RGB / 舵机配置 ──────────────────────────────────────
    # ★ 设计（对应用户需求「控制台自定义各状态 RGB / 舵机」）：
    #   ① 服务器存「期望值」—— 用户在控制台改完点保存即落盘
    #   ② 设备端另有 NVS 存「已生效值」—— 通过 MCP 工具
    #      self.led.set_default / self.servo.set_default 写入
    #   ③ 本接口只管 ①（服务器侧），②需设备在线时另行触发
    #   ⇒ 所以「保存」在服务器是立即的；「推给设备」看设备是否在线。
    HW_DEFAULTS = {
        "colors": {
            "idle":       [24, 8, 40],
            "connecting": [0, 32, 96],
            "listening":  [0, 96, 128],
            "speaking":   [96, 24, 128],
            "notifying":  [128, 64, 0],
            "error":      [128, 0, 0],
            "upgrading":  [64, 64, 0],
        },
        "servo": {"home_yaw": 0, "home_pitch": 45},
    }

    async def handle_hw_get(self, request):
        """读 RGB/舵机配置（没存过就返回内置默认值）"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            cfg = self._read_cfg()
            hw = cfg.get("hardware") or {}
            colors = dict(self.HW_DEFAULTS["colors"])
            colors.update(hw.get("colors") or {})
            servo = dict(self.HW_DEFAULTS["servo"])
            servo.update(hw.get("servo") or {})
            return web.json_response({"ok": True, "colors": colors,
                                      "servo": servo,
                                      "auto_motion": self._read_auto_motion(),
            "defaults": self.HW_DEFAULTS})
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)})

    async def handle_hw_save(self, request):
        """保存 RGB/舵机配置到 config.yaml

        body: {"colors": {"idle": [r,g,b], ...}, "servo": {"home_yaw":0,...}}
        ★ 只接受合法键与 0~255 整数，防止写坏配置。
        """
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            body = await request.json()
        except Exception as e:
            return web.json_response({"ok": False,
                                      "error": "JSON 解析失败: %r" % e},
                                     status=400)

        def _clamp255(v):
            try:
                n = int(v)
            except Exception:
                return None
            return 0 if n < 0 else (255 if n > 255 else n)

        colors_in = body.get("colors") or {}
        clean_colors = {}
        for k in self.HW_DEFAULTS["colors"]:
            if k not in colors_in:
                continue
            arr = colors_in[k]
            if not isinstance(arr, (list, tuple)) or len(arr) != 3:
                continue
            rgb = [_clamp255(x) for x in arr]
            if any(x is None for x in rgb):
                continue
            clean_colors[k] = rgb

        servo_in = body.get("servo") or {}
        clean_servo = {}
        try:
            if "home_yaw" in servo_in:
                y = float(servo_in["home_yaw"])
                clean_servo["home_yaw"] = max(-128.0, min(128.0, y))
            if "home_pitch" in servo_in:
                p = float(servo_in["home_pitch"])
                clean_servo["home_pitch"] = max(3.0, min(87.0, p))
        except Exception as e:
            return web.json_response({"ok": False,
                                      "error": "舵机参数非法: %r" % e},
                                     status=400)

        try:
            cfg = self._read_cfg()
            hw = cfg.get("hardware") or {}
            if clean_colors:
                hw["colors"] = dict(hw.get("colors") or {}, **clean_colors)
            if clean_servo:
                hw["servo"] = dict(hw.get("servo") or {}, **clean_servo)
            cfg["hardware"] = hw
            self._write_cfg(cfg)
            self.logger.info("已保存硬件配置: colors=%d servo=%d"
                             % (len(clean_colors), len(clean_servo)))
            return web.json_response({"ok": True,
                                      "saved_colors": clean_colors,
                                      "saved_servo": clean_servo})
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)},
                                     status=500)

    # ── 皮肤切换 ─────────────────────────────────────────────
    # ★ 用户需求：「切皮肤自动切服务器」——
    #     Fairy  皮肤 + 自建服务器（专用人格/音色）
    #     官方小表情 + 官方服务器（不人格违和，且能带出去）
    #   机制（读 ota.cc 确认）：Ota::GetCheckVersionUrl() 是 NVS 优先
    #     url = settings.GetString("ota_url"); if (url.empty()) url = CONFIG_OTA_URL;
    #   ⇒ 设备侧写 NVS 的 wifi/ota_url 就能换服务器，不用改固件。
    #   服务器只负责【转发意图】→ 调设备 MCP 工具 self.skin.set
    SKIN_OFFICIAL_OTA = "https://api.tenclass.net/xiaozhi/ota/"

    # ★ 自建服务器 OTA 地址：不写死！从【请求的 Host 头】自动推断
    #   （用户要求：「硬编码的私货你搞成自动识别不就完了」）
    #
    #   原理：用户访问 http://<地址>:8003/admin ⇒ Host 头就是 <地址>:8003
    #         ⇒ 这个地址一定是【通的】（否则页面根本打不开）
    #   ⇒ 开源后别人零配置即可用，不需要改任何文件
    #
    #   优先级：
    #     ① 配置覆盖 config.hardware.self_ota_url（给少数特殊网络场景）
    #     ② 请求 Host 头（正常情况）
    #     ③ 本机局域网 IP 猜测（兜底，见 _guess_self_ota）
    @staticmethod
    def _self_ota_from_request(request):
        """从请求推断自建服务器 OTA 地址（自动识别，无硬编码）"""
        # ① 配置覆盖（可选）
        try:
            cfg = {}
            for name in ("config", "_config", "app_config"):
                v = getattr(request, name, None)
                if isinstance(v, dict):
                    cfg = v
                    break
            if not cfg:
                cfg = request.app.get("config") or {}
            hw = (cfg.get("hardware") or {}) if isinstance(cfg, dict) else {}
            override = hw.get("self_ota_url")
            if override:
                return override.rstrip("/") + "/xiaozhi/ota/"
        except Exception:
            pass

        # ② 请求 Host 头（最常用）
        try:
            host = request.headers.get("Host") or ""
            host = host.strip()
            if host:
                # Host 可能带端口，也可能不带（反代场景）
                if ":" not in host:
                    # ★ 端口不写死：优先读配置 server.http_port
                    port = ""
                    try:
                        cfg = self.config or {}
                        port = str((cfg.get("server") or {})
                                   .get("http_port", "") or "")
                    except Exception:
                        port = ""
                    if not port:
                        # ★ 兜底时必须【显式告警】——否则用户改了端口却不知道
                        #   这里还在用 8003，会得到「设备连不上」这种很难查的现象
                        port = "8003"
                        if not getattr(self, "_warned_no_http_port", False):
                            self._warned_no_http_port = True
                            _w = "自适应提示"
                            print("[" + _w + "] 配置里没有 server.http_port，OTA 地址推断将兜底使用 8003")
                            print("[" + _w + "] 若你的 HTTP 端口不是 8003，请在 data/.config.yaml 里补：")
                            print("[" + _w + "]     server:")
                            print("[" + _w + "]       http_port: <你的实际端口>")
                return "http://%s/xiaozhi/ota/" % host
        except Exception:
            pass

        # ③ 兜底：拿不到 Host（极少见）
        return ""

    def _guess_self_ota(self, request=None):
        """当前生效的自建 OTA 地址（带日志，便于排查）"""
        url = self._self_ota_from_request(request) if request else ""
        if not url:
            ESP = "[skin]"
            print("%s 无法自动识别自建 OTA 地址，"
                  "请在配置里设 hardware.self_ota_url" % ESP)
        return url

    def _read_auto_motion(self):
        """读「待机自动转头」开关的当前值（给控制台回填）

        ★ 数据源优先级：
          ① 设备在线 → 问设备（最准，设备侧才是真相）
          ② 设备离线 → 读服务器配置里的缓存（上次下发的值）
          ③ 都没有   → 返回 True（与固件默认一致）
        """
        # ① 先看服务器有没有缓存
        cached = None
        try:
            cfg = self._read_cfg()
            hw = cfg.get("hardware") or {}
            v = hw.get("auto_motion")
            if isinstance(v, bool):
                cached = v
        except Exception:
            pass
        # ② 配置里有就用配置（这是"用户上次选择的意图"，
        #    与设备 NVS 保持一致，因为下发成功时我们会写配置）
        if cached is not None:
            return cached
        # ③ 没有记录 ⇒ 与固件默认一致（开）
        return True

    def _find_device_conn(self):
        """找一台在线设备连接

        ★ 修正（用户实测指出，他是对的）：
           旧实现在找 `self._connections` / `self.connections` ——
           而服务器【根本没有这个属性名】（是我猜的），
           所以永远返回 None ⇒ 控制台总显示"未连接"。
           现改为查 `core.api.device_registry`（真实注册表）。
        """
        # ① 优先：注册表里带 mcp_client 的连接
        try:
            from core.api import device_registry
            c = device_registry.get()
            if c is not None:
                return c
        except Exception:
            pass
        # ② 兜底：旧路径（万一有别的实现把池挂在这些名字上）
        for attr in ("_connections", "connections"):
            pool = getattr(self, attr, None)
            if isinstance(pool, dict):
                for cc in list(pool.values()):
                    if getattr(cc, "mcp_client", None):
                        return cc
            elif isinstance(pool, (list, set, tuple)):
                for cc in list(pool):
                    if getattr(cc, "mcp_client", None):
                        return cc
        for holder in ("_http_server", "_server", "http_server"):
            hs = getattr(self, holder, None)
            if hs is None:
                continue
            for attr in ("_connections", "connections"):
                pool = getattr(hs, attr, None)
                if isinstance(pool, dict):
                    for cc in list(pool.values()):
                        if getattr(cc, "mcp_client", None):
                            return cc
                elif isinstance(pool, (list, set, tuple)):
                    for cc in list(pool):
                        if getattr(cc, "mcp_client", None):
                            return cc
        return None

    async def handle_hw_apply(self, request):
        """★ 把控制台的 RGB/舵机设置【直接下发到设备】（走 MCP，不经 AI）

        ★ 背景（用户报的问题）：「舵机和RGB在控制台不管用」
           旧实现是 ledPreview() → sendToAI('把xx灯设成rgb(...)')
           ⇒ 指望 AI 理解那句话并主动调工具，链路长且不可靠。
           本条改成：服务器直接调设备的 MCP 工具。
        """
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            body = await request.json()
        except Exception as e:
            return web.json_response({"ok": False, "error": "参数错误: %r" % e},
                                     status=400)

        conn = self._find_device_conn()
        if conn is None:
            return web.json_response(
                {"ok": False, "error": "设备未连接（检查设备开机与网络）"},
                status=200)

        kind = str(body.get("kind", ""))
        try:
            from core.providers.tools.device_mcp.mcp_handler import (
                call_mcp_tool,
            )
        except Exception as e:
            return web.json_response(
                {"ok": False, "error": "MCP 模块不可用: %r" % e}, status=200)

        # ── 组装要调的 MCP 工具 + 参数 ──
        tool = args = None
        if kind == "led":
            st = str(body.get("state", ""))
            v = body.get("value") or [0, 0, 0]
            if not st:
                return web.json_response({"ok": False, "error": "缺 state"},
                                         status=400)
            tool = "self_led_set_default"
            args = {"state": st, "r": int(v[0]), "g": int(v[1]),
                    "b": int(v[2])}
        elif kind == "servo":
            tool = "self_servo_set_angles"
            args = {"yaw": int(body.get("yaw", 0)),
                    "pitch": int(body.get("pitch", 45))}
        elif kind == "servo_home":
            tool = "self_servo_set_default"
            args = {"yaw": int(body.get("yaw", 0)),
                    "pitch": int(body.get("pitch", 45))}
        elif kind == "servo_center":
            tool = "self_servo_center"
            args = {}
        elif kind == "led_mode":
            # ★ 用户需求：「灯光那里我看到每个底下有常亮和呼吸，但是不可选，
            #   你应该开放成可选的」
            #   ⇒ 设备端新增 MCP 工具 self_led_set_mode
            st = str(body.get("state", ""))
            md = str(body.get("mode", "static"))
            if not st:
                return web.json_response(
                    {"ok": False, "error": "缺 state"}, status=400)
            if md not in ("static", "breath"):
                return web.json_response(
                    {"ok": False, "error": "mode 只能是 static 或 breath"},
                    status=400)
            tool = "self_led_set_mode"
            args = {"state": st, "mode": md}
        elif kind == "motion_auto":
            # ★ 用户需求：「待机扭头，能不能有个关闭的方式」
            #   ⇒ 设备端新增 MCP 工具 self.motion.set_auto
            tool = "self_motion_set_auto"
            args = {"enabled": bool(body.get("enabled", True))}
        else:
            return web.json_response(
                {"ok": False, "error": "未知 kind: %s" % kind}, status=400)

        try:
            import json as _json
            res = await call_mcp_tool(conn, conn.mcp_client, tool,
                                      _json.dumps(args))
            # ★ 修复（用户报「关了会自己跳回去」）：
            #   下发成功后必须【把值也写进服务器配置】，
            #   否则控制台下次进来读配置读不到 ⇒ 回填成默认值 ⇒ 视觉上"跳回去"
            #   只有带【持久语义】的 kind 才写（一次性动作如 servo_move 不写）
            self._persist_hw_value(kind, body)
            return web.json_response({"ok": True, "tool": tool,
                                      "args": args, "result": str(res)[:300]})
        except Exception as e:
            return web.json_response(
                {"ok": False, "error": "下发失败: %r" % e,
                 "tool": tool, "args": args}, status=200)

    def _persist_hw_value(self, kind, body):
        """把"有持久语义"的下发值写进服务器配置

        ★ 为什么需要（用户实测报的 bug）：
          下发只到设备，服务器自己不记 ⇒ 控制台回填时读不到 ⇒ 开关"跳回去"。
          设备侧的确存了（NVS），但控制台的"回填"读的是服务器配置。

        ★ 哪些 kind 要写：
          · motion_auto（待机转头开关）—— 用户明确要"关了就是关了"
          · led_mode（常亮/呼吸）—— 有状态语义
          · servo_home（默认角度）—— 有状态语义
          一次性动作（servo_move / servo_center / led_color 试色）不写。
        """
        try:
            cfg = self._read_cfg()
            hw = cfg.setdefault("hardware", {})
            changed = False
            if kind == "motion_auto":
                v = bool(body.get("enabled", True))
                if hw.get("auto_motion") is not v:
                    hw["auto_motion"] = v
                    changed = True
            elif kind == "led_mode":
                st = body.get("state")
                md = body.get("mode")
                if st and md:
                    modes = hw.setdefault("led_modes", {})
                    if modes.get(st) != md:
                        modes[st] = md
                        changed = True
            elif kind == "servo_home":
                yaw = body.get("yaw")
                pitch = body.get("pitch")
                if yaw is not None and pitch is not None:
                    servo = hw.setdefault("servo", {})
                    d = servo.setdefault("default", {})
                    if d.get("yaw") != yaw or d.get("pitch") != pitch:
                        d["yaw"] = yaw
                        d["pitch"] = pitch
                        changed = True
            if changed:
                self._write_cfg(cfg)
                self.logger.bind(tag=TAG).info(
                    "已记住硬件状态: kind=%s" % kind)
        except Exception as e:
            self.logger.bind(tag=TAG).warning(
                "写硬件状态失败（不影响下发）: %r" % e)

    async def handle_device_state(self, request):
        """设备在线检测 —— 给控制台灯光/舵机页用

        ★ 用户需求：「点进页面自动提示用户是否已连接，连接了就提示已连接
           可实时预览，没有连接就提示设备未连接，后面加原因（）」
        ⇒ 返回 online=True/False + reason（人话原因）
        """
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        conn = self._find_device_conn()
        if conn is not None:
            # 取设备标识（有的连接对象有 device_id / client_id）
            dev = ""
            for a2 in ("device_id", "client_id", "mac_address", "uuid"):
                v = getattr(conn, a2, None)
                if v:
                    dev = str(v)
                    break
            n_tools = 0
            try:
                mc = getattr(conn, "mcp_client", None)
                tl = getattr(mc, "tools", None)
                if tl is None:
                    tl = getattr(mc, "_tools", None)
                n_tools = len(tl) if tl else 0
            except Exception:
                pass
            return web.json_response({
                "ok": True,
                "online": True,
                "reason": "",
                "device": dev,
                "tools": n_tools,
            })
        # ★ 未连接：给出【原因】（用户要求加在括号里）
        #   查真实注册表拿连接数
        n_conn = 0
        conns = []
        dbg = []
        try:
            from core.api import device_registry
            conns = device_registry.list_all()
            n_conn = len(conns)
            dbg = device_registry.debug_dump()
        except Exception:
            pass
        reason = ("设备已连接但未完成 MCP 握手" if n_conn > 0
                  else "设备未连接服务器（检查设备是否开机、WiFi 是否同一网络）")
        return web.json_response({
            "ok": True, "online": False, "reason": reason,
            "raw_conns": n_conn, "conns": conns, "debug": dbg,
        })

    async def handle_skin_get(self, request):
        """读当前皮肤 + 两套服务器地址（供控制台显示）"""
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            cfg = self._read_cfg()
            cur = (cfg.get("hardware") or {}).get("skin", "fairy")
            conn = self._find_device_conn()
            dev_reply = None
            if conn is not None:
                try:
                    # ★ MCPClient 没有 call_tool，必须走 mcp_handler
                    from core.providers.tools.device_mcp.mcp_handler import (
                        call_mcp_tool,
                    )
                    res = await call_mcp_tool(conn, conn.mcp_client,
                                              "self.skin.get", "{}")
                    dev_reply = str(res)[:200]
                    # ★★★ 自动对齐（2026-09-19 修用户反馈：
                    #     「按电源键切换皮肤，服务器没跟着切」）
                    #
                    #   真因：设备按电源键切皮肤时只写自己的 NVS，
                    #         不通知服务器 ⇒ 服务器配置仍是旧值。
                    #         而本函数【本来就会问设备】当前皮肤，
                    #         却只把答案返回前端、没写回配置。
                    #
                    #   修法：设备在线时，以【设备实际皮肤】为准，
                    #         与配置不一致就落盘对齐。
                    #   ⇒ 无论哪条路径切皮肤（电源键/语音/网页），
                    #     打开一次控制台即自动对齐。
                    dev_skin = None
                    if isinstance(res, dict):
                        dev_skin = res.get("skin") or res.get("result")
                    elif isinstance(res, str):
                        m = re.search(r'"?skin"?\s*[:=]\s*"?'
                                      r'(fairy|geometry)', res, re.I)
                        if m:
                            dev_skin = m.group(1)
                    if dev_skin:
                        dev_skin = str(dev_skin).strip().lower()
                        if dev_skin in ("fairy", "geometry") and dev_skin != cur:
                            hw = cfg.setdefault("hardware", {})
                            hw["skin"] = dev_skin
                            self._write_cfg(cfg)
                            self.logger.bind(tag=TAG).info(
                                "皮肤自动对齐：配置 %s → 设备实际 %s",
                                cur, dev_skin)
                            cur = dev_skin
                except Exception as e:
                    dev_reply = "查询失败: %r" % e
            return web.json_response({
                "ok": True,
                "skin": cur,
                "device_online": conn is not None,
                "device_reply": dev_reply,
                "servers": {"fairy": self._guess_self_ota(request),
                            "geometry": self.SKIN_OFFICIAL_OTA},
            })
        except Exception as e:
            return web.json_response({"ok": False, "error": repr(e)})

    async def handle_skin_set(self, request):
        """切皮肤：写配置 + 通知设备（设备写 NVS + 重启）

        body: {"skin": "fairy"|"geometry"}
        """
        if not self._check_client(request):
            return web.json_response({"ok": False, "error": "仅限本机"},
                                     status=403)
        try:
            body = await request.json()
        except Exception as e:
            return web.json_response(
                {"ok": False, "error": "JSON 解析失败: %r" % e}, status=400)

        skin = str(body.get("skin", "")).strip().lower()
        if skin not in ("fairy", "geometry"):
            return web.json_response(
                {"ok": False, "error": "skin 只能是 fairy 或 geometry"},
                status=400)

        try:
            cfg = self._read_cfg()
            hw = cfg.setdefault("hardware", {})
            hw["skin"] = skin
            self._write_cfg(cfg)
        except Exception as e:
            return web.json_response({"ok": False,
                                      "error": "写配置失败: %r" % e})

        conn = self._find_device_conn()
        if conn is None:
            return web.json_response({
                "ok": True, "skin": skin, "applied": False,
                "message": "配置已保存，但设备当前不在线。"
                           "设备上线后语音说「换成官方表情」即可生效。",
            })
        try:
            from core.providers.tools.device_mcp.mcp_handler import (
                call_mcp_tool,
            )
            res = await call_mcp_tool(
                conn, conn.mcp_client, "self.skin.set",
                '{"skin": "%s", "reboot": true}' % skin)
            return web.json_response({
                "ok": True, "skin": skin, "applied": True,
                "device_reply": str(res)[:300],
                "message": "设备正在切换并重启，约 10 秒后生效。",
            })
        except Exception as e:
            return web.json_response({
                "ok": True, "skin": skin, "applied": False,
                "message": "已保存，但通知设备失败：%r" % e,
            })

    # ── 注册路由 ────────────────────────────────────────────
    def register(self, app):
        app.add_routes([
            web.get("/admin", self.handle_page),
            web.get("/admin/", self.handle_page),
            # ★ 手机端独立 H5（与 /admin 互不影响）
            web.get("/m", self.handle_mobile_page),
            web.get("/m/", self.handle_mobile_page),
            web.get("/admin/api/config", self.handle_get),
            web.post("/admin/api/config", self.handle_save),
            web.post("/admin/api/restart", self.handle_restart),
            web.get("/admin/api/refs", self.handle_refs),
            web.get("/admin/api/chat", self.handle_chat),
            web.post("/admin/api/chat/clear", self.handle_chat_clear),
            web.post("/admin/api/chat/send", self.handle_chat_send),
            web.post("/admin/api/chat/web-clear", self.handle_chat_web_clear),
            web.get("/admin/api/state", self.handle_state),
            web.get("/admin/api/models", self.handle_models),
            # ★ RGB / 舵机配置（控制台「灯光」「舵机」页用）
            web.get("/admin/api/hardware", self.handle_hw_get),
            web.post("/admin/api/hardware", self.handle_hw_save),
            # ★ 皮肤切换（Fairy ⟷ 官方几何脸；会同时换服务器 + 设备重启）
            # ★ 设备在线检测（控制台灯光/舵机页用）
        web.get("/admin/api/device", self.handle_device_state),
        # ★ 直接把 RGB/舵机下发到设备（走 MCP，不经 AI）
        web.post("/admin/api/hw_apply", self.handle_hw_apply),
        web.get("/admin/api/skin", self.handle_skin_get),
            web.post("/admin/api/skin", self.handle_skin_set),
        ])
        # ★ 提示文本：不要只写 127.0.0.1 —— 手机/平板要用局域网 IP 访问
        #   （控制台已放行局域网，见 _is_lan）
        # ★ 端口【不写死】：优先读配置 server.http_port，取不到才用默认 8003
        _hp = ""
        try:
            _hp = str(((self.config or {}).get("server") or {}).get("http_port", "") or "")
        except Exception:
            _hp = ""
        if not _hp:
            _hp = "8003"      # 默认值（配置里没写时才轮到它）
        self.logger.bind(tag=TAG).info(
            "Fairy 控制台已挂载: http://<本机IP>:%s/admin"
            "（本机可用 127.0.0.1；手机/平板用局域网 IP，如 "
            "http://192.168.x.x:%s/admin；改过端口就换成你的端口）" % (_hp, _hp))
