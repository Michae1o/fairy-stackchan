"""网页聊天（Fairy 控制台用）

用户在网页打字 → 调本地 LLM（与设备同一套配置）→ 返回文字。
★ 明确不做的事：不把文字推给设备出声（那需要注入设备 WebSocket 连接，
  且会和设备当前对话状态冲突）。本模块只做"网页 ↔ LLM"。

设计要点：
  1. 【复用设备同一 LLM 配置】从 .config.yaml 的 selected_module.LLM 拿
     provider 名，实例化同一个 provider —— 保证模型/参数/Key 完全一致。
  2. 【惰性初始化 + 缓存】实例化要读配置，缓存在模块级，
     但配置变更后要能重建（按配置文件的 mtime 判断）。
  3. 【复用当前人设】system prompt 取自配置的 prompt 字段
     （即控制台里能改的那个），保证网页聊天与设备人格一致。
  4. 【带上下文】保留最近 N 轮，让网页聊天有连贯性（但不写进设备对话）。
  5. 【异常不崩】LLM 报错时返回可读错误，不是 500 空体。
"""

import io
import os
import sys
import threading
import time

import yaml

# ★ 提前把 conda 的 Library\bin 加进 DLL 搜索路径。
#   原因：本项目 provider 初始化链会 import opuslib（音频编解码），
#   而 opus 的 DLL 在 <env>\Library\bin 下。如果调用方（比如诊断脚本、
#   或从别处 import 的进程）没设好 PATH，就会报
#   "Could not find Opus library"，导致 LLM 初始化失败。
#   服务器正常启动时已设好，这里做兜底，让本模块在任何入口都能用。
def _ensure_opus_dll():
    if os.name != "nt":
        return
    try:
        prefix = sys.prefix
        for sub in ("Library\\bin", "Library\\lib", "DLLs"):
            d = os.path.join(prefix, sub)
            if os.path.isdir(d):
                try:
                    os.add_dll_directory(d)
                except (AttributeError, OSError):
                    pass
                if d not in os.environ.get("PATH", ""):
                    os.environ["PATH"] = d + os.pathsep + os.environ.get(
                        "PATH", "")
    except Exception:
        pass


_ensure_opus_dll()

_LOCK = threading.Lock()
_CACHE = {
    "provider": None,
    "cfg_mtime": 0,
    "cfg_path": None,
    "error": None,
}

# 网页聊天的上下文轮数（保留最近 N 条消息，含用户+AI）
MAX_CTX = 12


def _find_config_path():
    this = os.path.abspath(__file__)                 # core/api/chat_llm.py
    api_dir = os.path.dirname(this)                  # core/api
    server_root = os.path.dirname(os.path.dirname(api_dir))
    return os.path.join(server_root, "data", ".config.yaml")


def _read_cfg():
    p = _find_config_path()
    with io.open(p, encoding="utf-8") as f:
        return yaml.safe_load(f.read()), p


def get_provider():
    """惰性创建并缓存 LLM provider（配置变了会自动重建）"""
    with _LOCK:
        cfg, path = _read_cfg()
        mtime = os.path.getmtime(path)
        if (_CACHE["provider"] is not None
                and _CACHE["cfg_path"] == path
                and _CACHE["cfg_mtime"] == mtime):
            return _CACHE["provider"], None
        try:
            name = (cfg.get("selected_module") or {}).get("LLM")
            mod = (cfg.get("LLM") or {}).get(name)
            if not mod:
                return None, "配置里找不到 LLM provider: %s" % name
            ptype = mod.get("type")
            if ptype != "openai":
                return None, ("暂只支持 openai 兼容的 LLM（当前 type=%s）"
                              % ptype)
            # ★ 关键：复用官方工厂，不要手写实例化。
            #   实测 LLMProvider.__init__ 只接受 1 个参数（provider 自己的
            #   配置段），传 (config, mod) 两个会 TypeError。
            #   create_instance(config.get("type"), config) 是官方用法。
            from core.utils.llm import create_instance
            prov = create_instance(ptype, mod)
            _CACHE.update({"provider": prov, "cfg_mtime": mtime,
                           "cfg_path": path, "error": None})
            return prov, None
        except Exception as e:
            _CACHE["provider"] = None
            return None, "LLM 初始化失败: %r" % (e,)


def get_persona():
    """取当前人设（控制台可改的那个 prompt 字段）"""
    try:
        cfg, _ = _read_cfg()
        p = cfg.get("prompt")
        if isinstance(p, str) and p.strip():
            return p
    except Exception:
        pass
    return "你是 Fairy，一个桌面机器人助手。"


# 网页聊天的会话上下文（内存态，进程重启即清空）
_CTX = []
_CTX_LOCK = threading.Lock()


def get_context():
    with _CTX_LOCK:
        return list(_CTX)


def append_context(role, content):
    with _CTX_LOCK:
        _CTX.append({"role": role, "content": content})
        while len(_CTX) > MAX_CTX:
            _CTX.pop(0)


def clear_context():
    with _CTX_LOCK:
        _CTX.clear()


def chat(text, use_context=True):
    """发一条消息给 Fairy，返回 (reply, error)。

    reply: str 或 None
    error: str 或 None
    """
    text = (text or "").strip()
    if not text:
        return None, "消息为空"

    prov, err = get_provider()
    if prov is None:
        return None, err or "LLM 不可用"

    persona = get_persona()
    dialogue = [{"role": "system", "content": persona}]
    if use_context:
        dialogue.extend(get_context())
    dialogue.append({"role": "user", "content": text})

    try:
        result = ""
        # ★ 复用 provider 的流式接口，把 token 拼起来；
        #   设上限防止异常情况下无限拼接。
        for part in prov.response("web-console", dialogue):
            if isinstance(part, str):
                result += part
            if len(result) > 20000:
                break
        result = result.strip()
        if not result:
            return None, "LLM 返回了空内容"
        if use_context:
            append_context("user", text)
            append_context("assistant", result)
        return result, None
    except Exception as e:
        return None, "LLM 调用失败: %r" % (e,)
