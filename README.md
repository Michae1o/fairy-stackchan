# Fairy StackChan

把 M5Stack **StackChan**（CoreS3 桌面机器人）改成《绝区零》的 **Fairy**：
全屏表情 + DeepSeek 对话 + GPT‑SoVITS 零样本音色 + 网页控制台 + 摸头/甩晕/转头等互动。

---

## 这个仓库是什么（先看这段）

> ★★ **本仓库是「改动集」（patch 包），不是完整工程。**
>
> 里面只有**改动过的文件 + 脚本 + 文档**：**没有上游代码本体** —— 没有 `main/`、
> 没有 `CMakeLists.txt`、没有 `idf_component.yml`，也不含任何现成的图片 / 音频 / 模型权重。
> 用法固定两步：**① 先拿一份上游工程 → ② 把本仓库覆盖进去**（然后再编译 / 起服务）。
> **只 clone 本仓库是跑不起来的** —— 覆盖到哪、该用哪个上游版本，见
> [`INSTALL.md`](INSTALL.md) §0.5（版本）与 §1.2/§2.2（覆盖步骤）。
> 想跳过编译的人：用 Release 里的预编译固件，见 [`firmware-bin/README.md`](firmware-bin/README.md)。

**手把手教你把自己那台 StackChan 做成 Fairy。**

作者生产环境（在家天天用的那台）的**代码、配置、参数、踩过的坑**，都在这里；
去掉的只有三样：

1. **个人隐私信息** —— 内网 IP、密钥、本机绝对路径
2. **现成的美术素材** —— Fairy 的表情 GIF（版权属原版权方）
3. **音频与模型权重** —— 参考音频、TTS 微调权重

⇒ 你克隆下来，能照着把同样的东西做出来：**素材用仓库里的代码生成**，或换成你自己的。

---

## 做出来是什么效果

- **双皮肤，电源键短按切换**
  - `Fairy` —— 全屏 GIF 表情（320×240），配套自建服务器
  - `Geometry` —— 官方的几何脸（M5Stack 原版代码画的），直连官方服务器
- **双唤醒词**：`Hi Fairy` + `你好小智`（同一个固件里都生效）
- **24 个情绪名**（23 个 → `idle.gif`，`thinking` → `thinking.gif`）
- **互动**：摸头 → 开心 + 冒爱心；甩晕 → 转圈晕眩；待机自动转头（默认关）
- **点一下屏幕** → 状态栏短暂出现（左 WiFi / 右电量，约 3 秒后隐藏）
- **屏幕菜单**：右上角 `☰` 拉开右侧抽屉（设置项）
- **两个 MCP 工具**（让 AI 自己改设备）：设置服务器地址、切换皮肤
- **网页控制台**：PC 版 `/admin`（状态灯、硬件开关、皮肤、TTS 试听、网页对话），手机版 `/m` —— 用法见 [`CONSOLE.md`](CONSOLE.md)
- **音色**：GPT‑SoVITS 零样本克隆（服务器侧带 Fairy 语气后处理），不用训练
- **切皮肤**：三种方式 —— 电源键短按 / 语音吩咐 / 控制台点卡片

---

## 两条路线，选一条走

### 路线 A：不想编译，直接刷

> ★★ **前提：你先得有一台能跑的服务器。**
> 「免编译」省掉的只是**编译固件**这一步，**不等于不用服务器** ——
> 设备刷完要有东西可连，否则它只会连官方服务器（没有你的人设/音色/控制台）。
> ⇒ 服务器怎么起见 [`INSTALL.md`](INSTALL.md) §1（**先做这一线**）。

1. 按 [`INSTALL.md`](INSTALL.md) §1 把**服务器**跑起来并验通
   （判据：`/admin` 返回 200 + `tools/test_server_e2e.py` 四个 ✅）
2. 到 [Releases](https://github.com/Michae1o/fairy-stackchan/releases) 下载 `fairy-stackchan-universal.bin`
3. 按 [`firmware-bin/README.md`](firmware-bin/README.md) 刷进设备（三种方法，含浏览器网页烧录）
4. 设备配网页「高级选项」里填你的服务器地址 ⇒ 串口出现 `WS: Connecting to ws://…`

> ⚠️ 这个预编译固件里**内嵌了 2 个作者自绘的表情 GIF**（约 2MB），
> 只为你上手能看到效果，个人非商业使用；详见 [`CREDITS.md`](CREDITS.md)。

### 路线 B：要自己改 / 自己编

全部步骤见 **[`INSTALL.md`](INSTALL.md)**（标准安装 SOP，含前提、顺序、每步判据、报错对照）。

最短路径（三条命令）：

```bash
git clone <本仓库>
python3 -m pip install pillow                              # ① 生成素材要用（★ 装进你正在用的那个 python）
python3 tools/make_face.py --out fairy-assets              # ② 生成表情素材
python3 tools/apply_to_upstream.py <上游 xiaozhi-esp32>    # ③ 把固件改动打上去
python3 tools/apply_to_server.py   <上游 xiaozhi-server>   # ④ 把服务器改动打上去
```

（想一条命令把这些 + 编译都跑一遍：`bash tools/repro_all.sh --all`，见下方「给 AI Agent 的步骤」。）

两个脚本**幂等**（重复跑只会说「已存在，跳过」），改完照常 `idf.py build flash`。

> ★ **本包是按哪份上游做的**（两个版本，别混）：
> **作者实测过的** = 固件 `5d54beb7`（2026‑09‑15）+ 服务器 `4745186c`（2026‑09‑17）；
> **本包改动的编译验证** = 固件 `4632dc51f`（09‑20）+ 服务器 `788f5301f`（09‑21）。
> 要 100% 复现就用前两个（取法 + 更新了怎么办 ⇒ `INSTALL.md` §0.5）。

---

## 仓库地图

| 目录 / 文件 | 里面是什么 |
|---|---|
| [`INSTALL.md`](INSTALL.md) | **标准安装 SOP**：前提 → 固件 → 服务器 → 音色 → 验证 → 报错对照 |
| [`CONSOLE.md`](CONSOLE.md) | **控制台使用说明**（电脑 `/admin` 8 个页签 · 手机 `/m` 4 个页签 · 常见疑问 · API） |
| [`firmware/`](firmware/README.md) | 固件的**全部**改动源码（板卡层 / 头像与装饰器 / 本地组件 / 显示层）+ `CMakeLists.append.txt` |
| [`firmware-bin/`](firmware-bin/README.md) | 免编译固件 + 三种刷机方法 + 首连设置 + 排错 |
| [`server/`](server/README.md) | 服务器端**全部**改动源码（10 个文件，按镜像目录结构放） |
| [`tools/`](tools/) | 5 个脚本：生成素材 / 应用固件改动 / 应用服务器改动 / 校验产物 / **验服务器链路（模拟设备）** |
| [`fairy-assets/`](fairy-assets/README.md) | **空目录** —— 原本放 Fairy 表情 GIF，不随包（说明在该目录 README） |
| [`voice-package/`](voice-package/README.md) | 音色：零样本克隆 / 微调两条路，参考音频怎么录 |
| [`CREDITS.md`](CREDITS.md) | 出处、授权、素材与音色的版权说明 |
| [`CHECKLIST.md`](CHECKLIST.md) | 作者自己发布前用的自检清单（使用者可无视） |

---

## 有什么 / 没有什么

| | 内容 |
|---|---|
| **有** | 上游**改动源码**（固件：板卡层 / 头像与装饰器 / 本地组件 / 显示层；服务器：10 个文件）· 5 个工具脚本 · 参数与做法文档 · 1 张控制台界面截图（`docs/`）· 免编译固件（在 Release 里） |
| **没有** | ★ **上游代码本体**（`main/`、`CMakeLists.txt` 这些要自己去 clone 上游）· 任何**角色美术素材** · 参考音频 · TTS 微调权重 · 作者的内网 IP / 密钥 / 本机路径 |

表情素材、音色、权重的替代办法，分别见
[`fairy-assets/README.md`](fairy-assets/README.md)、[`voice-package/README.md`](voice-package/README.md)。

---

## 给「只用 AI Agent 操作」的用户

如果你自己写代码不多、主要让 AI Agent（Claude Code / Cursor / Hermes 等）帮你做，
把这个仓库丢给它，按这个顺序让它做：

1. 读 **`INSTALL.md`**（它是唯一安装依据）；**动手顺序按 §0.7 的三条线**
   —— **先服务器线**（不用设备就能验证）→ 再固件线 → 最后合流
2. 用 **`tools/apply_to_upstream.py`** 打固件改动、`tools/apply_to_server.py` 打服务器改动
   —— 别让它手抄补丁，这两个脚本带自检
3. 服务器起来后先用 **`python3 tools/test_server_e2e.py --audio ref.wav`** 验整条链路
   （模拟设备跑握手 → ASR → LLM → TTS，四个 ✅ 才继续）
4. 编译前用 **`python3 tools/verify_artifact.py --gif-dir fairy-assets`** 验素材规格
5. 编译后用 **`python3 tools/verify_artifact.py build/merged-binary.bin`** 验产物
   （会告出：双唤醒词在不在、两个 MCP 工具在不在、有没有泄漏私货 IP、内嵌了几张 GIF）
6. ★ **想省事就一条命令**：`bash tools/repro_all.sh --all`
   —— 它把上面 2~5 步（含编译）全跑一遍并直接打印判据，日志留在 `./repro-work/`
   （`--no-build` 跳过编译、`--firmware`/`--server` 只跑一条线；Windows 用 WSL / Git Bash）

---

## 许可

代码 MIT（见 [`LICENSE`](LICENSE)）；
**本包用到的第三方代码与各自许可**（含 Fairy-DSH 的 Apache-2.0 署名与许可全文）
见 [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md)；
形象、音色、素材的版权与出处见 [`CREDITS.md`](CREDITS.md)。
