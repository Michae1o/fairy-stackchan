# 免编译固件（直接刷，不用装开发环境）

> ★ 适合**不想装 ESP‑IDF 工具链**的人。会用工具链的建议自己编（见 [../INSTALL.md](../INSTALL.md)），更可控。

## 下载

固件放在 **Release** 里（约 11.9 MB；不放仓库里，免得 clone 变慢）：

```text
https://github.com/Michae1o/fairy-stackchan/releases/latest
    ⇒ 找 fairy-stackchan-universal.bin 下载

文件名：fairy-stackchan-universal.bin
大小：  12,466,209 字节（约 11.9 MB）
芯片：  ESP32-S3（整机合并固件，烧到 0x0）
md5：   034b0b58516625017e382d29341e83fc
```

> 下载后建议校验：`certutil -hashfile fairy-stackchan-universal.bin MD5`（Windows）
> 本目录（`firmware-bin/`）只放说明，不含 `.bin`。

---

## 一、这个固件是什么

```text
整机合并固件 = bootloader + 分区表 + 应用，一次烧到 0x0 就能跑

★ "universal（通用）" 的含义：不含任何服务器 IP ⇒ 谁刷都行
   ⚠️ 代价：它第一次开机会连官方服务器，怎么转到你自己的服务器见第三节（★ 必读）
```

### 和作者自用版的区别

| | 通用版（Release） | 作者自用版 |
|---|---|---|
| 服务器地址 | ❌ 不含（自己填） | ✅ 写死作者的内网 IP |
| 双皮肤 | ✅ | ✅ |
| 摸头 / 甩晕 / 爱心 / 呼吸动画 | ✅ | ✅ |
| 唤醒词 | ✅ Hi Fairy + 你好小智 | ✅ 同 |
| 配网页填服务器地址 | ✅ 支持（第三节） | ✅ 同 |
| 表情素材 | ✅ 内嵌 2 个 GIF（见下） | ✅ 含 |

**⇒ 功能完全一样，只差「服务器地址」这一项。**

> ⚠️ **关于表情素材**：这个预编译固件里**内嵌了 2 个作者自绘的表情 GIF**（约 2 MB）。
> 素材版权不属于本项目 —— 自用没问题，**二次分发 / 商用前请读 [CREDITS.md](../CREDITS.md)**。
> 想换成自己的：会编译 ⇒ 见 [INSTALL.md](../INSTALL.md) §1.3；
> **不会编译 ⇒ 用 [tools/make_face.py](../tools/make_face.py) 生成一套再编**。

---

## 二、怎么刷

> ⚠️ **刷之前先备份出厂固件**（这一步不可逆，别跳过）：
>
> ```bash
> python -m esptool --chip esp32s3 -p COM5 read-flash 0 0x1000000 factory-backup.bin
> ```
>
> 得到一个 16,777,216 字节的文件，存好 —— 想回出厂就把它整片写回 `0x0`。
> （这条命令会先复位设备再读，跑之前确认设备上没在跑重要的东西。）

> 下面三种任选。★ 推荐 **方法 A**（免安装、免命令行）。
> **验证程度（如实说明）**：**B 是本项目自己烧固件时实际在用的命令**（实测跑通）；
> **A** 是开源网页工具、其页面支持自定义 `.bin`，但**本项目没在真机上试过**；**C ⛔ 未实测**。
>
> ⛔ 服务器虽然有「OTA 自动升级」机制，但**本项目不推荐、也不展开** ——
> 它要同时凑对型号名、版本号和下载前缀，容易把设备搞成起不来。用 A / B 最稳。

### 方法 A（★ 推荐）：浏览器里刷，免安装

```text
1. 用【电脑】的 Chrome 或 Edge 打开 https://esptool.spacehuhn.com/
2. USB-C 数据线连设备（★ 要数据线，不是充电线）
3. 点 CONNECT → 选串口（Windows 上形如 COM3）
4. 点「Add your .bin」→ 选 fairy-stackchan-universal.bin
   ⚠️ 地址(address) 填 0x0 —— 这是合并固件，必须从 0x0 开始
5. 点 Program，等 1~2 分钟
```

> ⚠️ 必须用桌面版 Chrome / Edge（靠 WebSerial）；Firefox / Safari / 手机浏览器都不行。

### 方法 B：命令行 esptool（最稳）

```bash
pip install esptool

python -m esptool --chip esp32s3 -p COM3 -b 460800 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
    0x0 "fairy-stackchan-universal.bin"
```

> ⚠️ 合并固件 ⇒ 地址写 `0x0`（不用分别烧 bootloader / 分区表 / app）。
> 上面这串就是**本项目实际在用的命令**（实测跑通；esptool v5 用 `write-flash`）。
> 端口按实际改（「设备管理器 → 端口」里看）。

### 方法 C：M5Stack 官方 M5Burner（⛔ 未实测，仅供参考）

> ⛔ **未实测**：作者没用 M5Burner 烧过本项目的固件；官方文档只写了烧官方 UiFlow 固件 / 产品 demo，
> **没写明支持烧本地自定义 `.bin`** ⇒ 出问题请改用 A / B。

M5Burner 需去官网下载安装（<https://docs.m5stack.com/zh_CN/download>）。
若你的版本里有「自定义固件 / Custom」入口，也可以试 —— ⚠️ 地址同样填 `0x0`。

---

## 三、★★ 刷完之后：让它连【你自己的服务器】

### ⚠️ 先讲实话：「不用编译」≠「什么都不用做」

通用固件**不含任何服务器地址** ⇒ 全新设备第一次开机会先连官方服务器（`api.tenclass.net`）。
让它转到你自己的服务器，两条免编译的路：

> ⚠️ **别把两个页面搞混**：控制台（`/admin`）是**你自建服务器**上的页面，
> 设备连不上你的服务器就打不开它。要填地址的地方是**设备自己的配网页** ⇒ 路径 B。

#### 路径 A（要编译，本页不展开）：把地址编进固件

适合「给朋友用」：编译时写死地址，他刷完就能连、什么都不用配。

```text
· 用 scripts/build.py 编译：地址写在 main/boards/m5stack/core-s3/config.json 的 CONFIG_OTA_URL
· 用 idf.py 编译：       地址写进 sdkconfig.defaults*（config.json 那项 idf.py 不读）
```

⚠️ 写死地址的固件**别公开分发**（会泄露你的内网 IP）。细节见 [../INSTALL.md](../INSTALL.md) §1.4。

#### 路径 B（★ 免编译推荐）：在设备配网页填地址

```text
① 让设备进【配网模式】
     全新设备：第一次开机自动进
     已配网设备：开机时（还在 Starting）点一下屏幕；或 WiFi 连不上时自动进
② 手机连设备热点（名字见屏幕，形如 Xiaozhi-XXXX）
③ 浏览器打开配置页（地址见屏幕提示）
④ 切到 Advanced 页签（中文界面「高级选项」）→ 填「自定义 OTA 地址」：
      http://<你的服务器IP>:<http_port>/xiaozhi/ota/     （默认 http_port = 8003）
   → 保存
⑤ 回「新 Wi‑Fi」页签填你家 WiFi 密码 → 设备重启，直连你的服务器
```

> 同页还有「Wi‑Fi 最大发射功率 / 记住 BSSID / 启用睡眠模式」三项，**不影响连服务器**，一般不用动。
>
> **为什么成立**：这个输入框是上游 `esp-wifi-connect` 组件自带的，
> 保存时直接写进设备 NVS，和固件读的是**同一个键** —— 官方文档从没讲过，所以大多数人没见过。
> 原理见 [../firmware/README.md](../firmware/README.md) 的「双皮肤是怎么实现的」一节。

#### 路径 C（备选）：对它说地址

```text
设备先连上官方服务器，再对它说：
    「我的服务器是 http://<你的服务器IP>:8003/xiaozhi/ota/」
⇒ AI 调设备本地 MCP 工具 self.server.set_ota_url，直接写进设备 NVS
⇒ 设备重启，连上你的服务器
```

> ⚠️ 两个现实问题（所以只当备选）：
> ① **必须先有官方服务器可用**（纯离线环境走不通）
> ② **用嘴念 IP 很难念对**（ASR 会把「点五/幺五」听成别的）

### ★ 切皮肤不会丢地址

切到官方皮肤**之前**，设备会记住你填过的自建地址；切回 Fairy 时自动用回来，不用重填。
（原理与实现：`ota.cc` 的自动记忆机制，见 [../firmware/README.md](../firmware/README.md)。）

### 改过服务器端口的话

填地址时**把端口写全**（`http://<你的IP>:<端口>/xiaozhi/ota/`）就没问题 ——
设备侧**不再按固定 8003 猜端口**。

### 怎么确认成功

```text
成功：界面换成 Fairy 皮肤（全屏 GIF 表情在动）、说话是人设语气（"我在"、"收到"）
串口日志：WS: Connecting to ws://<你的服务器IP>:8000/...

没成功就查：
  · 浏览器能打开那个 ota/ 链接吗（服务器在跑、端口对、防火墙没拦）
  · 设备和电脑在同一个 WiFi 吗
  · 串口日志里它在连哪个 IP（对着看就知道是地址错了还是服务器没起）
```

---

## 四、双皮肤怎么用

刷完设备里有**两套皮肤**，随时切：

| 皮肤 | 屏幕上是什么 | 连谁 | 什么时候用 |
|---|---|---|---|
| **Fairy** | **全屏 GIF 表情**（320×240，来自你服务器的素材） | 你自己填的服务器 | 在家 |
| **Geometry** | **官方的几何脸**（代码画的，M5Stack 原版） | 官方服务器 | 带出门 |

**切换方式（三种任选）：**

```text
① ★ 按设备【电源键】短按 —— 最可靠，断网也能切
② 对它说：「换成官方表情」「切回 Fairy」
③ Web 控制台 → 皮肤页 → 点卡片
```

> 两套脸分别是什么、怎么改，见 [../firmware/README.md](../firmware/README.md) 的「形象相关」一节。

---

## 五、硬件要求

```text
· M5Stack StackChan（CoreS3 版本）
    —— 不是 CoreS3 开发板（没舵机/灯环/触摸头），刷了没意义
· USB-C 数据线（刷机用）
· 2.4GHz WiFi（★ ESP32-S3 不支持 5GHz）
```

---

## 六、这个固件里有什么 / 没有什么

```text
✔ 2 个表情动画 GIF（约 2 MB，作者自绘的二次创作）
    ⇒ 这就是它能显示全屏表情的原因
    ⇒ ★ 只在这个预编译固件里有；仓库源码包【不含任何图片文件】
    ⇒ 二次分发 / 商用前读 ../CREDITS.md 第三节

✘ 服务器地址（这正是它「通用、能公开」的原因）
    ⇒ 代价：第一次开机会连官方服务器，需按第三节转过来
✘ 任何 API Key（Key 在服务器配置里，不在固件里）
✘ Fairy 的【原版】美术素材（固件里那 2 个 GIF 是作者自绘的）
✘ 音色模型（音色在服务器侧，见 ../voice-package/README.md）
```

---

## 七、想自己编一个？

见 [../INSTALL.md](../INSTALL.md)（拉上游 → 用 `tools/apply_to_upstream.py` 打改动 → 编译）。
想连第三步都省掉，可以把服务器地址编进固件（路径 A），但那之后就**别公开分发**了。
