# INSTALL —— 从零装到能说话（标准 SOP）

> 这份文档是**唯一的安装依据**。按顺序做，每步都给「怎么知道这一步成了」。
> ★ **章节顺序 = 动手顺序**：§1 服务器（线一，不碰设备）→ §2 固件（线二）
> → §3 合流（线三）。总览见 §0.7。
> ⛔ 别跳步：跳步不会立刻报错，而是在很后面才炸（文档里标了「顺序反了会怎样」）。
>
> 本仓库给的是**改动源码 + 脚本**，不是能直接用的整套工程 ——
> 你要有一份上游工程，然后把改动打上去。三个仓库的关系见下面 §0.5。

---

## §0 前提

### 0.1 硬件

- **StackChan 套件**（M5Stack **CoreS3** 主控）
- **USB‑C 数据线**（能传数据的，不是只充电的）
- **2.4GHz WiFi**（ESP32 不支持 5GHz）
- 一台电脑（Windows / macOS / Linux 都行；本文以 Windows 为例）

### 0.2 软件

| 要装的东西 | 版本要求 | 用来干什么 |
|---|---|---|
| **ESP-IDF** | **v6.1**（本项目实测版本；v5.x 理论可行但未测） | 编译固件 |
| Python | 3.10+ | 跑 `tools/` 里的脚本 |
| **esptool** | **v5**（`python -m esptool` 能跑） | 烧录（命令行方式） |
| GPT‑SoVITS | 任意近期版本 | 音色（可选，见 §1.5） |

ESP‑IDF 装法见官方文档（Windows 用官方安装器最省事）。
装完在新终端里 `idf.py --version` 能出版本号才算好。

### 0.3 网络

- 本项目**联网**的地方：① 下载三个仓库 ② ESP‑IDF 首次编译时自动下载托管组件
  （`managed_components/`，见 §2.4 报错说明）③ 服务器装依赖
- **GitHub 连不上时**（国内常见）用这两种之一：

```bash
# 方式 A：走代理（把端口换成你自己的）
git -c http.proxy=http://127.0.0.1:<你的代理端口> clone https://github.com/78/xiaozhi-esp32.git

# 方式 B：直接下 tarball（不需要 git 也能拿源码）
curl -L -o xiaozhi-esp32.tar.gz https://codeload.github.com/78/xiaozhi-esp32/tar.gz/refs/heads/main
```

### 0.4 时间与空间

- 磁盘：**≈6GB**（IDF 工具链 ~2GB + 两个上游源码 + `build/` 目录）
- 首次编译：**15~25 分钟**（之后再编 2~4 分钟）
- 服务器首次装依赖：5~15 分钟

### 0.5 三个仓库，别搞混（★ 最容易出错的地方）

| 仓库 | 是什么 | 你要做什么 |
|---|---|---|
| **本仓库** `fairy-stackchan` | **只有改动**（源码文件 + 脚本 + 文档） | `git clone` 下来，命令里的路径都以它为准 |
| 上游**固件** `78/xiaozhi-esp32` | 完整可编译的固件工程 | 把本仓库的固件改动**打上去**（§2.2） |
| 上游**服务器** `xinnan-tech/xiaozhi-esp32-server` | 完整可运行的服务器 | 把本仓库的服务器改动**打上去**（§1.2） |

> ⚠️ 文档里凡是写 `xiaozhi-esp32/...` 的路径，都指**上游固件**；
> 写 `tools/...`、`firmware/...` 的，指**本仓库**。先 `cd` 到你 clone 的本仓库。

### 0.6 两条捷径（不做也行）

- 不想自己画表情 ⇒ 用 `tools/make_face.py` 一条命令生成（§2.3）
- 不想编译固件 ⇒ 直接刷 Release 里的预编译固件，见 [`firmware-bin/README.md`](firmware-bin/README.md)
  ⚠️ 但**服务器还是要先有**（§1）—— 「免编译」省掉的只是编译，不是服务器

---

### 0.7 ★ 推荐工序（三条线，照这个顺序，别跳）

> **顺序不是随便定的：先服务器、后设备。**
> 服务器那条线**不用设备、不用编译就能验证**，改错的代价小；
> 设备线一次编译十几分钟、刷机不可逆 —— 所以把服务器先跑通，能排掉一大半误判。
>
> ★ 原则：**先跑通，再换件** —— 先用上游默认件把整条链路跑通，
> 再逐个换成自己的（ASR / LLM / TTS），出问题才好定位。

**线一：服务器（先做，不碰设备）**

```text
① 拉服务器源码 + 装依赖（§1.1）
② 打服务器改动：tools/apply_to_server.py（§1.2）
③ 配置：★ 先【用上游默认件】跑通 —— 本地 ASR（零 Key）、先不接自建音色；
     大模型填你自己的 Key（§1.3）
④ 起服务：python app.py ⇒ /admin 返回 200（§1.4）
⑤ ★★ 服务器线的判据（不刷固件）：用仓库里的模拟设备脚本跑一遍
     python3 tools/test_server_e2e.py --audio ref.wav
     ⇒ 四个 ✅ 才算通：服务器 hello ｜ ASR 识别 ｜ LLM 回答 ｜ TTS 音频帧 > 0
⑥ 换件：接上你的音色（GPT-SoVITS :9880，§1.5）⇒ 再跑一次 ⑤
     ⇒ 这一步过了，「服务器那条线」才算真的通
```

**线二：固件（服务器通了再动设备）**

```text
⑦ 先备份出厂固件（★ 唯一不可逆的一步，别跳过）：
     python -m esptool --chip esp32s3 -p <串口> read-flash 0 0x1000000 factory-backup.bin
     判据：得到 16,777,216 字节的文件，存好
⑧ 装 ESP-IDF v6.1（§2.4.0）⇒ 新终端里 idf.py --version 能出版本号
⑨ 拉本包 + 上游固件（§2.1）；拷一份【独立副本】来改/编（§2.4 铁律 1）
⑩ 打固件改动：python3 tools/apply_to_upstream.py <上游副本>（§2.2）
⑪ 生成素材：tools/make_face.py --out fairy-assets
     ⇒ tools/verify_artifact.py --gif-dir fairy-assets（§2.3）
⑫ 编译：set-target → build → merge-bin（§2.4）
     判据必须含 **build/xiaozhi.bin 存在**
⑬ 验产物：tools/verify_artifact.py build/merged-binary.bin（§2.4）
⑭ 烧录（§2.5）⇒ 串口出现 `WS: Connecting to ws://…`（§2.6）
```

**线三：合流（让设备连到你的服务器）**

```text
⑮ 让设备找你那台服务器（§3，两种进法）：
     · 通用固件 ⇒ 设备配网页 →「高级选项」→「自定义 OTA 地址」：
                    http://<服务器IP>:<http_port>/xiaozhi/ota/    （默认 8003）
     · 编译时已把地址编进固件（§2.4.2）⇒ **直接配网即可**，配网页什么都不用填
⑯ 整机联调：喊唤醒词 → 说话 ⇒ 屏幕表情在动、喇叭是你的音色
```

**为什么是这个顺序**

- **线一不用设备也不用编译** ⇒ 先把服务器排干净；
  否则设备刷完连不上，你会去怀疑固件（这是最常见的误判方向）。
- **⑪ 必须在 ⑫ 之前**：素材是**编译时**打包进 `assets` 分区的，编完再换素材要重编一次。
- **⑦ 必须最先做**：整条流程里唯一不可逆的一步。
- **小改动别急着编**：首次编译 15~25 分钟、之后 2~4 分钟 ⇒ 攒够一次编。

**★ 五条铁律（踩出来的，照做能省几小时）**

```text
1. 别在要长期保留的源码树上直接编
   编译会生成/改动 sdkconfig、build/、managed_components/
   ⇒ 拷一份出来编（例：cp -r xiaozhi-esp32 xiaozhi-esp32-build）

2. 先拷 sdkconfig.defaults*，再删 sdkconfig 让它重新生成
   顺序反了 ⇒ 你的预置项不生效（典型症状：app partition is too small）

3. 用 `idf.py set-target esp32s3` + 手工写 CONFIG_BOARD_TYPE_M5STACK_CORE_S3=y
   ⛔ 别直接用上游的 scripts/build.py：它要从板卡 config.json 里读 board_type，
      脱敏过的 config 里没有那一项 ⇒ 会报 board_type not found

4. 判据必须包含【产物文件存在】（build/xiaozhi.bin）
   只看「✅ 编译完成」会被「提示成功但没产物」骗过去

5. 编不动先看僵尸进程：`idf.py` / `ninja` / `cc1plus` 卡着互抢 sdkconfig
   ⇒ 症状是「停在配置阶段、进程 0、无 error」
   ⇒ 处理：先清进程（pkill -9 -f idf.py / ninja / cc1plus），再从 sdkconfig.old 恢复
   ⇒ 记住：**进程 0 + 无 error = 中断，不是失败**，别急着重装工具链
```

## §1 线一：服务器（`xinnan-tech/xiaozhi-esp32-server`）★ 先做，不碰设备

> 这一线**不用设备、不用编译**就能验证完 —— 也是「先跑通，再换件」的前半段。

设备要能对话，必须有服务器：**语音识别 → 大脑（DeepSeek）→ 音色（TTS）** 都在这边。

### 1.1 拿源码 + 装依赖

```bash
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
cd xiaozhi-esp32-server/main/xiaozhi-server
pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple
```

> PyPI 慢/超时就换镜像（上面用的是清华源）。装完能 `python -c "import app"` 不报错才算好。

### 1.2 把本仓库的服务器改动打上去

```bash
cd fairy-stackchan
python3 tools/apply_to_server.py <上游服务器目录>
```

**改动共 10 个文件 + 2 处就地接线**（已逐项对照上游 `main` 核过）：

| 类别 | 文件 |
|---|---|
| **新增**（6，上游没有这些文件） | `core/api/admin_handler.py`（控制台后端）、`core/api/admin_page.html`（电脑版 `/admin`）、`core/api/admin_mobile.html`（手机版 `/m`）、`core/api/device_registry.py`（设备连接注册表）、`core/api/chat_llm.py`（控制台网页对话）、`core/utils/chat_log.py`（对话记录 + 状态灯） |
| **覆盖**（4，上游有、本项目改了） | `core/api/ota_handler.py`、`core/utils/dialogue.py`、`core/handle/sendAudioHandle.py`、`core/providers/tts/gpt_sovits_v2.py` |
| **就地接线**（2，脚本自动插入几行） | `core/http_server.py`（挂 `/admin` 路由）、`core/connection.py`（连接登记/注销） |

⛔ 接线那两个**不接就是坏的**：`/admin` 直接 404，或控制台能开但看不到设备、下发没反应。
接线代码与判据（6 个字符串）见 [`server/README.md`](server/README.md) §①-2。

每个文件干什么，见 [`server/README.md`](server/README.md)。

**判据**：脚本最后打印 **13/13 自检通过**；覆盖的那 4 个文件都是**只加不删**。
⛔ 覆盖是**整文件覆盖**（改动量 +14~+80 行），上游更新后要手工合并 ——
如果上游版本差得多，脚本会警告，别硬套。

> ⚠️ 只装一部分会连环报错：`admin_handler.py` 自己要 `import` `chat_log` / `chat_llm`，
> 缺了它们控制台直接 500。要装就 10 个一起装。

> ★★ **控制台不用单独搭建** —— 它就是这一步带来的：
> `core/api/admin_page.html`（电脑版 `/admin`）+ `core/api/admin_mobile.html`（手机版 `/m`）
> 是新增文件，接线那一步（`http_server.py` 里 `admin_handler.register(app)`）会在服务器启动时
> 自动把这两个路由挂上去。⇒ **打完这一步的补丁、起服务，控制台就有了**，没有额外的安装步骤。
> 怎么用（8 个页签 / 手机端差别 / 常见疑问 / 能直接调的 API）见 [`CONSOLE.md`](CONSOLE.md)。

### 1.3 配置 `config.yaml`

> ★★ **必须先建 `data/.config.yaml`** —— 上游**启动时会检查这个文件**，
> 缺了直接报 `FileNotFoundError: 找不到 data/.config.yaml`（不是"改 config.yaml 就行"）。

```bash
cd <上游服务器>/main/xiaozhi-server
mkdir -p data                                   # Windows: mkdir data
cp config.yaml data/.config.yaml                # Windows: copy config.yaml data\.config.yaml
# 之后【改的是 data/.config.yaml 这一份】
```

在 `data/.config.yaml` 里填这几项：

```yaml
server:
  ip: 0.0.0.0
  port: 8000                 # 设备用 WebSocket 连这个
  http_port: 8003            # OTA / 控制台用这个
  auth_key: <一串 32 位以上随机字符串>   # ★ 必填，否则视觉接口重启后随机失效
                                         #   生成：python -c "import secrets; print(secrets.token_hex(32))"

LLM:
  DeepSeekLLM:
    api_key: <你的 DeepSeek Key>
    base_url: https://api.deepseek.com
    model_name: deepseek-chat

TTS:
  GPTSoVITS:                 # 音色（见 §1.5）
    api_url: http://127.0.0.1:9880
```

（键名以你拿到的上游版本为准；上面是作者在用的那套名字。★ `LLM` 那一项的 provider 名要对上
上游 `config.yaml` 里的字段名，填错会在启动日志里看到「配置错误: LLM 的 API key 未设置」。）

### 1.4 起服务 + 判据

```bash
cd <上游服务器>/main/xiaozhi-server
python app.py
```

**成功判据**（四条都通才算服务器 OK）：

1. 控制台 `http://<服务器IP>:8003/admin` → **HTTP 200**，页面标题 `Fairy 控制台`
2. 手机版 `http://<服务器IP>:8003/m` → **200**
3. `http://<服务器IP>:8003/admin/api/state` → **200**（返回 JSON）
   —— 它 200 说明「对话记录 + 设备注册表」都接对了
4. ★★ **整条语音链路**（不用设备）：

```bash
python3 tools/test_server_e2e.py --audio <一段 5~10 秒人声 wav> \
        --url ws://<服务器IP>:8000/xiaozhi/v1/
```

它会**模拟一台设备**：握手 → 发音频 → 看服务器有没有回
`ASR 识别` / `LLM 回答` / `TTS 音频帧`。四个 ✅ 才算这条线通了：

```text
服务器 hello ✅ ｜ ASR 识别 ✅ ｜ LLM 回答 ✅ ｜ TTS 音频帧 > 0 ✅
```

没过时它会直接告诉你去查哪一环（ASR 配置 / 大模型 Key / 音色服务），
**别带着没通的服务器去刷固件** —— 那样你会去怀疑固件。

> **控制台怎么用**（电脑版 8 个页签各干什么、手机版 `/m` 差在哪、
> 「实时生效 vs 需重启」、常见疑问、能直接调的 API）⇒ [`CONSOLE.md`](CONSOLE.md)

### 1.5 线一的最后一步：换上你的音色（GPT‑SoVITS）

本仓库**不带**参考音频和微调权重（音频版权属原声者；权重是大文件）。

- **零样本克隆（推荐，不用训练）**：3~10 秒干净人声当参考，起 GPT‑SoVITS 的 API（默认 9880），
  服务器 §1.3 指向它即可。参考音频怎么录/怎么切、两条路的取舍 ⇒ [`voice-package/README.md`](voice-package/README.md)
- **微调（进阶）**：要训就用你自己的数据集；本项目的做法与参数也在同一份文档里
- 服务器侧还有一层 **Fairy 语气后处理**（暖度/削尖/降调/语速），
  **默认全部关闭**，想要就在 `gpt_sovits_v2.py` 里打开

---

## §2 线二：固件（服务器通了再动设备）

### 2.1 拿到三个仓库

```bash
git clone <本仓库>  fairy-stackchan
git clone https://github.com/78/xiaozhi-esp32.git          # 上游固件
# 服务器稍后再拿（§1.1）
```

### 2.2 把本仓库的固件改动打到上游（★ 用脚本，别手抄）

```bash
cd fairy-stackchan
python3 tools/apply_to_upstream.py <上游 xiaozhi-esp32 目录>
```

**这个脚本做的事**（= 本节 ①~⑨，手工做也是这些）：

```text
① firmware/board-core-s3/*      → xiaozhi-esp32/main/boards/m5stack/core-s3/
② firmware/board-common/*       → xiaozhi-esp32/main/boards/common/
③ firmware/components/*         → xiaozhi-esp32/components/
④ firmware/ota.cc               → xiaozhi-esp32/main/ota.cc
⑤ firmware/display/*            → xiaozhi-esp32/main/display/
⑥ 往 sdkconfig.defaults.esp32s3 追加唤醒词 Hi Fairy（上游只带「你好小智」）
⑦ 改 main/CMakeLists.txt：显式列出子目录源文件 + PRIV_REQUIRES + 表情包名改 fairy
⑧ 改 scripts/build_default_assets.py：加 fairy 表情包分支
⑨ 建 fairy-assets/：写情绪别名表（★ GIF 素材要你自己放，见 §2.3）
```

**判据**：脚本每个文件都打印「已覆盖 / 已存在，跳过」，最后一行是
`✅ 完成：N 个文件`（幂等 —— 重复跑只会说「已存在，跳过」，不会重复插入）。

**几个必须知道的点**：

- **⑥ 唤醒词只能写进 `sdkconfig.defaults.esp32s3`**，⛔ 不能写进 `sdkconfig` ——
  `sdkconfig` 在 `set-target` 时会被重新生成，写进去会丢（表现为：只有「你好小智」能唤醒）。
- **⑤ 之前必须保证上游有 `components/` 目录**：上游没有这个目录，
  脚本会自己 `mkdir -p`；你手工做时要先建，否则 `cp` 会报「目标不存在」。
- **别手改 `main/CMakeLists.txt` 里的源文件清单**：那里面每一行都有原因
  （GLOB 不递归、`.cpp` 后缀不被收、`.c` 素材要单独列），删一行就是链接期报错。
  细节见 [`firmware/README.md`](firmware/README.md)。
- **`--dry-run`** 可以先看会改什么不落盘；**`--no-cmake`** 只覆盖文件不动 CMake。

### 2.3 表情素材（设备上的那张脸）

**本仓库不带 GIF**（版权原因）。三种拿法：

```bash
# ① 用代码生成（推荐，零版权风险；一条命令）
python3 tools/make_face.py --out fairy-assets
python3 tools/verify_artifact.py --gif-dir fairy-assets     # 先验规格，再往下走

# ② 自己画 / ③ 用现成素材 ⇒ 规格要求见 fairy-assets/README.md 与 firmware/README.md
```

素材规格（`make_face.py` 生成的就是这个规格）：

| 项 | 值 |
|---|---|
| 尺寸 | **320×240**（必须是这个，固件按 1:1 贴图） |
| 格式 | GIF（`.png` 静态图也行） |
| 帧间隔 | 50 ms（20fps） |
| 循环 | **无限循环** |
| 体积 | 单张 ≤ 8MB，合计 ≤ 8MB（assets 分区大小） |
| 文件名 | 任意，但要有一份 `_emote_aliases.json` 把情绪名映射到文件名 |

把素材接进上游（会和 ①~⑨ 一起做，单独跑也行）：

```bash
python3 tools/apply_to_upstream.py <上游目录> --gif-dir fairy-assets
```

### 2.4 编译

#### 2.4.0 先把 ESP-IDF 装好（Windows）

1. 到乐鑫官方文档下 **ESP-IDF v6.1 的 Windows 安装器**（或离线安装包），一路下一步；
   中途会让你选组件，**默认全选就行**（要 Python 与工具链）
2. 装完开始菜单里会出现 **「ESP-IDF 6.1 PowerShell」** 之类的快捷方式 —— **用这个开终端**
   （它帮你设好了 PATH；直接开 PowerShell 跑 `idf.py` 会 `command not found`）
3. 判据：在这个终端里

```powershell
idf.py --version          # 出版本号（v6.1）即 OK
```

> Linux / macOS：`git clone --recursive ESP-IDF` 后跑 `./install.sh esp32s3`，
> 之后每次 `source ./export.sh`（或写进 `~/.bashrc`）。

#### 2.4.1 编译

```bash
cd <上游副本 xiaozhi-esp32 目录>
idf.py set-target esp32s3      # 判据：Target set to 'esp32s3'
idf.py build                   # 判据：Project build complete.
                               #       且 build/xiaozhi.bin 存在
```

- **首次编译十几分钟**（本机实测 7 分钟；视机器性能与是否已下载过组件而定）
  —— 首次会联网下载托管组件（`managed_components/`）并从零编 LVGL 等
- **`set-target` 会重新生成 `sdkconfig`** ⇒ 这就是为什么改动要写进 `sdkconfig.defaults*`（§2.2）
- 编译完合成整机固件（含引导 + 分区表，方便整片刷）：

```bash
idf.py merge-bin               # ⇒ build/merged-binary.bin
```

> ⛔ `merge-bin` 后面**不要**再加 `-o build/merged-binary.bin`：
> `idf.py` 已经在 `build/` 里执行，再加 `build/` 会变成 `build/build/...` 报
> `FileNotFoundError`。要指定就用裸文件名：`-o my.bin`。

**验产物**（别只看「编译成功」）：

```bash
python3 tools/verify_artifact.py <上游副本>/build/merged-binary.bin
```

它会告出：双唤醒词在不在、两个 MCP 工具在不在、有没有泄漏作者私货 IP、
内嵌了几张 GIF（0 就是素材没打进去）。

#### 2.4.2（可选）把服务器地址**编进固件**

这样刷完**只要配网就行**，配网页什么都不用填（合流见 §3 方法 B）。

```text
两条路，按你用的编译方式选：

· 用 scripts/build.py 编译
    把地址写进【板卡 config.json】：
      main/boards/m5stack/core-s3/config.json
      "CONFIG_OTA_URL=\"http://<你的服务器IP>:8003/xiaozhi/ota/\""
    （★ 这一项只有 scripts/build.py 会读；用 idf.py 编不生效）

· 用 idf.py 编译（本项目推荐）
    把地址写进 sdkconfig.defaults / sdkconfig.defaults.esp32s3：
      CONFIG_OTA_URL="http://<你的服务器IP>:8003/xiaozhi/ota/"
    （★ 写进 defaults 才能扛住 set-target 重新生成 sdkconfig，见 §0.7 铁律 2）
```

**判据**：刷完配好网，串口**直接**出现
`WS: Connecting to ws://<你的服务器IP>:8000/…`，而你没有在配网页填过任何地址。

> ★ **地址的优先级（源码确认，`main/ota.cc`）**：
> ① 先读设备 NVS 的 `wifi/ota_url` ② 它是空的才用编译期的 `CONFIG_OTA_URL`
> ⇒ 编进去的是**默认值**，事后仍可以用配网页覆盖（§3 方法 A）—— 两种不冲突。
> ⇒ 另外固件里有「自建地址自动记住」的逻辑：取到自定义地址时会记进 `fairy_skin/self_ota`，
>   切皮肤/重启不会丢（原理见 [`firmware/README.md`](firmware/README.md)）。

⛔ **写死地址的固件只适合自己用** —— 别公开分发（会把你的内网 IP 发出去）。

**编译卡住了先看这三样**

| 现象 | 先查什么 |
|---|---|
| 停在配置阶段不动、进程数 0、没有 error | 僵尸进程互抢（见 §0.7 铁律 5） |
| `app partition is too small` | `sdkconfig` 重建顺序错了（§0.7 铁律 2） |
| 卡在 `Downloading…` | 网络拉不到托管组件 ⇒ 挂代理重试 |

### 2.5 烧录

**先找到串口**（这一步别猜）：

```text
Windows：设备管理器 → 端口(COM 和 LPT) ⇒ 形如 「USB 串行设备 (COM5)」
         或命令行：python -m serial.tools.list_ports
Linux/macOS：ls /dev/ttyACM* /dev/cu.usbmodem*
```

**方式一：`idf.py`（最省事）**

```bash
idf.py -p <串口> flash         # 判据：Hash of data verified.
idf.py -p <串口> monitor       # 看日志（Ctrl+] 退出）
```

**方式二：`esptool` 直接烧（★ 最稳，本项目自己就用这条）**

```bash
# 整机固件（合并固件）从 0x0 开始烧
python -m esptool --chip esp32s3 -p <串口> -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
    0x0 build/merged-binary.bin
```

> 三种烧录方式（含**浏览器免安装**的网页烧录器）与预编译固件 ⇒ [`firmware-bin/README.md`](firmware-bin/README.md)

**烧不进去时按顺序试**

```text
① 换一根【数据】线、换 USB 口（最常见的真因）
② 关掉占用串口的程序（monitor、串口助手、另一个 idf.py）
③ 波特率降到 115200 再试
④ 先整片擦除再烧：python -m esptool --chip esp32s3 -p <串口> erase-flash
⑤ 手动进下载模式：按住 BOOT(或 GPIO0) → 点一下 RST → 松开 BOOT，再烧
⑥ Linux 报权限不足 ⇒ sudo usermod -aG dialout $USER（重新登录生效）
```

**★ 只换表情素材时不用整片重烧**（素材在 `assets` 分区，独立的）：

```bash
python -m esptool --chip esp32s3 -p <串口> write-flash \
    <assets 分区偏移> build/generated_assets.bin
```

> 偏移量看分区表（本项目用 `partitions/v2/16m.csv`，`assets` 那行的 offset 列）。
> 拿不准就整片重烧 `0x0`，慢一点但不会错。

**备份与回滚**（§0.7 ⓪ 已经做过备份的话，这里是恢复）：

```bash
# 读出厂固件（★ 只有这一步不可逆，务必先做）
python -m esptool --chip esp32s3 -p <串口> read-flash 0 0x1000000 factory-backup.bin
# 想回到出厂：把备份整片写回去
python -m esptool --chip esp32s3 -p <串口> -b 460800 write-flash 0x0 factory-backup.bin
```

### 2.6 设备端确认（这一步过了才算固件 OK）

`idf.py monitor` 里依次看到：

1. 开机自检（屏幕亮、显示表情）
2. 连上 WiFi
3. **`WS: Connecting to ws://<你的服务器IP>:8000/xiaozhi/v1/`** ← 这条是关键判据
4. 对它说唤醒词（**Hi Fairy** 或 **你好小智**），屏幕有反应

> 连不上服务器时**先别怀疑固件**：§3 有「让设备连过来」的正规做法。

---

## §3 线三：合流（让设备连到你的服务器）

> 前面两线各自验通了，这一步把它们接起来：**让设备去找你那台服务器**。

设备默认可能指向别处。让设备去找你那台服务器，**两种进法，按你手里的固件选一种**：

#### 方法 A：通用固件（免编译固件走这条）—— 在设备配网页填地址

1. 设备开机后，如果连不上 WiFi（或点一下屏）会进入**配网模式**，屏幕显示热点名
2. 电脑/手机连上那个热点，浏览器打开 `192.168.4.1`
3. 切到 **`Advanced` / 高级选项** 页签 → 填 **「自定义 OTA 地址」**：
   `http://<你的服务器IP>:<http_port>/xiaozhi/ota/`
   （`<http_port>` = 你配置里的 `server.http_port`，**默认 8003**；改过就写你的端口）
4. 保存 → 设备重启 → 串口日志里应该出现 `WS: Connecting to ws://<你的IP>:8000/...`

这个输入框写的是设备 NVS 里的 `wifi/ota_url`（和固件用的同一个键），
所以**免编译、免重刷**；也可以在设备上语音念 IP 让它自己改。

#### 方法 B：自己编译、**且已经把地址编进固件**了 —— 直接配网即可

```text
刷完只需要【配网】：连上你家 WiFi（配网时填 WiFi 密码）就行，
配网页里【什么都不用填】 —— 地址已经在固件里了（编地址的做法见 §2.4.2）

判据：配好网，串口【直接】出现 WS: Connecting to ws://<你的服务器IP>:8000/…
      而你从头到尾没有填过任何地址
```

> 适合「给朋友用」：编一个写死地址的固件，他刷完配个网就连你的服务器。
> ⚠️ 代价：这种固件**只能自己用，别公开分发**（内网 IP 写在里面）。
>
> 两种方法**不冲突**：编进去的是默认值（`CONFIG_OTA_URL`），
> NVS 里的 `wifi/ota_url` **优先** —— 哪天要换服务器，配网页再填一次就能覆盖它。

#### 怎么确认设备当前连的是哪台

```text
串口日志：SkinManager: skin saved: <皮肤名>, ota_url=<实际地址>
控制台  ：CONSOLE.md 的「设备」页会列出 Fairy 服务器 / 官方服务器两个地址
```

---

**⑯ 整机联调**（这一步过了，就算全通了）：

```text
喊唤醒词（Hi Fairy / 你好小智）→ 说话 ⇒ 屏幕表情在动、回话是你的音色
点一下屏 ⇒ 状态栏短暂出现（左 WiFi / 右电量）
摸头顶 → 开心 + 冒爱心 ｜ 用力甩 → 晕眩
打开控制台（CONSOLE.md）⇒ 能看到设备在线、状态灯跟着对话变
```

---

## §4 验证清单（每步都做，别跳）

| 验什么 | 命令 | 通过判据 |
|---|---|---|
| **服务器链路**（先验这个） | `python3 tools/test_server_e2e.py --audio ref.wav` | 四个 ✅：hello / ASR / LLM / TTS 帧 > 0 |
| 表情素材 | `python3 tools/verify_artifact.py --gif-dir fairy-assets` | 尺寸 320×240、无限循环、合计 ≤8MB |
| 固件产物 | `python3 tools/verify_artifact.py <上游>/build/merged-binary.bin` | 双唤醒词 ✅、两个 MCP 工具 ✅、无内网 IP ✅、内嵌 GIF > 0 |
| 服务器控制台 | 浏览器开 `/admin` | HTTP 200 + `Fairy 控制台` |
| 设备 | `idf.py monitor` | `WS: Connecting to ws://<你的IP>:8000/...` |
| 整机 | 喊唤醒词 → 说话 | 屏幕有表情反应、喇叭有回话 |

---

## §5 常见报错对照

| 现象 | 真因 | 怎么办 |
|---|---|---|
| `idf.py: command not found` | ESP‑IDF 环境没装好 / 没开新终端 | 重开终端，跑 IDF 的 `export.ps1`（或快捷方式）后再试 |
| 起服务器报 `FileNotFoundError: 找不到 data/.config.yaml` | 上游**必须**有这个文件 | `mkdir data` + `cp config.yaml data/.config.yaml`，之后改这一份（§1.3） |
| 起服务器报 `Could not find Opus library` | conda 环境没激活 ⇒ `Library\bin` 不在 PATH（Windows 常见） | 先 `conda activate <环境>` 再起；或把 `<conda环境>\Library\bin` 加进 PATH |
| 启动日志「配置错误: LLM 的 API key 未设置」 | 大模型那段没填/填错字段名 | 改 `data/.config.yaml` 的 LLM 段（§1.3） |
| `undefined reference to ...`（板卡类符号） | §2.2 的 `CMakeLists.txt` 清单没打上 | 用 `apply_to_upstream.py` 重跑；手工的话检查 `main/CMakeLists.txt` 里那几行 |
| `undefined reference to bmi270_init` | 托管组件没下载成功（网络） | 删 `build/` 和 `managed_components/` 重编（别自己手加 BMI270 的 `.c`） |
| `app partition is too small` | 分区表没生效 | 确认 `sdkconfig.defaults*` 里有 `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m.csv"`，且**先拷 defaults 再删 sdkconfig** 重建 |
| 只有「你好小智」能唤醒，Hi Fairy 不行 | 唤醒词没写进 `sdkconfig.defaults.esp32s3`（写进 `sdkconfig` 会被冲掉） | 见 §2.2 ⑥，重跑脚本 |
| 卡在下载 / 编译停住不动 | 联网下载托管组件失败 | 挂代理重试；或先编一次让它把组件拉全 |
| 串口打不开 / 被占用 | 有别的程序占着（monitor、串口助手） | 全关掉；换根数据线；换 USB 口 |
| 编译成功但设备不显示表情 | GIF 没打进 assets | `verify_artifact.py` 看「内嵌 GIF 数量」，0 就是没打进去 ⇒ 重跑 §2.3 |
| 设备一直连不上服务器 | OTA 地址没配对 | 按 §3 在配网页填；对着串口日志看它到底在连哪个 IP |
| 浏览器 `/admin` 404 | 服务器改动没打全 | 重跑 §1.2，确认 13/13 |
| 控制台点开关「自己跳回去」 | 服务器没把状态落盘 | 本项目已修（`_persist_hw_value`）；自改过就要照做 |
| 服务器有输出但设备没声音 | 采样率不一致 / 音频流没关连接 / WiFi 省电 | 三条都要满足：采样率严格相等、响应带 `Connection: close`、关 WiFi modem sleep |

---

卡在哪一步，就到 `tools/verify_artifact.py` 和对应目录的 `README.md` 里找判据 ——
**先拿判据确认事实，再动手改**。
