# 安装说明（INSTALL）

> 请先读 [CREDITS.md](CREDITS.md)：**美术与音色素材有独立版权，本项目不附带。**

---

## 0. 你需要准备什么

```
硬件
  · M5Stack StackChan 整机（CoreS3 + 底座，含舵机 / 触摸 / IMU）
  · USB-C 数据线（能传数据，不是只能充电的）

软件
  · 固件编译环境：ESP-IDF v5.x 或 v6.x（官方推荐 v5.4+）
    ⚠️ 本项目在 ESP-IDF v6.1 上验证通过
  · 服务器：Python 3.10+（建议 3.12）

上游项目（必须先拉下来）
  · 固件  https://github.com/78/xiaozhi-esp32
  · 服务器 https://github.com/xinnan-tech/xiaozhi-esp32-server
```

---

## 1. 固件部分

### 1.1 拉取上游固件

```bash
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32
```

### 1.2 覆盖本项目的改动（**三条命令，缺一不可**）

```bash
cd /path/to/opensource

# ① 板卡目录（核心改动）
cp -r firmware/board-core-s3/* \
      xiaozhi-esp32/main/boards/m5stack/core-s3/

# ② 公共 I2C 设备层（★ 上游没有这个文件，是本项目新增的）
#    没有它 ⇒ 'TryReadRegs' was not declared in this scope
mkdir -p xiaozhi-esp32/main/boards/common
cp firmware/board-common/i2c_device.* \
   xiaozhi-esp32/main/boards/common/

# ③ 本地组件（★ 上游没有 components/ 目录，是本项目新增的）
#    没有它 ⇒ fatal error: smooth_ui_toolkit.hpp: No such file
cp -r firmware/components/* xiaozhi-esp32/components/

# ④ ota.cc（固件版本检查 + 本项目加的"记住自建地址"）
cp firmware/ota.cc xiaozhi-esp32/main/ota.cc

# ⑤ 显示层（★ 必须覆盖 main/display/，不是板卡目录）
#    上游 main/display/lcd_display.cc 已存在，且代码里是
#    #include "display/lcd_display.h" ⇒ 放错位置会导致
#    undefined reference to `LcdDisplay::ShowTopBarTemporarily()'
cp firmware/display/* xiaozhi-esp32/main/display/
```

> **覆盖的内容包括**：
> - `board-core-s3/`：板卡主文件、皮肤管理、几何脸、传感器、`drivers/`、`stackchan_avatar/`、`FTServo/`
> - `board-common/`：`i2c_device.{h,cc}`（★ 新增文件，含不 abort 的 `TryReadRegs`）
> - `components/`：`smooth_ui_toolkit`（第三方 UI 库，MIT）+ `mooncake` / `mooncake_log`
> - `ota.cc`：OTA 版本检查
> - `display/`：`lcd_display.{cc,h}`（★ 覆盖 `main/display/`，含菜单/状态栏改动）
>
> ⚠️ **五条 cp 之外没有别的步骤** —— 上游其余文件一律不动。

### 1.3 ★★ 必做：选对板卡（否则等于没改）

**上游默认板卡不是 CoreS3** —— 不选的话，`main/boards/m5stack/core-s3/`
里的文件**根本不会被编译**（链接时才发现一堆 undefined reference）。

**先选板卡（二选一）：**

```bash
cd xiaozhi-esp32

# 方式 A：交互式（推荐新手）
idf.py set-target esp32s3
idf.py menuconfig
#   进入：Xiaozhi Assistant → Board Type → M5Stack CoreS3
#   保存退出

# 方式 B：直接写进 sdkconfig（脚本/CI 用）
echo "CONFIG_BOARD_TYPE_M5STACK_CORE_S3=y" >> sdkconfig
idf.py reconfigure
```

> **怎么验证选对了？** 编译后看这个目录有没有 `.obj`：
> `build/esp-idf/main/CMakeFiles/__idf_main.dir/boards/m5stack/core-s3/`
> **空的 = 板卡没选对**（这时编出来的固件跟本项目无关）。

### 1.4 ★★ 必做：改 `main/CMakeLists.txt`（否则一定链接失败）

**这一步最容易被忽略，但不做就一定失败**（即使板卡选对了）。

```
原因：上游用 file(GLOB ...) 收源文件，而它【不递归子目录】：

    file(GLOB BOARD_SOURCES
        .../boards/${BOARD_DIR}/*.cc      ← 只收这一层
        .../boards/${BOARD_DIR}/*.c
    )

⇒ FTServo/ drivers/ stackchan_avatar/ 里的文件【不会被编译】
⇒ 链接期报一堆 undefined reference（舵机/传感器/装饰器/几何脸全丢）
```

**做法**：把 `firmware/CMakeLists.append.txt` 的**全部内容**，
粘贴到 `xiaozhi-esp32/main/CMakeLists.txt` 里这一行的**后面**：

```cmake
list(APPEND SOURCES ${BOARD_SOURCES})
```

> ⚠️ 那段代码有 `if(BOARD_DIR STREQUAL "m5stack/core-s3")` 包裹，
> 只对 CoreS3 生效，不影响其他板卡。

### 1.5 ★ 改一处配置（或让它自动识别）

**两种方式，任选一种：**

#### 方式 A（推荐）：编译时烧入你的服务器地址

编辑 `main/boards/m5stack/core-s3/config.json`，在 `sdkconfig_append` 里加一行：

```json
"CONFIG_OTA_URL=\"http://<你的服务器IP>:8003/xiaozhi/ota/\""
```

#### 方式 B：不动配置，开机后在设备上设置

固件第一次启动会进入配网模式，连上 WiFi 后，
在 Web 控制台里填服务器地址即可（会存进设备 NVS）。

> **自动识别机制**：本项目已把固件里写死的 IP 全部改成**自动识别** ——
> 「自建服务器地址」= 设备当前正在用的 OTA 地址，
> 切皮肤时会自动记住/恢复，**不会因为切换而丢失**。

### 1.6 编译 & 烧录

```bash
cd xiaozhi-esp32
idf.py set-target esp32s3
idf.py build
idf.py -p <你的串口> flash
```

> Windows 上串口形如 `COM3`，Linux/macOS 形如 `/dev/ttyUSB0` / `/dev/cu.usbserial-*`
>
> **进下载模式**：多数 StackChan 需要**按住复位键约 3 秒**直到内部 LED 变色，
> 或按住 BOOT 键再按一下 RST。

> ✅ **本项目的发布者已用「干净上游 + 覆盖本包」的方式实测编译通过**
> （2026-09-19，ESP-IDF v6.1）。
> 如果你的编译报 `undefined reference`，**99% 是漏了 1.3 那一步**。

---

## 2. 服务器部分

### 2.1 拉取上游服务器

```bash
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
```

### 2.2 覆盖本项目的改动

```bash
cp opensource/server/patches/*.py \
   xiaozhi-esp32-server/main/xiaozhi-server/core/api/
cp opensource/server/patches/admin_page.html \
   xiaozhi-esp32-server/main/xiaozhi-server/core/api/
```

### 2.3 配置

按上游文档配置 `data/.config.yaml`，至少需要：

```yaml
LLM:
  ChatGLMLLM:            # 或任何兼容 OpenAI 协议的模型
    api_key: <你的 Key>
    model_name: deepseek-chat

TTS:
  # 若用 GPT-SoVITS：
  GPTSoVITSTTS:
    api_url: http://127.0.0.1:9880
```

### 2.4 启动

```bash
python app.py          # 在 main/xiaozhi-server/ 目录下
```

启动后浏览器打开（**8003 是默认端口，可在 `data/.config.yaml` 的
`server.http_port` 改；本项目代码会读这个配置，不写死**）：

```
http://<服务器IP>:8003/admin   电脑版控制台
http://<服务器IP>:8003/m       手机版控制台
```

---

## 3. 验证它工作正常

```
① 设备能连上服务器
  → 串口日志出现 OTA 请求，控制台显示"设备已连接"

② 摸头顶 → 开心表情 + 冒爱心
③ 用力甩 → 晕眩转圈
④ 说话   → 嘴一开一合
⑤ 点屏幕 → 状态栏短暂出现（左侧 WiFi / 右侧电量，约 3 秒后自动隐藏）
⑥ 按电源键短按 → 切换皮肤（设备重启约 10 秒）
⑦ 打开控制台 → 皮肤页能点卡片切换、视觉页能填模型名
```

---

## 4. 常见问题

### ⚠️ 切回自建皮肤后，设备却连了官方服务器

**表现**：形象是对的，但**声音/人设变成官方的**；设备日志一切正常（`skin saved` 成功）。

**原因**：设备连谁由 **OTA 地址**决定，
而"自建地址"的最后一层兜底是编译期的 `CONFIG_OTA_URL`。
**它一旦缺失，就会回落到上游默认 = `https://api.tenclass.net/xiaozhi/ota/`（官方）。**

按顺序查这三处：

```
① config.json 里 CONFIG_OTA_URL 是否还在、且指向【你自己】的服务器
      main/boards/m5stack/core-s3/config.json → sdkconfig_append
      ⛔ 若为空/被删 ⇒ 必然连官方
      ⛔ 改了它必须【重新 configure + 重新编译】才生效

② 编译产物里是否真带上你的地址（验证配置真的编进去了）
      Windows:  findstr /C:"<你的IP>" build\merged-binary.bin
      Linux  :  grep -c "<你的IP>" build/merged-binary.bin
      搜不到 ⇒ 配置没生效，回 ①

③ 看设备串口日志里的实际地址
      SkinManager: skin saved: <皮肤名>, ota_url=<实际地址>
      若是官方 ⇒ NVS 里的旧值在作怪：
      让设备成功连上一次你的服务器即可自动覆盖
      （固件有"自动记住自建地址"机制，见 firmware/README）
```

> **实现层面的完整说明**（两套皮肤 / 两套服务器的设计、三层兜底、
> 自动记忆机制）见 [firmware/README.md](firmware/README.md) 的
> 「双皮肤是怎么实现的」一节。

---

### 其他常见问题

| 现象 | 原因 / 解法 |
|---|---|
| **菜单点不动** | 确认 `InitializeLvglInput()` 被调用（本项目已修）。LVGL 需要注册 `lv_indev` 才能收到点击 |
| **爱心/晕眩出现了但不消失** | 确认 `avatar_->update()` 被每帧调用（本项目已修，在 `AvatarTick()`） |
| **甩晕没反应** | BMI270 地址是 **0x69**（不是 0x68）。地址错就读不到数据 |
| **切皮肤后连不上服务器** | 检查 NVS 里的 `wifi/ota_url`。本项目会自动记住自建地址（前提：先成功连过一次） |
| **切皮肤后音色变成官方的** | ★ 见上面「切回自建皮肤后，设备却连了官方服务器」 |
| **双唤醒词只有一个生效** | 两个都要写进 `sdkconfig.defaults.esp32s3`（**不是** `sdkconfig`，后者每次构建会被重建） |

---

## 5. 回到官方固件

强烈建议**刷之前先备份出厂固件**：

```bash
esptool.py --port <串口> read_flash 0 0x1000000 factory-backup.bin
```

恢复：

```bash
esptool.py --port <串口> write_flash 0x0 factory-backup.bin
```
