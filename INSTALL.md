# 安装说明（INSTALL）

> 请先读 [CREDITS.md](CREDITS.md)：**美术与音色素材有独立版权，本项目不附带。**

---

## 0. 你需要准备什么

```
硬件
  · M5Stack StackChan 整机（CoreS3 + 底座，含舵机 / 触摸 / IMU）
  · USB-C 数据线（★ 要能传数据的，不是只能充电的）

软件（都要自己装）
  · 固件编译环境：ESP-IDF（上游要求 v5.4+；★ 本项目在 v6.1 上实测通过）
  · 服务器：Python 3.10+（建议 3.12）
  · 一个终端：Windows 用 PowerShell / cmd，Linux / macOS 用自带终端

网络（★ 最容易被低估的一条）
  · 编译固件要联网：拉上游代码 + 自动下载组件（managed_components/）
  · 国内直连 github.com 大概率超时 ⇒ 先备好代理，或按 1.1 用 codeload 下 tarball
  · 服务器侧要能访问你选的大模型 / TTS 服务

三个仓库（都要先拿到本地）
  · 本项目的包（含 firmware/ server/ 等目录，下文所有 cp 的“源”都来自它）：
        https://github.com/Michae1o/fairy-stackchan
  · 上游固件（要把本包覆盖到它上面）：https://github.com/78/xiaozhi-esp32
  · 上游服务器（同理）：https://github.com/xinnan-tech/xiaozhi-esp32-server

时间 / 空间
  · 首次编译 10~20 分钟（之后增量 1~3 分钟）
  · 磁盘：ESP-IDF 本体 + 工具链约 3~5 GB，项目 build/ 约 1~2 GB

★ 两条「不用装」的捷径（先想清楚再动手）
  · 只想让设备跑起来、不想装工具链 ⇒ 用【免编译固件】，
    见 firmware-bin/README.md，本节的编译部分整个跳过
  · 只想改人设 / 音色 / 大模型 ⇒ 完全不用碰固件，只做第 2 节（服务器）
```

---

## 1. 固件部分

### 1.0 ★ 建议的工序（作者本人的做法，供参考）

> ★ **这一节只是作者的实操顺序**，不是强制要求。
> 你也可以按自己的习惯来，但下面这个顺序最省事 ——
> **因为「服务器地址」是后面所有动作的前提，没有它什么都做不了。**

```text
① 先搭自建服务器（★ 硬前提，必须最先做）
     拉 xiaozhi-esp32-server，再把本项目 server/patches/ 覆盖到它里面，
     配好 data/.config.yaml（LLM / TTS / auth_key，见第 2 节）。
     ⇒ 做完你就有了【服务器地址】，后面每一步都要用它。

     ★ 也可以租一台能跑 TTS 的云服务器：
       好处是本地机器不用常开，出门也能用。

② 同时/紧接着：用官方固件把设备连上官方服务器
     设备先刷官方固件、连上 api.tenclass.net，确认：
       · 硬件正常（能唤醒、能对话、屏幕/舵机/灯都对）
       · 网络正常（WiFi 通、能连外网）
     ⇒ 这一步与 ① 没有依赖关系，可以并行。
     ⇒ 目的只是【先验证硬件 + 网络】，把这两个变量排除掉。
     ⇒ 不验也行，但硬件若有问题，后面排查会绕远路。

③ 把自建服务器地址写进固件
     main/boards/m5stack/core-s3/config.json → CONFIG_OTA_URL
     （见 1.5 节 ★ 必读）

④ 编译 + 刷机
     ⇒ 设备开机直接连上你的服务器，Fairy 皮肤 + 你的音色跑起来。

⑤ 最后再追加官方几何脸（第二套皮肤）
     ⇒ 它属于「锦上添花」，不影响主功能；
       而且它切过去就断开了你的服务器，放在最后试更省心。
```

**为什么是这个顺序？**

```text
① 服务器最先 → 它是【硬前提】：没有地址，固件填什么、设备连哪里都无从谈起
② 验硬件     → 建议项，不是前提；但先跑通官方，出问题能立刻分清是设备还是配置
③④ 填地址+刷机 → 这一步开始才真正需要 ① 的产物
⑤ 几何脸最后 → 它是可选项，且切换会重启设备，放最后不干扰前面几步
```

> ⚠️ **注意：本文档的章节顺序是「1 固件 → 2 服务器」，
> 与上面的建议工序【相反】。**
> 因为文档要按「材料准备 → 配置 → 验证」来组织才好读，
> 但**实际操作时请按 ①→⑤ 的顺序做**。
> 具体说：**先做完第 2 节（服务器），再回来做第 1 节（固件）。**

---

### 1.1 拉取上游固件

```bash
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32
```

> ⚠️ **国内直连 github.com 大概率失败（超时）** —— 两种绕法，任选：
>
> ```bash
> # ① 走代理（你本机有代理时；例：代理在 127.0.0.1:7897）
> git -c http.proxy=http://127.0.0.1:7897 clone \
>     https://github.com/78/xiaozhi-esp32.git
>
> # ② 用 GitHub 自己的源码打包域名（codeload 一般能直连）
> curl -L -o xiaozhi-esp32.tar.gz \
>     https://codeload.github.com/78/xiaozhi-esp32/tar.gz/refs/heads/main
> tar -xzf xiaozhi-esp32.tar.gz && mv xiaozhi-esp32-main xiaozhi-esp32
> ```
>
> （③ 你也可以自己找一个国内镜像站，但那是第三方，安全性请自行判断。）
>
> ★ 注意：**编译时还会自动下载一批组件**（`managed_components/`）和 Python 依赖，
> 那一步同样要能连外网 —— 卡在不动基本都是网络问题（见 1.6 的报错对照）。

### 1.2 把本项目的改动覆盖到上游（**6 步，缺一不可**）

> **前提**：下面命令里的 `firmware/...` 指的是【本项目包】里的目录，**不是上游**。
> 先 `cd` 到你自己放本项目的位置（把示例路径换成你的实际路径）：
> `cd /path/to/opensource` ⇒ 例如 `cd ~/fairy-stackchan`。
>
> ★ **别手抄下面这些命令**：本仓库自带脚本，一条命令做完 1.2 ~ 1.4（幂等、可重复跑）：
> `python3 tools/apply_to_upstream.py /path/to/xiaozhi-esp32`
> 手工做的理由只能是「想看懂每一步改了什么」。

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

# ⑥ 唤醒词「Hi Fairy」（★ 上游只带「你好小智」，不加这行就没有 Hi Fairy）
#    必须写进 sdkconfig.defaults.esp32s3
#    ⛔ 不要写进 sdkconfig —— 它每次构建会被重建，手写的改动会丢
echo "CONFIG_SR_WN_WN9_HIFAIRY_TTS2=y" >> xiaozhi-esp32/sdkconfig.defaults.esp32s3
```

> **覆盖的内容包括**：
> - `board-core-s3/`：板卡主文件、皮肤管理、几何脸、传感器、`drivers/`、`stackchan_avatar/`、`FTServo/`
> - `board-common/`：`i2c_device.{h,cc}`（★ 新增文件，含不 abort 的 `TryReadRegs`）
> - `components/`：`smooth_ui_toolkit`（第三方 UI 库，MIT）+ `mooncake` / `mooncake_log`
> - `ota.cc`：OTA 版本检查
> - `display/`：`lcd_display.{cc,h}`（★ 覆盖 `main/display/`，含菜单/状态栏改动）

### ★★ 覆盖前建议先 diff（**特别是同步上游之后**）

```
★ 为什么要 diff：
   本项目的文件是【基于某个上游版本】改的。
   如果你已经 pull 了更新的上游，那些文件可能已被官方改过 ——
   直接 cp 覆盖 = 把上游的新改动冲掉（可能是 bug 修复或新功能）。

⇒ 所以：先看差异，再决定怎么合并。
```

**方法 A：覆盖前先看会改哪些（不实际写）**

```bash
# 只列出差异文件（不覆盖）
diff -rq firmware/board-core-s3/ \
         xiaozhi-esp32/main/boards/m5stack/core-s3/ \
  | grep -v "^Only in xiaozhi-esp32"     # 忽略上游独有的文件
```

**方法 B：逐个看内容差异**

```bash
# 例：看板卡主文件的差异（本项目改了什么）
diff -u xiaozhi-esp32/main/boards/m5stack/core-s3/m5stack_core_s3.cc \
        firmware/board-core-s3/m5stack_core_s3.cc | less
```

**方法 C：★ 推荐 —— 用 git 管理，保留上游历史**

```bash
cd xiaozhi-esp32
git checkout -b my-fairy            # 开个分支做改动
# 然后把本项目的文件覆盖进去（用上面的 cp 命令）
git status                          # 看改了哪些
git diff                            # 看具体改了什么
git diff --stat                     # 看改动量
```

```
⇒ 这样你能清楚看到「本项目改了上游的哪些文件、改了多少行」，
  上游更新时也能用 git merge / rebase 处理冲突，而不是靠 cp 硬覆盖。
```

**★ 三类文件的处理方式不同：**

| 类型 | 例子 | 覆盖策略 |
|---|---|---|
| **本项目新增**（上游没有） | `board-common/i2c_device.*`、`PY32IOExpander_Class.*`、`components/*` | ✅ 直接放，不会冲突 |
| **本项目大改**（基本重写） | `m5stack_core_s3.cc`（12KB → 54KB） | ⚠️ 覆盖后上游更新难合并，建议以本项目为基准 |
| **本项目小改**（少量 patch） | `ota.cc`、`lcd_display.{cc,h}` | ⚠️ **优先 diff 后手工合并**，别盲覆盖 |

> ⚠️ **本项目按「覆盖」方式交付**，属于典型的 patch 包。
> 如果你要长期跟进上游，建议按方法 C 用 git 分支管理。

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

**★ 省事办法（推荐）**：跑一次本仓库自带的脚本，1.2 ~ 1.4 一次做完，
幂等、不会插错位置，锚点找不到会直接报错（不会静默跳过）：

```bash
python3 tools/apply_to_upstream.py /path/to/xiaozhi-esp32
```

**手工做的话，一共 3 处，缺一不可：**

**① 子目录源文件**（不做 ⇒ 舵机/传感器/表情素材全部链接失败）

把 `firmware/CMakeLists.append.txt` 里的**代码部分**
（`if(BOARD_DIR STREQUAL "m5stack/core-s3")` … `endif()` 那段）
粘贴到 `main/CMakeLists.txt` 里这一行的**后面**：

```cmake
list(APPEND SOURCES ${BOARD_SOURCES})
```

**② PRIV_REQUIRES 里加 4 个组件**（不做 ⇒ `fatal error: smooth_ui_toolkit.hpp: No such file`）

在 `main/CMakeLists.txt` 的 `idf_component_register(... PRIV_REQUIRES ...)`
列表里，找到 `xiaozhi-fonts` 那一行，在**它后面**加上：

```cmake
                        lvgl
                        smooth_ui_toolkit
                        mooncake
                        mooncake_log
```

> ★ `lvgl` 上游列表里也没有，一起加。缩进对齐即可（24 个空格）。

**③ 表情包名（Fairy 全屏动画的来源）**

```text
a) main/CMakeLists.txt 里 CoreS3 那一段：
     set(DEFAULT_EMOJI_COLLECTION noto-color-emoji_64)
   ⇒ 改成
     set(DEFAULT_EMOJI_COLLECTION fairy)

b) scripts/build_default_assets.py 的 get_emoji_collection_path() 里，
   在 otto-gif 分支后面加 fairy 分支（代码见 CMakeLists.append.txt 第 3 步）

c) 仓库根建 fairy-assets/ 放 GIF（见下面 1.7）
```

> ⚠️ ① 那段有 `if(BOARD_DIR STREQUAL "m5stack/core-s3")` 包裹，
> 只对 CoreS3 生效，不影响其他板卡。
>
> ⚠️ **②③ 不做也「能编过」，但舵机/传感器会缺、表情也不对** ——
> 这就是「明明编译成功了，但效果和作者的不一样」最常见的原因。

### 1.5 ★ 设置服务器地址（这里是关键，请读完）

**先讲清一件事：设备「第一次」连哪台服务器，只有两个来源。**

读 `ota.cc` 的取值逻辑（`Ota::GetCheckVersionUrl()`）：

```cpp
std::string url = settings.GetString("ota_url");   // ① 设备 NVS 里存的
if (url.empty()) {
    url = CONFIG_OTA_URL;                          // ② 编译期写进固件的
}
```

⇒ **NVS 是空的（全新设备第一次开机）时，只能用 ②。**

那 NVS 里的地址是怎么写进去的？共有**四个**写入点 ——
其中**只有第 1 个不需要设备先连上任何服务器**，所以只有它能做「首次连接」：

| # | 写入点 | 代码位置 | 前提 |
|---|---|---|---|
| 1 | ★ **设备配网页的「自定义 OTA 地址」** | `esp-wifi-connect` 组件的配网页面 | **无**（连设备热点即可） |
| 2 | 语音说「我的服务器是 …」 | `skin_manager.cc` MCP 工具 `self.server.set_ota_url` | 已连上某台服务器（否则收不到指令） |
| 3 | 网页控制台切皮肤 | `skin_manager.cc` `SaveSkin()` | 已连上你的服务器（否则打不开控制台） |
| 4 | 切皮肤时自动记住 | `ota.cc` → `RememberSelfOtaIfCustom()` | 已连上你的服务器 |

> ⚠️ 第 4 条**只写 `fairy_skin/self_ota`**（用于切回 Fairy 时读回），
> **不写 `wifi/ota_url`**，所以它不能用来做「首次连接」。
>
> ★ 第 1 条为什么算数：配网页里那个输入框保存时写的就是
> **NVS `wifi` 命名空间的 `ota_url` 键**，和固件 `ota.cc` 读的**是同一个键**。


---

#### 方式 A：编译时烧入地址（要装工具链，但一劳永逸）

编辑 `main/boards/m5stack/core-s3/config.json`，在 `sdkconfig_append` 里加一行：

```json
"CONFIG_OTA_URL=\"http://<你的服务器IP>:8003/xiaozhi/ota/\""
```

⇒ 刷完开机就直接连你的服务器，**不需要任何额外操作**。

> ⚠️ 改了 `config.json` 必须**重新 configure + 重新编译**才生效。
> 验证配置真编进去了（Windows）：
> ```powershell
> findstr /C:"<你的IP>" build\merged-binary.bin
> ```
> 搜不到 ⇒ 配置没生效，回去检查。

#### 方式 B（★ 免编译首选）：在设备配网页里填地址

**如果你不想编译固件**（比如直接用 Release 里的通用固件），
这是最干净的一条路 —— **不需要联网、不需要官方服务器、不用念 IP**：

```text
① 让设备进【配网模式】
     全新设备：第一次开机会自动进
     已配网的设备：开机后（状态还在 Starting 时）点一下屏幕；
                   或 WiFi 连不上时也会自动进
② 手机连设备的热点（名字见屏幕提示，形如 Xiaozhi-XXXX）
③ 浏览器打开配置页（屏幕上显示的那个地址）
④ 切到 Advanced 页签（中文界面是「高级选项」）→ 填「自定义 OTA 地址」：
      http://<你的服务器IP>:<http_port>/xiaozhi/ota/
   → 保存
⑤ 回「新 Wi-Fi」页签填你家 WiFi 密码 → 设备重启，直连你的服务器
```

**原理**：配网页面（`esp-wifi-connect` 组件）自带这个「自定义 OTA 地址」输入框，
保存时执行 `nvs_set_str(..., "ota_url", ...)`，写的是 **NVS `wifi` 命名空间**，
和固件 `ota.cc` 读的**是同一个键**。这个框在固件里是**开着的**
（`main/boards/common/wifi_board.cc` 里 `config.show_ota_config = true`）。

> ★ **为什么很多人没见过这个框**：开关默认是关的 —— 组件（`78/esp-wifi-connect`）
> 的 README 写得很清楚：这两个开关 `Both default to false, hiding the
> corresponding fields in the config portal`，要显式打开才会显示
> `Custom OTA URL input`。是 xiaozhi 在 `wifi_board.cc` 里把它打开的；
> 而 xiaozhi 的主文档**没对用户讲这个用法**（官方文档只讲「接官方服务器 +
> xiaozhi.me 控制台」），所以它属于「代码里给了、文档没写」的功能。

#### ★ 「高级选项」页签里的其它三项（一般不用动）

同一个页签里还有三个字段 —— 它们**都不影响服务器连接**，多数人不用管：

| 字段 | 默认 | 说明 |
|---|---|---|
| Wi-Fi 最大发射功率 | 读驱动当前值回填 | 只影响发射功率。想信号更稳选最大的 `20 dBm`（代价：稍耗电、发热略增）。 |
| 记住 BSSID | 不勾（`false`） | 勾了会把设备**锁死到某一个 AP**。家用单路由器无所谓；**Mesh / 多个同名 AP 的环境不要勾**。 |
| 启用睡眠模式 | 勾（`true`） | 允许固件空闲后省电。★ 它写的是**固件自己也在读的同一个 NVS 键**，见下。 |

> ★ **「启用睡眠模式」的准确语义**（以本项目 CoreS3 为例）：
> **开着** = 空闲 300 秒后熄屏省电，碰一下或叫它就回来，**永不自动关机**；
> **关掉** = 屏幕一直亮着，功耗与发热上去、屏幕长期常亮。
>
> 代码依据：`main/boards/m5stack/core-s3/m5stack_core_s3.cc` 里建的是
> `PowerSaveTimer(-1, 300, -1)`；而 `main/boards/common/power_save_timer.cc`
> 的 `SetEnabled()` 会先读 `wifi/sleep_mode`，读到 `false` 就**直接 return**
> （定时器根本不启动）⇒ 屏幕永不熄。别的板卡数值可能不同，语义一致。

> ⚠️ 这一页的「保存」会**真的写进设备 NVS**（地址、功率、BSSID、睡眠都会落盘），
> 所以：**只想看看就别按「保存」**。

#### 方式 C（备选）：对着设备说地址

设备第一次开机会先连官方服务器 `api.tenclass.net`（NVS 空 + 编译期是官方地址）。
此时你可以对它说：

```
「我的服务器是 http://<你的IP>:8003/xiaozhi/ota/」
```

AI 会调设备的本地 MCP 工具 `self.server.set_ota_url`，
**把地址直接写进设备 NVS**（这个工具跑在设备上，不依赖服务器逻辑）。
写完后设备重启，就连上你的服务器了。

> ⚠️ **两个现实问题，所以只当备选：**
>
> ① **你得先有官方服务器可用**（联网、且官方服务正常）—— 离线环境走不通。
> ② **用嘴念 IP 很难念对**。ASR 经常把 `<服务器IP>` 听成别的
>    （「一点五」/「幺五」/「一屋」都可能）。
>    建议**一个数字一个数字念**，或先写在纸上照着念。
>
> ★ **给朋友用**：能帮他编译就方式 A；不想让人装工具链 → 方式 B（填一次地址即可）。

#### ★ 自动记忆机制（它解决的是「切换不丢」，不是「首次怎么连」）

本项目固件里**没有写死任何 IP**。它的设计是：

```
「自建服务器地址」 = 设备当前正在用的那个 OTA 地址
```

读取优先级（`skin_manager.cc` `SelfOtaUrl()`）：

```
① fairy_skin/self_ota   ← 上次切到官方之前记下来的
② wifi/ota_url          ← 设备当前在用的
③ CONFIG_OTA_URL        ← 编译期兜底
```

配合 `ota.cc` 的自动记录：

```cpp
// 设备每次 OTA 都会走这里取地址
std::string Ota::GetCheckVersionUrl() {
    ...
    // 取到的是「自定义地址」⇒ 记进 fairy_skin/self_ota
    // （官方地址不记）⇒ 以后切来切去都不会丢
    stackchan_skin::RememberSelfOtaIfCustom(url);
    return url;
}
```

**⇒ 效果**：你只要让它**成功连上过一次**你的服务器，
之后在「你的服务器」和「官方服务器」之间来回切皮肤，地址都不会丢。

> ⚠️ **顺序很重要**：**先保证能连上你的服务器**（方式 A / B / C 任一），再折腾切换。
> 否则第 ③ 层兜底若是官方地址，切回 Fairy 时会连到官方去。

---

#### ★ 端口是怎么确定的（改过 `http_port` 的人必读）

设备侧**不再按固定 8003 猜端口**，规则如下（`skin_manager.cc`）：

```text
① 每次取地址前，用【设备正在用的那条地址】记下「自建地址」
   （ota.cc → RememberSelfOtaIfCustom，端口来自地址本身 ⇒ 确切值）
② 只有当这个记录还是空的，才会用「服务器下发的 WebSocket 地址」推导；
   而且推导只在【还不知道】时写，绝不覆盖 ① 记下的确切值
③ 推导时的端口顺序：显式配置(fairy_skin/http_port) → 当前生效地址里的端口 → 8003
   ⇒ fairy_skin/http_port 是【保留键】，目前没有代码写它，不用管它
```

> ⇒ **实践建议（推荐做法，不是必须）**：如果你把 `server.http_port` 改成了
> 8003 以外的值，仍然建议用方式 A（`CONFIG_OTA_URL`）把**完整地址**编进固件
> —— 因为「首次连接」只有这三条路：**编译期地址 / 设备配网页填地址 / 语音说地址**，
> 而完整地址（不管哪条路）里都自带端口，设备会原样记住。

> 补充：**服务器侧**（`admin_handler.py`）也会读 `server.http_port`，
> 读不到时会打印明确告警：
> `[自适应提示] 配置里没有 server.http_port，OTA 地址推断将兜底使用 8003`
> ⇒ 看到这条就说明你该去 `data/.config.yaml` 补上 `server.http_port`。

### 1.6 编译 & 烧录

**开始之前（前提 —— 缺一条都会卡住）**

```text
□ 1.1 ~ 1.5 都做完了：上游已拉下来、本包已覆盖（6 步，含唤醒词）、板卡已选对、
   main/CMakeLists.txt 已追加、config.json 里已填好服务器地址（走方式 A 时）
□ ESP-IDF 已装好，并且【在同一个终端里 export 过】（否则 idf.py 不存在）
□ 设备通电、数据线连着电脑，且知道它在哪个串口：
      Windows → 设备管理器 → 端口(COM 和 LPT) → 形如 COM3
      Linux   → ls /dev/ttyUSB*        macOS → ls /dev/cu.*
□ 网络通（编译时会自动下组件）；国内先配好代理
```

**① 装工具链（只装一次）** —— ESP-IDF **v6.1**（本项目实测的版本）

```bash
# Linux / macOS
mkdir -p ~/esp && cd ~/esp
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3 && . ./export.sh
```

> Windows 用**官方安装器**最省事（自带 Python / Git / 驱动）：
> <https://docs.espressif.com/projects/esp-idf/zh_CN/v6.1/esp32s3/get-started/index.html>
> 装完先跑一次 `export.bat`，再开命令行。

**② 配置 → 编译 → 烧录（★ 按这个顺序，别跳步）**

```bash
cd xiaozhi-esp32
idf.py set-target esp32s3      # 生成 sdkconfig（会读 sdkconfig.defaults）
idf.py reconfigure             # ★ 改过 config.json / sdkconfig.defaults 就必须再跑
idf.py build                   # 首次 10~20 分钟（增量 1~3 分钟）
idf.py -p COM3 flash           # 烧进设备，约 1 分钟
idf.py -p COM3 monitor         # 看串口日志；退出按 Ctrl + ]
```

**每步「看到什么」才算过**

```text
set-target   → Target set to 'esp32s3'
reconfigure  → -- Configuring done
build        → 结尾 Project build complete.，且 build/ 下生成了 xiaozhi.bin
校验产物     → python3 tools/verify_artifact.py build/xiaozhi.bin  ⇒ 打印「🎉 全部通过」
               （查双唤醒词 + 两个 MCP 工具 + 无内网 IP + 有没有内嵌表情）
flash        → Hash of data verified. / Leaving... Hard resetting via RTS pin
monitor      → WS: Connecting to ws://<你的服务器IP>:8000/...   ← 这才是连上了
```

> ★ 产物都在 `build/`：`xiaozhi.bin`（应用本体）、`partition-table.bin`、bootloader。
> 想把它们合成**一个整机 bin**（方便给别人刷）用 `idf.py merge-bin`。
> ★ `idf.py flash` 会**自动**让设备进下载模式，**不用手动按任何键**。
> 看不到串口时：先换一根**数据线**、换个 USB 口；仍不行装 M5Stack 官网的 USB 驱动。
> ⚠️ **顺序反了会怎样**：先 `build` 再改 `config.json` ⇒ 改动**不会进固件**，
> 必须 `reconfigure` 后重编 —— 这是「我明明改了却没生效」最常见的原因。

**③ 设备端确认**

```text
屏幕换成 Fairy（或你设的）皮肤；对它说话是人设语气；
服务器控制台能看到设备上线 ⇒ 就成功了。
没成功 ⇒ 先看上面的报错对照，再看第 4 节「常见问题」。
```

#### ⚠️ 编译报 "app partition is too small" 怎么办

```
现象：
  链接成功，最后报
  Error: app partition is too small for binary xiaozhi.bin size 0x2ebbb0
    - Part 'factory' 0/0 @ 0x10000 size 0x100000 (overflow 0x1ebbb0)

原因：
  上游默认用【单 app 分区表】（partitions_singleapp.csv，factory 只有 1MB），
  而本项目的固件约 3MB ⇒ 装不下。

修法：
  上游仓库里有【现成的大分区表】，在 sdkconfig 里指定即可：

      CONFIG_PARTITION_TABLE_CUSTOM=y
      CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m.csv"
      CONFIG_PARTITION_TABLE_OFFSET=0x8000

  ⇒ 或者用交互式配置：
      idf.py menuconfig
      Partition Table → Custom partition table CSV → 填 partitions/v2/16m.csv

  ★ 本项目发布者的 sdkconfig.defaults 里就是这么配的。
```

> ✅ **本项目的发布者已用「干净上游 + 把本包覆盖上去」的方式实测编译通过**
> （2026-09-19，ESP-IDF v6.1）。

#### ⚠️ 常见报错对照

```text
idf.py: command not found
  ⇒ 没跑环境脚本。先 . ./export.sh（Windows：export.bat），再开命令行

undefined reference to `XXX'
  ⇒ 99% 是漏了 1.4（没把 CMakeLists.append.txt 贴进 main/CMakeLists.txt）；
     也和 1.3（板卡没选对）有关 ⇒ 两个都回去检查一遍

串口打不开 / 一直连不上
  ⇒ 端口被占用：关掉串口监视器 / M5Burner / Arduino / 其他串口工具
  ⇒ 再确认用的是【数据线】（不是只能充电的线），或换个 USB 口

卡在下载不动（Cloning into ... / pip install ...）
  ⇒ 网络问题：组件和 Python 依赖要从外网拉 ⇒ 配好代理再试，
     还不行就删掉 build/ 重新编

app partition is too small
  ⇒ 见上面那节（分区表）
```

---

### 1.7 ★ 表情素材（GIF）—— 本仓库【不附带】，请自备

设备「全屏表情」来自 `xiaozhi-esp32/fairy-assets/` 里的 GIF：

```text
fairy-assets/
  idle.gif               待机 / 大多数情绪
  thinking.gif           思考中
  _emote_aliases.json    情绪别名表（脚本会按你的文件名自动生成）
```

**⛔ 本仓库不附带任何 GIF**：Fairy 的美术形象版权属于米哈游
（见 [CREDITS.md](CREDITS.md)），不能随包分发。所以你要知道：

```text
① 不放 GIF   ⇒ 设备用上游默认的彩色 emoji（功能全正常，只是脸不一样）
② 放自己的   ⇒ 把自己做的 / 有权使用的 GIF 起名叫 idle.gif / thinking.gif，
               放进 fairy-assets/，重跑一次 apply_to_upstream.py，重新编译
③ 用上游的   ⇒ 把 main/CMakeLists.txt 里 CoreS3 那段的
               set(DEFAULT_EMOJI_COLLECTION fairy) 改成 otto-gif
```

★ **这就是「编译成功 ≠ 和作者效果一样」的主要差别所在** ——
其它功能（摸头爱心、甩晕、双唤醒词、菜单、皮肤切换）都不依赖 GIF。

★ 写进文档里的判据：编完后 `build/generated_assets.bin` 会比没有表情时大 1 MB 以上；
用 `python3 tools/verify_artifact.py <固件>` 会打印「内嵌 GIF 数量」。

---

## 2. 服务器部分

> ★★ **建议先做这一节（服务器），再做第 1 节（固件）** ——
> 「服务器地址」是后面所有动作的前提：固件里要填的就是它，设备要连的也是它。
> 完整工序与理由见 [1.0 节](#10--建议的工序作者本人的做法供参考)。
> 本文档章节顺序只是便于阅读，**实际动手顺序与之相反。**

### 2.1 拉取上游服务器

```bash
git clone https://github.com/xinnan-tech/xiaozhi-esp32-server.git
```

> ⚠️ 国内直连 github.com 大概率超时 —— 绕法与 [1.1 节](#11-拉取上游固件) 相同
> （换 codeload 下 tarball，或先配代理）。

### 2.2 把本项目的改动覆盖到上游

> **前提**：同 1.2 —— 先 `cd` 到【本项目包】的根目录（`server/patches/` 就在里面）。

```bash
cp server/patches/*.py \
   xiaozhi-esp32-server/main/xiaozhi-server/core/api/
cp server/patches/admin_page.html \
   xiaozhi-esp32-server/main/xiaozhi-server/core/api/
```

### 2.3 配置

按上游文档配置 `data/.config.yaml`，至少需要：

```yaml
server:
  # ★★ 【必填】JWT 密钥 —— 不填会影响「设备拍照识图」
  #    真因：视觉接口 /mcp/vision/explain 用 JWT 鉴权，密钥取自这里；
  #          若这一项为空，服务器【每次启动都会随机生成一个新密钥】，
  #          设备缓存的旧 token 立刻失效 ⇒ 拍照回复 "Failed to upload photo"。
  #    ⇒ 固定写一串（≥32 位）就一劳永逸。
  auth_key: <一串 32 位以上的随机字符串>
  http_port: 8003          # Web 控制台端口（默认 8003）

LLM:
  # ★ 节点名要与 server.selected_module 里配的一致
  #   本示例用 DeepSeekLLM（本项目的控制台按这个节点名读写）
  DeepSeekLLM:
    api_key: <你的 Key>
    model_name: deepseek-chat
    base_url: https://api.deepseek.com

VLLM:                    # ★ 视觉模型（设备「看图说话」用，可与 LLM 不同服务商）
  DeepSeekVLLM:
    api_key: <你的 Key>
    model_name: deepseek-chat
    base_url: https://api.deepseek.com

TTS:
  # 若用 GPT-SoVITS：
  GPT_SOVITS_V2:
    api_url: http://127.0.0.1:9880
    ref_audio_path: <你的参考音频.wav>      # 见 voice-package/README.md
    prompt_text: <参考音频对应的文字>
```

> ⚠️ **上面只是示例**，不是唯一写法。你可以换成任何兼容 OpenAI 协议的
> 模型/服务商（阿里、智谱、本地 Ollama 等）。
>
> ★ **但有一个前提**：本项目自带的 Web 控制台（`/admin`、`/m`）
> **按固定节点名读写配置** —— 目前是：
>
> ```
> LLM   → LLM.DeepSeekLLM
> VLLM  → VLLM.DeepSeekVLLM
> TTS   → TTS.GPT_SOVITS_V2
> ```
>
> ⇒ 如果你在 `selected_module` 里选的是**别的节点名**（如 `ChatGLMLLM`），
> 控制台的「大模型 / 音色」页会读不到值（显示为空），保存时也会写到上面那几个
> 固定节点里。
>
> ⇒ **两个办法**：① 沿用上面的节点名（最省事）；
> ② 想用别的节点名，就直接改 `data/.config.yaml`，
> 不走控制台改这几项（控制台的其他功能仍可正常用）。
>
> ⇒ 想彻底解决，可改 `server/patches/admin_handler.py` 里的节点名常量。

**生成 `auth_key` 的方法（任选）：**

```bash
python -c "import secrets; print(secrets.token_hex(32))"
# 或直接用一串你随便敲的长字符串（≥32 位即可）
```

> ★ 这个 `auth_key` **不是**任何第三方服务的 Key，只是你自己服务器内部的签名密钥，
> 随便写、写死就行，不要留空。

### 2.4 启动

```bash
cd xiaozhi-esp32-server/main/xiaozhi-server
pip install -r requirements.txt      # ★ 首次必须先装依赖（建议先建虚拟环境）
python app.py
```

> ★ 装依赖同样要联网（PyPI）。国内慢就挂镜像：
> `pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple`

启动后浏览器打开（**8003 是默认端口，可在 `data/.config.yaml` 的
`server.http_port` 改；服务器侧代码会读这个配置，不写死**）：

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
（★ 上面是 esptool v4 及更早的写法；**v5 已改成子命令形式**，见下面「本项目在用的写法」）

**本项目在用的写法（esptool v5）**：

```bash
python -m esptool --chip esp32s3 -p <串口> -b 460800 read-flash 0 0x1000000 factory-backup.bin
```

恢复：

```bash
esptool.py --port <串口> write_flash 0x0 factory-backup.bin
```
（v5 的等价写法：`python -m esptool --chip esp32s3 -p <串口> write-flash 0x0 factory-backup.bin`）
