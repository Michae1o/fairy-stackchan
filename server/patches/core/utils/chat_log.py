"""对话记录器（Fairy 控制台用）

作用：把设备与 Fairy 的对话实时落盘，供 Web 控制台展示。

★ 设计要点（都是踩过坑才这么写的）：
  1. 【只记真实对话】Dialogue.put() 也被用于 few-shot 示例
     （is_temporary=True）与 system 提示 —— 这些必须跳过，
     否则对话记录里会混进"给我讲个故事吧"之类的示例台词。
  2. 【按会话分文件】每天一个 jsonl + 一份滚动索引，避免单文件无限增长
     导致页面一次读几 MB。
  3. 【追加写 jsonl】不用"读-改-写"，避免并发写坏文件；
     每行一个 JSON，读时逐行解析，坏行跳过（不因一行坏数据丢整段历史）。
  4. 【容量上限】保留最近 N 条（默认 500），超出自动裁剪索引。
  5. 【写失败绝不抛异常】记录对话是附属功能，
     磁盘满/权限问题不能影响设备正常说话。
"""

import io
import json
import os
import threading
import time

_LOCK = threading.Lock()

# 对话记录目录（项目根下的 data/chat_logs）
_THIS = os.path.abspath(__file__)
# core/utils/chat_log.py -> core/utils -> core -> <server_root>
_SERVER_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_THIS)))
_PROJECT_ROOT = os.path.abspath(os.path.join(_SERVER_ROOT, "..", "..", ".."))
CHAT_DIR = os.path.join(_PROJECT_ROOT, "data", "chat_logs")
INDEX_FILE = os.path.join(CHAT_DIR, "index.json")
STATE_FILE = os.path.join(CHAT_DIR, "state.json")

MAX_KEEP = 500          # 索引里最多保留多少条消息
MAX_TEXT = 4000         # 单条内容最大长度（防超长刷爆）
STATE_TTL = 90          # 状态超过这么久没更新就算过期（秒）


# ── 状态（给 Web 控制台的眼睛用）────────────────────────────
# 网页是 HTTP 轮询，拿不到设备 WebSocket 的事件流，所以这里落一个
# 轻量状态文件：谁在说话/在思考，网页读它来切换眼睛样式。
# ★ 状态是"瞬时"数据，所以单独一个文件、带时间戳、读时判过期，
#   不混进对话历史（否则会污染记录、还会让索引膨胀）。

def set_state(state, detail=""):
    """写当前状态：idle / thinking / speaking

    ★ 绝不抛异常：状态是附属信息，出错不能影响设备说话。
    """
    if state not in ("idle", "thinking", "speaking"):
        return False
    try:
        with _LOCK:
            _ensure_dir()
            obj = {"state": state, "detail": detail or "",
                   "ts": round(time.time(), 3)}
            tmp = STATE_FILE + ".tmp"
            with io.open(tmp, "w", encoding="utf-8", newline="\n") as f:
                f.write(json.dumps(obj, ensure_ascii=False))
            os.replace(tmp, STATE_FILE)
        return True
    except Exception:
        return False


def get_state():
    """读当前状态；过期或无文件时返回 idle"""
    try:
        with io.open(STATE_FILE, encoding="utf-8") as f:
            obj = json.loads(f.read())
        if not isinstance(obj, dict):
            return {"state": "idle", "ts": 0}
        if time.time() - float(obj.get("ts", 0)) > STATE_TTL:
            return {"state": "idle", "ts": obj.get("ts", 0),
                    "expired": True}
        return obj
    except Exception:
        return {"state": "idle", "ts": 0}


def _day_file(ts=None):
    t = time.localtime(ts or time.time())
    return os.path.join(CHAT_DIR, time.strftime("%Y-%m-%d", t))


def _ensure_dir():
    os.makedirs(CHAT_DIR, exist_ok=True)


def record(role, content, device_id="", ts=None):
    """记录一条对话。

    role: "user" | "assistant"
    content: 文本
    返回 True/False（失败不抛异常）
    """
    if role not in ("user", "assistant"):
        return False
    if not content or not str(content).strip():
        return False
    text = str(content).strip()
    if len(text) > MAX_TEXT:
        text = text[:MAX_TEXT] + "…（已截断）"
    ts = ts or time.time()
    item = {
        "ts": round(ts, 3),
        "time": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts)),
        "role": role,
        "content": text,
        "device": device_id or "",
    }
    try:
        with _LOCK:
            _ensure_dir()
            # ① 追加到当天 jsonl（追加写，不怕并发）
            with io.open(_day_file(ts), "a", encoding="utf-8",
                         newline="\n") as f:
                f.write(json.dumps(item, ensure_ascii=False) + "\n")
            # ② 更新滚动索引
            idx = _read_index()
            idx.append(item)
            if len(idx) > MAX_KEEP:
                idx = idx[-MAX_KEEP:]
            _write_index(idx)
        return True
    except Exception:
        # ★ 绝不影响主流程
        return False


def _read_index():
    try:
        with io.open(INDEX_FILE, encoding="utf-8") as f:
            d = json.loads(f.read())
            return d if isinstance(d, list) else []
    except Exception:
        return []


def _write_index(idx):
    tmp = INDEX_FILE + ".tmp"
    with io.open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(idx, ensure_ascii=False))
    os.replace(tmp, INDEX_FILE)


def recent(limit=100, since_ts=None):
    """读取最近对话（给 Web 控制台用）"""
    idx = _read_index()
    if since_ts:
        idx = [x for x in idx if x.get("ts", 0) > since_ts]
    if limit and len(idx) > limit:
        idx = idx[-limit:]
    return idx


def clear():
    """清空对话记录（索引 + 当天文件）"""
    try:
        with _LOCK:
            _ensure_dir()
            _write_index([])
            for fn in os.listdir(CHAT_DIR):
                if fn == "index.json" or fn.endswith(".tmp"):
                    continue
                try:
                    os.remove(os.path.join(CHAT_DIR, fn))
                except OSError:
                    pass
        return True
    except Exception:
        return False
