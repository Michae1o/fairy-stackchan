# 固件改动说明（firmware/board-core-s3/）

覆盖目标：`xiaozhi-esp32/main/boards/m5stack/core-s3/`

---

## 目录

```text
① 文件清单              —— 改/加了哪些文件，各自放哪、干什么
② 关键实现要点          —— 六个最容易踩的坑（LVGL 输入设备 / 装饰器 update / 加锁 / BMI270 0x69 / 唤醒词 / 两套 SetupUI）
③ 编译                  —— 怎么编（详细 SOP 在 ../INSTALL.md）
④ 双皮肤怎么实现的       —— 原理、数据结构、改成你自己的两套、地址自动记忆、排查
⑤ 表情素材              —— 规格要求 + 从矢量图/代码导出「设备能吃」的素材
⑥ 形象相关（★ 重点）     —— 你设备上那两套脸分别是什么、本仓库给了什么、怎么改
⑦ 扩展情绪 / 表情        —— 几何脸的 6 种情绪怎么加
```

---

## 文件清单

> 完整目录结构见 `INSTALL.md` 第 1.2 节（6 步：5 个覆盖位置 + 唤醒词）。

### 本项目核心改动（`board-core-s3/`）

| 文件 | 作用 |
|---|---|
| `m5stack_core_s3.cc` | **板卡主文件**（★ 改动最大：上游 12KB → 本项目 54KB）。含：状态栏（左 WiFi / 右电量）、LVGL 触摸输入、说话嘴动画驱动、待机转头、传感器反应（摸头/甩晕）、皮肤按钮接线 |
| `skin_manager.{cc,h}` | **双皮肤管理**。切皮肤 = 写 NVS + 换 OTA 地址 + 重启；OTA 地址**自动识别**（不写死 IP） |
| `stackchan_geometry_display.{cc,h}` | **官方几何脸皮肤**。含：情绪映射、说话嘴开合、爱心/晕眩装饰器、`AvatarTick()` 驱动 |
| `stackchan_sensor.{cc,h}` | **传感器封装**。SI12T 触摸（0x68）+ BMI270 摇晃（**0x69**） |
| `cores3_py32_led.{cc,h}` | **12 颗 RGB**。挂在 PY32 IO 扩展上，支持每状态「常亮/呼吸」可选 |
| `cores3_servo.{cc,h}` | **飞特舵机**（SCSCL 协议，UART1 / 1Mbps） |
| `PY32IOExpander_Class.{cpp,hpp}` | PY32 IO 扩展驱动（★ 上游没有，后缀是 .cpp 需显式加进 CMake） |
| `config.json` | 板卡配置。**与上游相同**，保留它是因为你要在这里填自己的 `CONFIG_OTA_URL` |

### 放在别处（★ 位置很重要，放错就编不过）

| 位置 | 文件 | 为什么不能放板卡目录 |
|---|---|---|
| `main/boards/common/` | `i2c_device.{h,cc}` | ★ 上游没有这两个文件；本项目加了**不 abort** 的 `TryReadRegs` |
| `main/display/` | `lcd_display.{cc,h}` | 上游 `main/display/` 已存在同名文件，代码里是 `#include "display/lcd_display.h"` |
| `main/` | `ota.cc` | 固件版本检查 + 本项目的"记住自建地址" |
| `components/` | `smooth_ui_toolkit` 等 | ★ 上游没有 `components/` 目录；本地组件 |

### 从官方 StackChan / Bosch 移植的驱动（都在 `board-core-s3/` 下）

```
drivers/bmi270/            加速度计（含 Bosch SensorAPI，保留原 LICENSE / BSD-3）
drivers/Si12T/             触摸芯片
drivers/motion_detector/   摇晃判定算法
stackchan_avatar/          几何脸（眼睛/嘴/气泡 + 5 个装饰器 + 图片素材）
FTServo/                   飞特舵机库
```

---

## 关键实现要点（容易踩坑的地方）

### ① LVGL 需要注册输入设备才能收到点击

```cpp
// 没有这段，所有 LVGL 按钮都点不动（本项目踩过）
lv_indev_t* indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, LvglTouchReadCb);
lv_indev_set_group(indev, lv_group_get_default());      // ← 别漏
lv_indev_set_display(indev, lv_display_get_default());  // ← 别漏
```

`read_cb` 运行在 **LVGL 任务上下文**里 ⇒ 只能用缓存坐标，**不能做 I2C 读**。

### ② 几何脸装饰器必须被 update() 驱动

```cpp
// avatar->update() 干三件事：
//   ① 更新 key_elements（眨眼/呼吸）
//   ② 更新 decorators（爱心跳动 / 晕眩转圈）
//   ③ cleanup() —— ★ 到期的装饰器在这里销毁
```

**不调 `avatar->update()` 的后果**（本项目实测过）：
- 爱心/晕眩静止不动（看起来"很烂"）
- 装饰器永不过期 ⇒ **一直盖住表情** ⇒ 表情再也不变

官方在 `stackchan.h` 主循环里调；本项目挂在 20ms 轮询定时器上。

### ③ 所有 LVGL 操作都要加锁

LVGL 非线程安全。本项目用 `DisplayLockGuard lock(display_)`。
定时器回调里操作 LVGL 尤其要加锁，否则**菜单会卡死**（点不动也关不掉）。

### ④ BMI270 的 I2C 地址是 0x69，不是 0x68

```cpp
#define BMI270_ADDR 0x69   // ★ SDO 接 VCC
```

SI12T 是 `0x68`。**两者不冲突，可以同时使用**。
（官方 `hal_imu.cpp` 用的就是 0x69。）

### ⑤ 双唤醒词要写进 `sdkconfig.defaults.esp32s3`

> ★ 安装步骤里已包含这一步（`INSTALL.md` 1.2 第 ⑥ 步）——
> 上游默认已带「你好小智」，所以只需补下面 Hi Fairy 那一行。

```
CONFIG_SR_WN_WN9_HIFAIRY_TTS2=y
CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y
```

⛔ **不要写进 `sdkconfig`** —— `scripts/build.py` 每次构建都会把它挪成
`sdkconfig.old` 再重建 ⇒ 手写的改动会丢。

### ⑥ 条件编译有两套 SetupUI

`lcd_display.cc` 里 `LcdDisplay::SetupUI()` 有两份，由
`CONFIG_USE_WECHAT_MESSAGE_STYLE` 二选一。
改状态栏时**确认改对了分支**（本项目曾改错分支 ⇒ UI 完全不变）。

---

## 编译

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <串口> flash
```

首次编译约 10~20 分钟（视机器）。
产物：`build/merged-binary.bin`（可直接用 esptool 烧 0x0）

---

## 双皮肤是怎么实现的（★ 改造成你自己的两套）

**这一节教你怎么让「两套形象」各自连「不同的服务器」。**

核心文件：`skin_manager.cc`（板卡目录下）

### 原理（一句话）

```
皮肤 = 形象 + 服务器地址
切皮肤 ⇔ 换 OTA 地址 ⇔ 设备重启后去连另一台服务器
```

设备每次开机/联网都会走 **OTA** 流程（向 OTA 地址请求配置），
所以"设备连谁"完全由 **OTA 地址**决定。

### 数据结构

```cpp
enum class Skin {
    Fairy = 0,      // 默认皮肤，示例里连【你自己的服务器】
    Geometry = 1,   // 第二套皮肤，示例里连【官方服务器】
};
```

两套皮肤各自绑定一个 OTA 地址：

```cpp
// 官方（固定值）
const char* kOtaUrlOfficial = "https://api.tenclass.net/xiaozhi/ota/";

// 自建（★ 自动识别，见下）
static std::string SelfOtaUrl();

// 分发：皮肤 → 地址
static std::string OtaUrlFor(Skin s) {
    return (s == Skin::Geometry) ? std::string(kOtaUrlOfficial)
                                 : SelfOtaUrl();
}
```

### 你要做的 3 步

**第 1 步：让"自建地址"有值**（这一步做完，切换才不会跑偏）

这是**整个项目最关键的一步** —— 先讲清原理：

```text
设备「第一次」连哪台服务器，只有两个来源（见 ota.cc）：

    ① 设备 NVS 里存的 ota_url      —— 全新设备是【空的】
    ② 编译期写进固件的 CONFIG_OTA_URL —— 通用固件是【官方默认地址】

⇒ 所以：全新设备 + 通用固件 = 第一次开机【必然先连官方服务器】
```

**能写 NVS 的入口有四个 —— 只有第 1 个不需要设备先连上任何服务器：**

| # | 写入方式 | 前提 |
|---|---|---|
| 1 | ★ **设备配网页的「自定义 OTA 地址」** | **无**（手机连设备热点即可） |
| 2 | 语音说「我的服务器是 …」 | 已连上某台服务器（否则收不到这句话） |
| 3 | 网页控制台切皮肤 | 已连上**你的**服务器（否则打不开控制台） |
| 4 | 切皮肤时自动记住 | 已连上你的服务器 |

⇒ **结论：「通用固件 + 在设备配网页里填地址」也能做到刷完直连你的服务器。**

**三种做法：**

```text
方式 A（要编译）：编译时写进 config.json
    main/boards/m5stack/core-s3/config.json 的 sdkconfig_append 里加：
      "CONFIG_OTA_URL=\"http://<你的服务器IP>:8003/xiaozhi/ota/\""
    ⇒ 改完必须【重新 configure + 重新编译】才生效

方式 B（免编译）：在设备配网页里填地址
    ① 设备进配网模式（首次开机自动进；已配网的设备开机后点一下屏幕）
    ② 手机连设备热点 → 浏览器打开配置页
    ③ Advanced 页签（中文界面是「高级选项」）→ 填「自定义 OTA 地址」：
         http://<你的服务器IP>:<http_port>/xiaozhi/ota/   → 保存
    ④ 回「新 Wi-Fi」页签填 WiFi 密码 → 设备重启，直连你的服务器
    ⇒ 不需要联网、不需要官方服务器、不用念 IP

方式 C（备选）：先连官方，再对它说
      「我的服务器是 http://<你的IP>:8003/xiaozhi/ota/」
    ⇒ AI 调设备的本地 MCP 工具 self.server.set_ota_url，把地址直接写进设备 NVS
    ⚠️ 前提：官方服务器可用（离线走不通）；且念 IP 很容易念错
```

> ⛔ **注意：不存在「在自建服务器的 Web 控制台里填地址」这条路** ——
> 控制台是**你自建服务器**上的页面，设备连不上你的服务器就打不开它。
> ★ 但**设备自己的配网页**里确实有这个输入框（见方式 B），
> 别把这两个页面搞混。

> ★ **改了 `server.http_port` 的话**：设备**不会固定按 8003 猜端口** ——
> 它用【正在用的那条地址】记下自建地址（端口来自地址本身），
> 推导只在没有记录时发生、且不覆盖确切值（`skin_manager.cc`）。
> 不过仍建议用方式 A / B 给设备**完整地址**
> （首次连接就三条路：编译期地址 / 配网页填地址 / 语音说地址）。

**第 2 步：改皮肤枚举名/数量**（可选，默认两套）

**第 3 步：给每套配形象素材 + 服务器地址**

详见下面「改成你自己的两套主题」。

### ★ 核心机制：自建地址是"自动记住"的（不用你写死）

设备切皮肤时会往 NVS 写一个新 OTA 地址。"自建地址"从哪来？
按优先级三层：

```
① fairy_skin/self_ota   ← 上次记下来的（最可靠）
② wifi/ota_url          ← 设备当前在用的
③ CONFIG_OTA_URL        ← 编译期兜底
```

**为了让你不用写死 IP**，本项目加了自动记忆（`ota.cc`）：

```cpp
// 设备每次 OTA 都会走这里取地址
std::string Ota::GetCheckVersionUrl() {
    ...
    // 取到的是"自定义地址" ⇒ 记进 fairy_skin/self_ota
    // （官方地址不记）⇒ 以后切来切去都不会丢
    stackchan_skin::RememberSelfOtaIfCustom(url);
    return url;
}
```

⇒ 效果：**你只要让它成功连上过一次你的服务器，之后就永远记得**，
在"你的服务器"和"官方服务器"之间来回切都不会丢地址。

> ⚠️ 顺序很重要：**先保证能连上你的服务器**（第 1 步），再折腾切换。
> 否则第 ③ 层兜底若等于官方地址，切回自建皮肤时会连到官方去。

### ⚠️ 排查：切回自建皮肤，却连了官方服务器

**表现**：形象是对的，但声音/人设变成官方的；设备日志正常（`skin saved` 成功）。

按顺序查这三处：

```
① config.json 里 CONFIG_OTA_URL 是否还在、且指向【你自己】的服务器
      ⛔ 若为空/被删 ⇒ 回落上游默认，必然连官方
      ⛔ 改了 config.json 必须【重新 configure + 重新编译】才生效

② 编译产物里是否真带上你的地址（验证配置真的编进去了）
      Windows:  findstr /C:"<你的IP>" build\merged-binary.bin
      Linux  :  grep -c "<你的IP>" build/merged-binary.bin
      搜不到 ⇒ 配置没生效，回 ①

③ 看设备串口日志里的实际地址
      SkinManager: skin saved: <皮肤名>, ota_url=<实际地址>
      ota_url 若是官方 ⇒ NVS 里的旧值在作怪：
      连上一次你的服务器即可自动覆盖（见上面的自动记忆机制）
```

### ★ 改成你自己的两套主题

**第 1 步**：改枚举名（可选）

```cpp
enum class Skin {
    MyCharA = 0,    // 你的角色 A
    MyCharB = 1,    // 你的角色 B
};
```
> 改完要同步改 `SkinName()` / `SwitchSkinByName()` / `OtaUrlFor()`

**第 2 步**：给每套配素材和服务器

```
皮肤 A（自建服务器）：
  · 形象：替换 fairy-assets/ 里的 GIF（★ 完整参数见下方「硬参数表」）
  · 服务器：自建（就是 SelfOtaUrl() 自动识别的那个）

皮肤 B（另一套）：
  · 形象：用官方几何脸（本项目已移植），或你自己画
  · 服务器：改成你要的地址
```

---

#### ★★ 表情素材（GIF）的硬参数表（★ 使用者最容易卡死的地方）

> 上面说「替换素材」，但**具体放哪、几张、多大、什么名字、什么格式**？
> 下面这张表是**照真实代码填的**，照做就能塞进去。

| 项 | 值 | 说明 |
|---|---|---|
| **放置目录** | `xiaozhi-esp32/fairy-assets/` | **仓库根目录**（不是 `main/` 下面，也不是 managed_components） |
| **加载方式** | `DEFAULT_EMOJI_COLLECTION = fairy` | 在 `main/CMakeLists.txt` 的 core-s3 分支里 |
| **解析代码** | `scripts/build_default_assets.py` → `get_emoji_collection_path()` 的 `fairy` 分支 | ★ 本项目加的分支，把 "fairy" 这个名字映射到 `fairy-assets/` |
| **文件格式** | `.gif` 或 `.png` | 见 `process_emoji_collection()`：`file.lower().endswith(('.png','.gif'))` |
| **分辨率** | **320 × 240** | 屏幕分辨率（全屏显示）。本项目两个 GIF 都是这个尺寸 |
| **张数 / 状态** | 本项目 **2 个文件**：`idle.gif`（待机）、`thinking.gif`（思考） | ★ 但通过别名表覆盖 **23 种情绪**（见下） |
| **文件名** | 文件名（去掉扩展名）**自动成为情绪名** | 例：`happy.gif` ⇒ 情绪名 `happy` |
| **别名的意义** | `_emote_aliases.json` 让**同一个 GIF 注册成多个情绪名** | ★ 省分区空间（本项目 23 个情绪只用了 2 个 GIF） |
| **总大小上限** | **8 MB**（`assets` 分区，见 `partitions/v2/16m.csv`） | 本项目 2 个 GIF 合计 ≈ 2.1 MB |
| **要不要转 C 数组** | ❌ **不用** | 直接放 `.gif`/`.png`，编译脚本会打进 `assets.bin` |
| **转换脚本** | 无（自动） | `scripts/build_default_assets.py` 在 `idf.py build` 时自动执行 |

> ★ **别和几何脸搞混（两套皮肤各认各的）**：
> · **几何脸**（官方皮肤）只认 **6 种**情绪：`neutral / happy / angry / sad / doubt / sleepy`
> · **Fairy GIF 表情包**认上面这 **23 种**情绪名（靠别名表映射到 2 个 GIF）
> ⇒ 同一个情绪词，在两套皮肤下的表现不一定都有 —— 这是设计如此，不是 bug。

**`_emote_aliases.json` 的格式（本项目实际内容）：**

```json
{
  "idle.gif": ["angry","confident","confused","cool","crying","delicious",
               "embarrassed","funny","happy","idle","kissy","laughing",
               "listening","loving","neutral","relaxed","sad","shocked",
               "silly","sleepy","speaking","surprised","winking"],
  "thinking.gif": ["thinking"]
}
```

### 从矢量图 / 代码导出「设备能吃」的素材

上面那几节讲的是**收货标准**（设备要什么）。如果你是用代码 / 矢量图画形象的
（见 [CREDITS.md](../CREDITS.md) 三之二），这里补上**中间那一步**：

```text
① 画：用什么工具都行（矢量、代码生成、手绘……）—— 不指定工具
② 导出成设备认的格式：**.gif 或 .png**（其它格式不会被加载）
     · 尺寸 **320 × 240**（屏幕全屏尺寸；大了会缩、小了留边）
     · 背景：不透明最省事；要透明就用 GIF 的透明索引色
     · 动图：**自己带循环**（GIF 循环次数设成无限）——
       GIF 是逐帧动画，设备不会补帧；帧率在导出时定好即可
     · 体积：单张 1 MB 左右最稳；合计 ≤ 8 MB（assets 分区上限）
③ 命名：文件名（去扩展名）就是情绪名；想一个文件顶多个情绪，
     写进 _emote_aliases.json（见上一节）
④ 放：`<上游>/fairy-assets/`，然后重跑 `tools/apply_to_upstream.py`
     （它会重写别名表，并把表情包名改成 `fairy`）
⑤ 自检：编译后跑 `python3 tools/verify_artifact.py build/xiaozhi.bin`
     ⇒ 「内嵌 GIF 数量」> 0 才算素材真打进去了
```

> ★ 设备端**不解析 SVG / 矢量**：SVG 只在你自己画的时候有用，
> 最终必须导出成位图 GIF/PNG 才能进 `fairy-assets/`。
> ★ 不用素材也有替代：把 `set(DEFAULT_EMOJI_COLLECTION fairy)` 改回
> `noto-color-emoji_64`（上游自带的彩色 emoji），或改成 `otto-gif`
> （上游自带的表情包）—— 用哪种由你决定。

> 键 = **GIF 文件名**（含扩展名）；值 = **要注册的情绪名数组**。

**代码侧怎么取用（`lcd_display.cc`）：**

```cpp
// 服务器下发情绪字符串 ⇒ 按名字查表 ⇒ 取到对应的 GIF
auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
auto image = emoji_collection->GetEmojiImage(emotion);   // emotion 就是别名
⇒ 查不到时打日志 "Emoji not found: xxx"（见 emoji_collection.cc L17）
```

**★ 完整操作步骤（3 步）：**

```bash
# ① 建目录（仓库根目录，与 main/ 同级）
mkdir xiaozhi-esp32/fairy-assets

# ② 放素材（320×240 的 GIF，名字随意 —— 名字会成为情绪名）
cp 你的形象.gif      xiaozhi-esp32/fairy-assets/
cp 你的思考.gif      xiaozhi-esp32/fairy-assets/
# 如需一个 GIF 覆盖多个情绪，写 _emote_aliases.json（见上）

# ③ 在 main/CMakeLists.txt 的 core-s3 分支改一行
#    set(DEFAULT_EMOJI_COLLECTION noto-color-emoji_64)   ← 原值
     set(DEFAULT_EMOJI_COLLECTION fairy)                  ← 改成这样
#    并在 scripts/build_default_assets.py 加 fairy 分支（见 CMakeLists.append.txt 第 3 步）

# 然后正常编译即可（会自动打包，无需手动转格式）
idf.py build
```

**⚠️ 常见现象：**

```
· 日志 "Emoji not found: happy"     ⇒ 别名表里没有 happy，或 GIF 文件名与之不匹配
· 日志 "Fairy emoji collection not found"
                                    ⇒ 仓库根目录没有 fairy-assets/
· 表情一直是普通脸                   ⇒ _emote_aliases.json 的键没写对（键必须是【含扩展名的文件名】）
· 编译报 assets 分区超了             ⇒ 素材总和 > 8MB（压缩 GIF / 减帧 / 降分辨率）
```

> ⛔ **本项目不附带任何 GIF**（Fairy 形象版权属米哈游，见 [../CREDITS.md](../CREDITS.md)）。
> ⇒ 你要么**自己画**（零版权风险），要么去 CREDITS.md 里说的网盘获取。
> ⇒ 这份表就是为了让你能把它**塞进去**；合法性那部分看 CREDITS.md。

**第 3 步**：改触发方式（可选）

现有**两种**切换方式（都写在 `m5stack_core_s3.cc` 和 `skin_manager.cc`）：
```cpp
· 电源键短按 → PollPowerKey() 里（★ 推荐，断网也能用）
· 语音       → skin_manager.cc 的 MCP 工具 self.skin.set
```

> ⚠️ **早期版本还有第三种「状态栏 ☰ 菜单」**，因触摸热区太小、体验差，
> **已整体移除**（相关代码也删干净了）。想加回设备端菜单，
> 参考 `MenuBuild()` 的思路自己接一个入口即可。

---

## ★ 形象相关：两套脸分别在哪儿、本仓库给了什么

> ⚠️ **先把话说清楚**（这一节以前写得容易让人误解）：
> 本仓库**不含 Fairy 形象素材**（唯一的图片是控制台界面截图 `docs/console-home.png`，
> 那是文档配图、不是素材）；但**「用代码画 Fairy」的实现是有的** —— 见下面的 ③。
> 本仓库真正给的是三样东西：
>
> ```text
> ① 接口与参数：设备要什么素材、放在哪、怎么改（本节 + INSTALL 1.7）
> ② 一个现成的、代码画的脸：官方几何脸（M5Stack，MIT）—— 改常量就能改样子（5.2 / 5.3）
> ③ 「用代码画你自己的脸」：现成实现是 [`tools/make_face.py`](../tools/make_face.py)，
>    思路与参考见 CREDITS.md 三之二（含版权边界）
> ```
>
> ⇒ 想要「和作者设备上**一样**的 Fairy 脸」，只能**自备素材**
> （作者那两张 GIF 涉及角色版权，不在仓库里）。

### 5.1 你设备上的两套脸，分别是怎么来的

```text
【Fairy 皮肤】显示 → 全屏 GIF 表情包
    素材：xiaozhi-esp32/fairy-assets/（★ 仓库不附带，见 INSTALL 1.7）
    机制：main/CMakeLists.txt 里 set(DEFAULT_EMOJI_COLLECTION fairy)
          + scripts/build_default_assets.py 的 fairy 分支
          + _emote_aliases.json（情绪名 → GIF 文件）

【几何皮肤】显示 → 代码画的官方几何脸（不是图片）
    素材：firmware/board-core-s3/stackchan_avatar/（M5Stack 官方，MIT）
    机制：stackchan_geometry_display.cc + DefaultAvatar，
          眼睛/嘴/说话气泡都是 LVGL 图元 + 常量画出来的
    切换：m5stack_core_s3.cc 里 geometry_display_->SetGeometryVisible(geom)，
          其中 geom = (skin == Skin::Geometry)
          ⇒ ★ 也就是说：**Fairy 皮肤下几何脸是藏起来的**
```

**⇒ 想改「Fairy 那套脸」= 换 `fairy-assets/` 里的 GIF；**
**⇒ 想改「几何脸」= 改 `skins/default/` 里的常量（见 5.2 / 5.3）。**

### 5.2 几何脸的文件在哪（★ 改几何脸就从这几个文件下手）

```
firmware/board-core-s3/stackchan_avatar/
├── skins/default/            ← ★ 几何脸就在这里（目录名沿用了官方的 "default"）
│   ├── default.h             主类（DefaultAvatar / DefaultEyes / ...）
│   ├── default.cpp
│   ├── eyes.cpp              ★ 眼睛：位置 / 大小 / 偏移 / 眨眼
│   ├── mouth.cpp             ★ 嘴：位置 / 大小 / 圆角 / 开合
│   └── speech_bubble.cpp     说话气泡：位置 / 尺寸 / 箭头
├── avatar/
│   ├── avatar.h              形象基类
│   └── elements/
│       ├── emotion.h         ★ 情绪枚举（6 种）
│       ├── element.h         元素基类
│       └── feature.h         特征接口（setPosition/setSize/setEmotion...）
└── decorators/               ★ 装饰器（摸头冒爱心、甩晕等）
    ├── heart.cpp / dizzy.cpp / angry.cpp / shy.cpp / sweat.cpp
    └── assets/*.c            装饰器的图片数据（★ 这里是唯一含位图的地方）
```

**⇒ 改几何脸 = 改 `skins/default/` 那几个 `.cpp` 里的常量（参数见下一节）。**

### 5.3 几何脸的参数（★ 官方默认值，本项目**没改过**）

> ⚠️ **这些是「官方几何脸」的官方默认值**，本项目**没有改过**——
> 我们只是把默认皮肤当成“几何脸 + 自定义装饰器”来用。
> 之所以列出来，是因为**改形象就是从这几个常量下手**，给你个起点。

**眼睛（`skins/default/eyes.cpp` L12-16）**

```cpp
static const int      _eye_size            = 16;             // 眼球直径
static const Vector2i _eye_pos        = Vector2i(-70, -16);  // 眼睛基准位置
static const Vector2i _eye_min_offset = Vector2i(-16, -16);  // 眼球能偏的最小量（看的方向）
static const Vector2i _eye_max_offset = Vector2i( 16,  16);  // 眼球能偏的最大量
static const Vector2i _eye_size_limit = Vector2i(  8,  32);  // 眼球缩放范围（8~32）
```

**嘴（`skins/default/mouth.cpp` L12-18）**

```cpp
static const Vector2i _mouth_pos        = Vector2i( 0, 26);  // 嘴的中心位置
static const Vector2i _mouth_min_offset = Vector2i(-16, -16);
static const Vector2i _mouth_max_offset = Vector2i( 16,  16);
static const Vector2i _mouth_min_size   = Vector2i( 90,  6); // 闭嘴尺寸（宽 90 / 高 6）
static const Vector2i _mouth_max_size   = Vector2i( 60, 50); // 张嘴尺寸（宽 60 / 高 50）
static const int      _mouth_min_radius = 0;                 // 嘴角圆度（闭嘴）
static const int      _mouth_max_radius = 16;                // 嘴角圆度（张嘴）
```

**说话气泡（`skins/default/speech_bubble.cpp` L12-20）**

```cpp
static const Vector2i _container_pos  = Vector2i(  0,  89);  // 气泡容器位置
static const Vector2i _container_size = Vector2i(320,  74);  // 容器尺寸（屏宽 320）
static const Vector2i _arrow_offset   = Vector2i( 40, -15);  // 气泡小尖角
static const int      _text_mx             = 20;             // 文字左右边距
static const int      _bubble_min_width    = 90;
static const int      _bubble_max_width    = 340;
static const int      _bubble_height       = 52;
static const int      _bubble_min_offset_x = 66;
static const int      _bubble_max_offset_x = 0;
```

**颜色（`skins/default/default.h` L21-22）**

```cpp
lv_color_t primaryColor   = lv_color_white();   // 前景（脸/眼）
lv_color_t secondaryColor = lv_color_black();   // 背景
```

**⇒ 想改成你自己的形象**：改上面这些数值 + 换 `decorators/assets/*.c`（装饰器图片）。
屏幕是 320×240，坐标以屏幕中心为原点。

> ★ **注意**：`skins/default/` 里**还有一层 `assets/`**（气泡箭头等小图）。
> 如果你要完全替换形象，把那几个 `.c` 一起换掉（它们只是 LV_IMAGE 数组，不是照片）。

### 5.4 为什么目录叫 "default" 而不叫 "fairy"

```
官方的默认皮肤类就叫 DefaultAvatar（DefaultEyes / DefaultMouth ...）
本项目直接沿用了它，没有另起一套类 —— 所以目录名还是 default/。

★ 好处：上游更新时不会冲突（我们没新增皮肤类）
★ 代价：看目录名不知道这是 Fairy 皮肤，得进 eyes.cpp/mouth.cpp 才明白
```

**⇒ 如果你想让它叫 `fairy/`**：把 `skins/default/` 复制成 `skins/fairy/`，
改类名 + 在 `stackchan_geometry_display.cc` 里 include 新路径即可（注意两个地方都要改）。

### 5.5 ★ 「完整角色」= 上面两套脸之一 + 人设 + 音色

```
「Fairy 效果」= 固件形象 + 服务器人设 + 音色参考
                 ↓            ↓            ↓
            几何脸皮肤    prompt 文字   GPT-SoVITS
             + 装饰器     （人设）      ref_audio
```

**① 形象（固件侧）—— 上面两套脸任选其一**

```
· Fairy 皮肤  ：fairy-assets/*.gif + _emote_aliases.json（★ 素材要自备）
· 几何皮肤    ：skins/default/{eyes,mouth,speech_bubble}.cpp（参数见 5.3）
                decorators/{heart,dizzy,...}.cpp + assets/  ← 摸头冒爱心、甩晕转圈
                stackchan_geometry_display.cc L47-52        ← ★ 情绪字符串 → 枚举 映射
```

情绪映射长这样（想加情绪就改这里）：

```cpp
// stackchan_geometry_display.cc
if (strcmp(norm, "happy")  == 0) return Emotion::Happy;
if (strcmp(norm, "angry")  == 0) return Emotion::Angry;
if (strcmp(norm, "sad")    == 0) return Emotion::Sad;
if (strcmp(norm, "doubt")  == 0) return Emotion::Doubt;
if (strcmp(norm, "sleepy") == 0) return Emotion::Sleepy;
return Emotion::Neutral;      // ★ 认不出来的词 ⇒ 一律 Neutral
```

**② 人设（服务器侧，不在固件里）**

```
data/.config.yaml → LLM.<模块>.prompt     ← ★ 决定说话的风格/身份/边界
```

**③ 音色（服务器侧）**

```yaml
# data/.config.yaml
TTS:
  ref_audio_path: <你的参考音频.wav>       # ★ 音色来源
  prompt_text:    <参考音频对应的文字>      # ★ 必须与音频内容一致
  prompt_lang:    zh
```

> ★ **所以「换 Fairy」不是改一个地方**：
> 形象在固件、人设在 prompt、音色在 TTS 配置。三处都换了才是完整的「换角色」。
> 只换形象 ⇒ 脸是 Fairy 但说话像助手；只换 prompt ⇒ 性格对但脸是几何脸。

### 5.6 ⚠️ 本仓库**没有**的（别找了）

```
✘ 没有 Fairy 的形象素材 —— 但**给了「用代码画」的完整实现**：
  `tools/make_face.py`（六层同心环 + 光晕 + 双高光 + 呼吸动画，参数在文件顶部，改几行就能改样子）
✘ 没有独立的 FairyAvatar 类（几何脸直接用了官方的 DefaultAvatar）
✘ 几何脸不是逐帧图（它是 LVGL 图元画的）；Fairy 那套才是 GIF，但那套素材不随仓库
✘ 没做官方皮肤的「美化」—— 几何脸部分只加了装饰器（爱心/晕眩/生气/害羞/流汗）
```

---

## 扩展情绪 / 表情（★ 官方皮肤部分我们没做细）

### 现有情绪（6 个）

```cpp
// stackchan_avatar/avatar/elements/emotion.h
enum class Emotion {
    Neutral = 0, Happy, Angry, Sad, Doubt, Sleepy,
};
```

字符串 → 枚举的映射在 `stackchan_geometry_display.cc`：
```cpp
if (strcmp(norm, "happy") == 0) return Emotion::Happy;
if (strcmp(norm, "angry") == 0) return Emotion::Angry;
// ... 想加就照抄
```

### 怎么自己加一个情绪

```
① 在 emotion.h 里加枚举值（如 Surprised）
② 在 stackchan_geometry_display.cc 的映射里加一行
③ 在 skins/default/ 里实现它的呈现：
     · eyes.cpp   —— 眼睛形状
     · mouth.cpp  —— 嘴形状（DefaultMouth::setWeight(0~100)）
④ （可选）加装饰器：decorators/ 下照 dirty/heart 写一个，
   然后在 _update() 里做动画，由 avatar->update() 驱动
```

> ⚠️ **注意**：`avatar->update()` 必须被每帧调用（本项目挂在 20ms 定时器
> 的 `AvatarTick()` 上）—— 不调的话装饰器不动、也不会自动销毁，
> 会一直盖住表情。这是我们踩过的坑。

### 现有装饰器（5 个，都在 decorators/）

```
heart.cpp   爱心（官方：原地旋转跳动）
dizzy.cpp   晕眩（两个旋转的圈）
shy.cpp     害羞
sweat.cpp   汗
angry.cpp   怒

★ 我们自写的（可作为扩展示例）：
floating_hearts.h   多个爱心向上漂浮 + 淡出
```

**这几个装饰器目前只接了 2 个（爱心、晕眩）**，其余 3 个（shy/sweat/angry）
代码在但没接触发 —— 你可以直接调用 `avatar_->addDecorator(...)` 接上。

---
