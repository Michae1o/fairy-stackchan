# 服务器改动说明（server/patches/）

**目录结构 = 镜像**：`patches/` 下的相对路径，直接对应服务器仓库里的路径。
安装脚本（`tools/apply_to_server.py`）就是按这个镜像关系拷过去的。

```text
patches/core/api/admin_handler.py     → main/xiaozhi-server/core/api/admin_handler.py
patches/core/utils/chat_log.py        → main/xiaozhi-server/core/utils/chat_log.py
...
```

---

## 文件清单

### 新增文件（上游没有）

| 文件 | 作用 |
|---|---|
| `core/api/admin_handler.py` | **控制台后端**。皮肤切换、硬件下发（RGB/舵机/转头/跟随）、重启、配置读写、设备状态、网页对话 |
| `core/api/admin_page.html` | **电脑版控制台前端**（`/admin`），单文件 HTML+CSS+JS |
| `core/api/admin_mobile.html` | **手机版控制台前端**（`/m`） |
| `core/api/device_registry.py` | **设备连接注册表**（控制台要知道「哪条连接是哪台设备」） |
| `core/api/chat_llm.py` | 控制台「网页对话」用的 LLM 调用（复用同一套模型配置） |
| `core/utils/chat_log.py` | **对话记录 + 状态灯**（thinking / speaking / idle，控制台眼睛会跟着变） |

### 覆盖上游同名文件（★ 上游更新时按 diff 合并；改动都是「只加不删」）

| 文件 | 加了什么 |
|---|---|
| `core/api/ota_handler.py` | 设备在 OTA 里上报皮肤时，**自动把服务器配置对齐**（否则按电源键切皮肤后服务器配置不同步） |
| `core/utils/dialogue.py` | **真实对话落进控制台记录**（会过滤 few-shot 示例 / system 提示词 / tool 中间结果） |
| `core/handle/sendAudioHandle.py` | 说话开始时上报 `speaking`、结束回 `idle`（控制台眼睛跟着变） |
| `core/providers/tts/gpt_sovits_v2.py` | **Fairy 音色后处理**（暖度/削尖/降调/语速，**默认全部关闭**；缺 librosa 等库时自动降级为原样输出） |

---

## 关键实现要点

### ① `device_registry.py` 是必需的（★ 但光有它不够，见下一节）

上游服务器**没有**「在线设备注册表」这个结构。
控制台要「给设备下发命令」，必须知道**哪条连接对应哪台设备**。

本项目新增 `device_registry.py`，API 就是这几个
（★ 参数是**连接对象**，不是 device_id）：

```python
register(conn)          # 连接建立时调用（内部按 conn.session_id 记录）
unregister(conn)        # 连接断开时调用
get(require_mcp=True)   # 取一条可用连接（带存活检查 + 过期清理）
get_any() / list_all() / count() / debug_dump()
```

### ①-2 ★★ 必须「接上线」：改上游两个文件（否则控制台是坏的 / 空的）

`admin_handler.py` 和 `device_registry.py` 只是两个模块，
**上游服务器不会自动加载它们** —— 必须在两个上游文件里各加几行调用：

```text
A. main/xiaozhi-server/core/http_server.py     （3 处 ⇒ 控制台才有 /admin 路由）
   ① import 区:
        from core.api.admin_handler import AdminHandler
   ② __init__ 里（self.vision_handler = ... 之后）:
        try:
            self.admin_handler = AdminHandler(config, self.logger)
        except Exception as e:
            self.admin_handler = None
            self.logger.bind(tag=TAG).warning(f"Fairy 控制台初始化失败（不影响其他接口）: {e}")
   ③ start() 里（"# 运行服务" 之前）:
        if self.admin_handler is not None:
            self.admin_handler.register(app)

B. main/xiaozhi-server/core/connection.py      （3 处 ⇒ 控制台才看得到设备、能下发）
   ① import 区（from collections import deque 之后）:
        from core.api import device_registry
   ② async def handle_connection(...) 第一行（try: 之前）:
        try:
            device_registry.register(self)
        except Exception as _e:
            self.logger.bind(tag=TAG).warning(f"[registry] 登记失败: {_e!r}")
   ③ 那个 finally: 块开头（await self._save_and_close(ws) 之前）:
        try:
            device_registry.unregister(self)
        except Exception:
            pass
```

**⇒ 一键做 + 自动自检**（幂等，锚点找不到会报错）：

```bash
python3 tools/apply_to_server.py /path/to/xiaozhi-esp32-server
```

**⇒ 判据**（这 6 个字符串都在 = 真的接上了；照抄即可用来检查）：

```text
http_server.py : from core.api.admin_handler import AdminHandler
http_server.py : self.admin_handler = AdminHandler(
http_server.py : self.admin_handler.register(app)
connection.py  : from core.api import device_registry
connection.py  : device_registry.register(self)
connection.py  : device_registry.unregister(self)
```

⛔ **不接的后果**：`/admin` 直接 404（控制台整块用不了）；
或控制台能开、但看不到设备在线，皮肤切换 / RGB / 舵机 / 转头全部点了没反应。

### ② 「下发成功」≠「服务器记住」

```
现象：开关点一下变成"开"，刷新页面又跳回去

原因：服务器下发成功后没把值落盘 ⇒ 下次读回来还是旧值/默认值
修法：下发成功后调 _persist_hw_value() 写进配置
```

**凡是有 UI 开关/回填的 kind**（`motion_auto` / `led_mode` / `servo_home`）
都必须在下发成功后落盘。

### ③ 控制台地址自动识别，不写死 IP

```python
# 旧写法（本项目已废弃）：SKIN_SELF_OTA = "http://<写死的IP>:8003/..."
# 新写法：从请求的 Host 头推断
def _self_ota_from_request(request):
    host = request.headers.get("Host")
    ...  # ⇒ 谁访问控制台，就用谁的地址
```

理由：开源后别人拿去，写死的 IP 会指向一个不存在的机器。

### ④ 「保存并重启」必须独立进程启动

```
现象：点「保存并重启」⇒ 服务关掉了，但没起来

原因：重启脚本的第一件事是"杀掉所有服务进程"，
      而它自己是服务器 fork 出来的 ⇒ 被连坐杀掉

修法：用 cmd /c start "" /b <专用bat> 启动（独立进程 + 有控制台）
      并确保 all_service_pids() 排除自身 PID
```

### ⑤ MCP 工具名要 sanitize

```
self.servo.set_angles  →  字典 key 实际是  self_servo_set_angles
（sanitize_tool_name 把非 [a-zA-Z0-9_\-一-鿿] 换成 _）
```

---

## 安装

见 [../INSTALL.md](../INSTALL.md) 第 2 节。

## 注意

- `data/.config.yaml` **不在本包内**（含 API Key），请自行配置
- ★ **控制台的访问控制（如实说明）**：
  - **放行**：本机（127.0.0.1 / ::1）+ **RFC1918 局域网地址**
    （`10.x` / `192.168.x` / `172.16-31.x` / IPv6 链路本地与唯一本地）
    ⇒ 手机、平板连同一个 WiFi 就能打开控制台
  - **拒绝**：公网 IP（返回 403）
  - ⛔ **没有登录/密码/token 校验** —— 能访问到这个地址的人就能改你的
    大模型 Key、人设、音色、皮肤
  - ⇒ 只在内网用，**不要把这个端口映射到公网**；
    需要外网访问请用【反向代理 + HTTP 基础认证】或 VPN
- 上游配置里的 `remotes.enabled = true` 会**放行全部来源**（含公网），慎用
