# -*- coding: utf-8 -*-
"""设备连接注册表 —— 记录「当前有哪些设备正连着服务器」

★ 为什么需要它（用户实测指出）：
   服务器原来的 WebSocketServer 【没有任何连接列表】，
   所以控制台问「设备在线吗」时无从查起，只能显示"未连接"。
   而 ConnectionHandler 实例其实是活的（有 websocket / mcp_client），
   只是没人把它登记下来。

★ 为什么需要【健康检查】（日志实测踩到）：
   `device_registry.unregister()` 在 connection.py 里有调用点，
   但实测日志里「设备已注销」= 0 次 —— 说明某些断开路径不经过那个 finally。
   ⇒ 注册表会堆【死连接】⇒ get() 可能返回死掉的那个 ⇒ 下发失败。
   ⇒ 所以不能只依赖"被正确注销"，必须能【自己识别死活】。

三重保障：
   ① 注册时清掉同一设备的旧连接（device_id 相同则替换）
   ② get() 时健康检查（websocket 已关 ⇒ 跳过并清理）
   ③ 过期清理（超过 STALE_SEC 没活动 ⇒ 踢掉）
"""
import threading
import time

# 多久没活动算"死连接"（小智是短连接，但被唤醒后会持续 listen）
STALE_SEC = 300

_lock = threading.Lock()
_conns = {}          # session_id -> {"conn":, "since":, "device":, "last":}


def _is_alive(conn) -> bool:
    """连接对象是否还活着（多判据，任一失败即认为已死）

    ★ 保守策略：只要【有任何证据表明它死了】就判死；
      拿不到证据时判活（宁可多留一会，别误杀活连接）。
    """
    if conn is None:
        return False
    # ① websocket 显式关闭
    ws = getattr(conn, "websocket", None)
    if ws is not None:
        # websockets 库：有 state 属性（枚举）或 closed 布尔
        try:
            st = getattr(ws, "state", None)
            if st is not None:
                # websockets >= 12: State.OPEN = 1
                if getattr(st, "name", "") in ("CLOSED", "CLOSING"):
                    return False
                if isinstance(st, int) and st > 1:
                    return False
            closed = getattr(ws, "closed", None)
            if closed is True:
                return False
        except Exception:
            pass
    # ② 连接对象自身的关闭标记
    for a in ("is_closed", "closed", "stop_flag"):
        v = getattr(conn, a, None)
        try:
            if v is True:
                return False
            if v is not None and hasattr(v, "is_set") and v.is_set():
                return False
        except Exception:
            pass
    # ③ MCP 客户端显式说未就绪（且连接已不在 listen）
    return True


def register(conn):
    """连接建立时登记（★ 会清掉同一设备的旧连接）"""
    sid = getattr(conn, "session_id", None) or str(id(conn))
    dev = ""
    for a in ("device_id", "client_id", "mac_address", "uuid"):
        v = getattr(conn, a, None)
        if v:
            dev = str(v)
            break
    now = time.time()
    with _lock:
        # ① 同设备旧连接 → 替换
        if dev:
            for k, v in list(_conns.items()):
                if v.get("device") == dev and k != sid:
                    del _conns[k]
        # ② 顺手清死连接
        for k, v in list(_conns.items()):
            c = v.get("conn")
            if c is None or not _is_alive(c):
                del _conns[k]
        _conns[sid] = {"conn": conn, "since": now, "device": dev,
                       "last": now}
    return sid


def unregister(conn):
    """连接断开时注销（★ 尽力而为，不依赖它）"""
    sid = getattr(conn, "session_id", None)
    with _lock:
        if sid and sid in _conns:
            del _conns[sid]
        else:
            for k, v in list(_conns.items()):
                if v.get("conn") is conn:
                    del _conns[k]
                    break


def _touch(conn):
    """标记活动（get 时不改，心跳时改）"""
    sid = getattr(conn, "session_id", None)
    with _lock:
        if sid in _conns:
            _conns[sid]["last"] = time.time()


def _sweep():
    """清理死连接 + 过期连接（调用方需持锁或自行加锁）"""
    now = time.time()
    for k, v in list(_conns.items()):
        c = v.get("conn")
        if c is None or not _is_alive(c):
            del _conns[k]
            continue
        if now - v.get("last", v.get("since", now)) > STALE_SEC:
            del _conns[k]


def get(require_mcp=True):
    """取一台【活着的】在线连接

    ★ 关键：返回前做健康检查，死连接跳过并清理。
    """
    with _lock:
        _sweep()
        for v in _conns.values():
            c = v.get("conn")
            if c is None or not _is_alive(c):
                continue
            if require_mcp and not getattr(c, "mcp_client", None):
                continue
            v["last"] = time.time()
            return c
    return None


def get_any():
    """取任意一台【活着的】在线连接（即使还没 MCP 握手）"""
    with _lock:
        _sweep()
        for v in _conns.values():
            c = v.get("conn")
            if c is not None and _is_alive(c):
                v["last"] = time.time()
                return c
    return None


def list_all():
    """列出全部【活着的】在线连接（给控制台显示）"""
    now = time.time()
    with _lock:
        _sweep()
        return [
            {
                "session_id": k,
                "device": v.get("device", ""),
                "since": v.get("since", 0),
                "has_mcp": bool(getattr(v.get("conn"), "mcp_client", None)),
                "online_sec": max(0, int(now - v.get("since", 0))),
                "idle_sec": max(0, int(now - v.get("last", 0))),
            }
            for k, v in _conns.items()
        ]


def count():
    with _lock:
        _sweep()
        return len(_conns)


def debug_dump():
    """排查用：连死连接也列出来（不清理）"""
    with _lock:
        return [
            {
                "session_id": k,
                "device": v.get("device", ""),
                "alive": _is_alive(v.get("conn")),
                "has_mcp": bool(getattr(v.get("conn"), "mcp_client", None)),
                "idle_sec": int(time.time() - v.get("last", 0)),
            }
            for k, v in _conns.items()
        ]
