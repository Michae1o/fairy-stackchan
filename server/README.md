# 服务器改动说明（server/patches/）

覆盖目标：`xiaozhi-esp32-server/main/xiaozhi-server/core/api/`

---

## 文件清单

| 文件 | 作用 |
|---|---|
| `admin_handler.py` | **控制台后端**。新增：皮肤切换、硬件下发（RGB/舵机/转头/跟随）、重启、配置读写、设备状态查询 |
| `admin_page.html` | **控制台前端**。单文件（HTML + CSS + JS），无框架依赖 |
| `device_registry.py` | **设备连接注册表**（★ 新增文件） |

---

## 关键实现要点

### ① `device_registry.py` 是必需的

上游服务器**没有**「在线设备注册表」这个结构。
控制台要「给设备下发命令」，必须知道**哪条连接对应哪台设备**。

本项目新增 `device_registry.py`：

```python
# 注册（连接建立时）
register(device_id, conn)
# 查询（带健康检查 + 过期清理）
conn = get(device_id)
```

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
- 控制台默认只允许本机访问；要允许局域网访问需在上游配置里开 `remotes.enabled`
