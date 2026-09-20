# 固件改动说明（firmware/board-core-s3/）

覆盖目标：`xiaozhi-esp32/main/boards/m5stack/core-s3/`

---

## 文件清单

> 完整目录结构见 `INSTALL.md` 第 1.2 节（5 条 cp 对应 5 个位置）。

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

二选一：

```
方式 A（推荐）：编译时写进 config.json
    main/boards/m5stack/core-s3/config.json 的 sdkconfig_append 里加：
      "CONFIG_OTA_URL=\"http://<你的服务器IP>:8003/xiaozhi/ota/\""

方式 B：运行时填
    设备配网后在控制台填服务器地址（存进 NVS 的 wifi/ota_url）
```

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
  · 形象：替换 lvgl_assets/ 里的 GIF； ★ 若不想分发官方美术、或想要矢量清晰度，见 CREDITS.md 的「三之二、SVG 代码绘制」
  · 服务器：自建（就是 SelfOtaUrl() 自动识别的那个）

皮肤 B（另一套）：
  · 形象：用官方几何脸（本项目已移植），或你自己画
  · 服务器：改成你要的地址
```

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
