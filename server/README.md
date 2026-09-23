# 服务器改动说明（server/patches/）

**目录结构 = 镜像**：`patches/` 下的相对路径，直接对应服务器仓库里的路径。
安装脚本（`tools/apply_to_server.py`）就是按这个镜像关系拷过去的。

> **控制台怎么用**（电脑 `/admin` 的 8 个页签、手机 `/m` 的 4 个页签、
> 实时生效与需重启的区别、常见疑问、以及能直接调的 API）⇒ [`../CONSOLE.md`](../CONSOLE.md)

```text
patches/core/api/admin_handler.py     → main/xiaozhi-server/core/api/admin_handler.py
patches/core/utils/chat_log.py        → main/xiaozhi-server/core/utils/chat_log.py
...
```

---

## 文件清单

### 新增文件（★ 上游 `main` 里没有这些文件，已逐个核过）

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

B. main/xiaozhi-server/core/connection.py      （5 处）
   ── 前 3 处 ⇒ 控制台才看得到设备、能下发 ──
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
   ── 另外 2 处（控制台状态灯 + 声纹免重启）──
   ④ 对话开始处（self.dialogue.put(Message(role="user", content=query)) 之后）:
        from core.utils import chat_log ; chat_log.set_state("thinking", "llm")
   ⑤ _initialize_voiceprint() 里（voiceprint_config = self.config.get("voiceprint", {}) 之后）:
        ★ 改成【实时读 data/.config.yaml 的 voiceprint 段】——
          否则控制台里增删说话人后，必须重启服务器才生效。
          改完 ⇒ 设备【下次对话】就认新名单（该函数每个连接都会走一次）。
          一键脚本按锚点自动插入（锚点找不到会报错，不会静默跳过）。
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

---

## 声纹识别（可选功能）

让设备认得出「**是谁在说话**」：每次对话时服务器把这段音频拿去和已注册的声纹比对，
认出就把说话人名字带进上下文（Fairy 就知道这句是主人在说、还是别人）。

### 需要什么

一个**单独的声纹识别服务**（上游没有，要另外跑）：

- 用配套的开源项目 [`xinnan-tech/voiceprint-api`](https://github.com/xinnan-tech/voiceprint-api)
  （阿里 3D-Speaker 模型，**Apache-2.0**），跑在 **8005** 端口。
- ★ 本项目对它做了 **3 处改造：MySQL → SQLite**（省掉一个常驻数据库服务）。
  补丁在 [`third-party-patches/voiceprint-api-sqlite/`](third-party-patches/voiceprint-api-sqlite/)，
  按那份 README 覆盖过去即可。
  ⚠ **不覆盖的话，控制台的「已注册」状态会读不到** ——
  控制台是**直接读 SQLite 库文件**的（原版存 MySQL，读不到）。
- 首次启动会下载模型（约几百 MB），之后走本地缓存。

### 怎么接上

**第 1 步：跑起声纹服务**（假设放在 `<项目根>/voiceprint-api/`）

```bash
cd voiceprint-api
python -m venv venv
venv/Scripts/pip install -r requirements.txt      # Windows；Linux 用 venv/bin/pip
python app.py                                     # 首次下载模型，之后监听 0.0.0.0:8005
```

判据：`GET http://127.0.0.1:8005/voiceprint/health?key=<token>` 返回 `"status":"healthy"`。
（token 在它自己的 `data/.voiceprint.yaml` 里 `server.authorization`，首次启动自动生成。）

**第 2 步：在服务器配置里打开声纹**

编辑 `data/.config.yaml`（★ 是 **data/** 下那份，它会覆盖根目录的同名项）：

```yaml
voiceprint:
  # ★ 这里只需要它的 host + key —— 服务器会自己拼 /voiceprint/identify
  url: http://127.0.0.1:8005/voiceprint/health?key=<第 1 步那个 token>
  speakers:                      # 候选说话人： "id,名字,描述"
    - me,你的名字,一句话描述你自己
  similarity_threshold: 0.4      # 低于阈值就报「未知说话人」
```

**第 3 步：注册声纹**

打开控制台 `http://<服务器IP>:8003/admin` → 侧栏 **🗣️ 声纹**：

```text
① 「➕ 添加说话人」填 ID（英文，如 me）/ 名字 / 描述 → 点「添加」
② 「🎙️ 录音注册」选这个人 → 点「开始录音（6 秒）」→ 让本人对着麦克风正常说 6 秒
③ 列表里那个人从「未注册」变「已注册」 ⇒ 成了
```

> ⚠️ **麦克风只在 `http://127.0.0.1:8003/admin`（本机 localhost）或 https 下可用** ——
> 这是浏览器的安全策略（局域网 IP 下浏览器根本不暴露麦克风接口）。
> ⇒ 录音这一步要在**服务器那台电脑上**用 localhost 打开；
> **加人 / 删除 / 看列表不受这个限制**，手机也能做。
> 也可以直接在服务器上跑命令行：`python tools/voiceprint-register.py`

**第 4 步：验证**

让本人问设备「你知道我是谁吗」。服务器日志里会出现：

```text
声纹识别耗时: 0.2xx s
```

控制台的对话记录里，每条会多一个 `speaker` 字段
（认出来是名字，没认出来是「未知说话人」）。

### 实现要点（改了哪些）

| 文件 | 改了什么 |
|---|---|
| `core/connection.py` | `_initialize_voiceprint()` 里新增第 ⑤ 处接线：**实时读**配置的 `voiceprint` 段（否则控制台加完人必须重启服务器） |
| `core/api/admin_handler.py` | 5 个接口：列表 / 注册 / 删除声纹 + 增删说话人；注册时用 **ffmpeg** 把浏览器录的 webm 转成 16k wav 再转给声纹服务 |
| `core/api/admin_page.html` | 「🗣️ 声纹」页签（列表 / 添加 / 录音 / 删除），**每一步都给出成功或失败的具体原因**，不会点了没反应 |
| `third-party-patches/voiceprint-api-sqlite/` | 对声纹服务本身的 3 文件改造（MySQL → SQLite） |

> ★ **没改上游的声纹客户端** —— `core/utils/voiceprint_provider.py` 是上游自带的，
> 本项目只是把配置填上、并把候选名单做成了可以在网页里管理。
