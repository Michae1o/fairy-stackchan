# 音色包（GPT-SoVITS）

**本项目【不附带】微调权重和参考音频** —— 原因见下，本节教你怎么自己弄出来。

---

## 0. ⚠️ 权重和参考音频的获取渠道

**本项目【不附带】微调权重和参考音频文件**，但**给你明确的获取渠道**：

```
★ 模型 + 参考音色 网盘（UP 主本人分享，永久有效，免登录下载）
   https://1730154.share.123pan.cn/123pan/UHp9-kqi8H
   （123 云盘 · 分享名「GPT-SoVITS模型」· 约 112 GB，含大量模型）
```

**⇒ 到这里下载你需要的模型，然后继续看第 2 节怎么接上。**

### 为什么仓库里不放

``` 
① 微调权重单个 148 MB，超出 GitHub 单文件 100 MB 硬限
② 整个网盘 112 GB，不可能放进 git 仓库
③ 音色版权归属【原声者 / 训练者】，公开再分发有风险
⇒ 所以：本仓库只放【教程 + 官方获取链接】
```

### 来源（音色参考）

**B 站 UP 主「白菜工厂1145号员工」**

- 个人空间：<https://space.bilibili.com/518098961>
- 模型网盘：<https://1730154.share.123pan.cn/123pan/UHp9-kqi8H>

```text
· 模型权重：从上面的网盘取
· 参考音频：网盘里通常也带；或从他公开视频里截一段清晰人声
            （5~10 秒即可，取法见 ../audio/README.md）

· 音色来源有两种，用哪种由你决定：
    A. 微调权重（.ckpt + .pth + 配套参考音频，三件套）
    B. 零样本（只给参考音频，不加载微调权重）
```

> ⚠️ 使用任何人的音色前请注意（★ 这一条限制的是【音色文件】，与代码无关）：
> **不要**用这个音色去冒充他人、做商业用途、或做违法内容；
> 若原作者要求下架应立即停止。
>
> ★ 网盘是 UP 主本人的分享，**版权归他**。你下载后自己用没问题，
> 但**不要再把文件二次分发到公开仓库**（这也是本项目不放权重的原因）。
>
> 💡 想商用？→ 换成【你自己的声音】录参考音频（零版权顾虑，见 `../audio/README.md`）。
> 而本项目【代码】本身是 MIT，商用完全 OK。

---

## 1. 部署 GPT-SoVITS（语音引擎本体）

### 1.1 获取代码

**★ 国内网络注意：`github.com` 直连大概率失败**，三选一：

```bash
# 方式 A：直连（有代理/能访问 github 时）
git clone https://github.com/RVC-Boss/GPT-SoVITS.git
cd GPT-SoVITS

# 方式 B：用国内镜像（不需要代理）
git clone https://gitee.com/mirrors/GPT-SoVITS.git
# 或 https://ghfast.top/https://github.com/RVC-Boss/GPT-SoVITS.git

# 方式 C：下载 zip（浏览器直接下 release 包）
#   https://github.com/RVC-Boss/GPT-SoVITS/archive/refs/heads/main.zip
```

> 本项目**不包含** GPT-SoVITS 本体（体积几 GB，且它是独立项目）。

### 1.2 安装环境

**方式 A：官方整合包（Windows，最省事 —— ★ 新手推荐）**

去 [Releases](https://github.com/RVC-Boss/GPT-SoVITS/releases) 下载整合包
（文件名形如 `GPT-SoVITS-v2pro-xxxxxxx.7z`），**解压即用**：

```
解压后目录里有：
  go-webui.bat        ← 双击启动 WebUI（训练/推理界面）
  go-api.bat          ← 双击启动 API 服务（★ 本项目要的就是这个）
  runtime\            ← 自带 Python 环境，不用装 conda
  GPT_SoVITS\         ← 模型与配置
```

> ★ 整合包**不需要**下面的 conda 步骤，直接跳到 1.3 下模型。

**方式 B：手动装（Linux / 想自己控制）**

```bash
conda create -n GPTSoVits python=3.10
conda activate GPTSoVits

# ★★ 顺序很重要：先装 CUDA 版 torch，再装其余依赖
#    （反过来的话 requirements.txt 里的 CPU 版 torch 会把 CUDA 版覆盖掉）
# 按你的显卡选 CUDA 版本：
#   40 系及以下 → cu124    ·    50 系（Blackwell）→ cu128
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu128

# 然后装其余依赖
pip install -r requirements.txt
```

> ⚠️ 国内网络：`pip` 加 `-i https://pypi.tuna.tsinghua.edu.cn/simple`
>
> **验证装对了**（应输出 `True` 和你的显卡型号）：
> ```bash
> python -c "import torch; print(torch.cuda.is_available(), torch.cuda.get_device_name(0))"
> ```

### 1.3 下载预训练模型（★ 最容易卡的一步）

GPT-SoVITS 需要几个**基础模型**（**与第 0 节的音色包不是一个东西**）：

```
需要放到 GPT_SoVITS/pretrained_models/ 下的：

① GPT 底模（v2 用这个）
     gsv-v2final-pretrained/s1bert25hz-5kh-longer-epoch=12-step=369668.ckpt
② SoVITS 底模（v2 用这个）
     gsv-v2final-pretrained/s2G2333k.pth
③ 文本前端（中英）
     chinese-roberta-wwm-ext-large/
     chinese-hubert-base/
④ 中文多音字（G2PW，约 600MB —— ★ 第一次合成慢就是因为加载它）
     （WebUI 首次运行会自动下载，也可手动放到 GPT_SoVITS/text/G2PWModel/）
```

**★ 下载方法（三选一）：**

```bash
# 方式 A：用官方脚本（会自动下全，推荐）
python -m tools.download_models   # 或在 WebUI 里点「下载模型」

# 方式 B：HuggingFace（有代理时）
#   https://huggingface.co/lj1995/GPT-SoVITS/tree/main

# 方式 C：★ 国内镜像 ModelScope（不用代理，推荐国内用）
#   打开 https://www.modelscope.cn/models
#   搜索 "GPT-SoVITS" ⇒ 找一个「模型文件」仓库（不是代码仓库）
#   进去用"下载模型"按钮，或按页面给的 git clone 命令拉取
#   ★ 需要的是 pretrained_models 那一批（不是本项目的音色包）
```

**怎么确认放对了：**

```
GPT-SoVITS/
├── GPT_SoVITS/
│   ├── pretrained_models/
│   │   ├── gsv-v2final-pretrained/
│   │   │   ├── s1bert25hz-5kh-longer-epoch=12-step=369668.ckpt   ← 要有
│   │   │   └── s2G2333k.pth                                      ← 要有
│   │   ├── chinese-roberta-wwm-ext-large/                        ← 要有（含 config.json 等）
│   │   └── chinese-hubert-base/                                  ← 要有（含 config.json 等）
│   └── text/G2PWModel/                                           ← 首次会自动下
```

> ⚠️ **目录名必须完全一致**（`gsv-v2final-pretrained` 不能改名），
> 否则启动时会报 `FileNotFoundError`。

---

## 2. 用你的音色（两种来源）

> ⚠️ 本仓库**不带**权重文件，需你自己准备。

**两种来源，用哪种由你决定：**

| 来源 | 要准备什么 | 特点 |
|---|---|---|
| **A. 微调权重** | `.ckpt` + `.pth` + **配套**参考音频（三件套） | 音色更稳定；需拿到成品权重或自己训练 |
| **B. 零样本** | 只要一段参考音频（5~10 秒） | 省事，不用权重；效果通常略逊于 A |

> A 的做法见 [2.3 节](#23-用微调权重配置三件套)；
> B 的做法见下面 2.1 / 2.2。

### 2.1 启动 API 服务

```bash
cd GPT-SoVITS
python api_v2.py -a 127.0.0.1 -p 9880 -c GPT_SoVITS/configs/tts_infer.yaml
```

> **这个脚本只有 3 个参数**（★ 没有 `-d`，设备是在配置文件里选的）：
> ```
> -a  监听地址   默认 127.0.0.1
>                 本机自用就保持默认；想让局域网里【别的机器】访问就写 0.0.0.0
> -p  端口       默认 9880（本项目服务器按这个填）
> -c  配置文件   默认 GPT_SoVITS/configs/tts_infer.yaml
> ```
>
> **想切成 CPU / GPU？改配置文件**（`tts_infer.yaml` 里 custom 段的 `device`）。
>
> **整合包**：双击 `go-api.bat`（等价于上面的命令）。
>
> **验证起来了**：浏览器打开 <http://127.0.0.1:9880/docs> 能看到接口页面。

**★ 运行中切换模型（不用重启）—— 这两个接口很实用：**

```bash
# 切 GPT 权重
curl "http://127.0.0.1:9880/set_gpt_weights?weights_path=GPT_weights_v2/你的模型.ckpt"

# 切 SoVITS 权重
curl "http://127.0.0.1:9880/set_sovits_weights?weights_path=SoVITS_weights_v2/你的模型.pth"

# 重启（换完权重想清缓存时用）
curl "http://127.0.0.1:9880/control?command=restart"
```

### 2.2 指定参考音频（零样本）

**方式 A：改配置文件 `GPT_SoVITS/configs/tts_infer.yaml`**

```yaml
custom:                          # ★ 改这个 custom 段（api_v2.py 默认用它）
  device: cuda                   # cuda / cpu
  is_half: true                  # 半精度（省显存，显卡支持时开）
  version: v2                    # 版本：v1 / v2 / v2Pro …

  # ★ 零样本：权重保持默认（pretrained_models 下的底模），只填参考音频
  ref_audio_path: <绝对路径>/你的参考音频.wav
  prompt_text: 参考音频里说的那句话
  prompt_lang: zh
```

> ⚠️ **零样本不需要改 `t2s_weights_path` / `vits_weights_path`** ——
> 保持它们指向 `pretrained_models/` 下的**底模**即可。
> 只有你**自己微调过**（见 2.3）才需要改那两项。

**方式 B：API 请求里指定（不改配置文件，更灵活）**

```python
import requests
requests.post("http://127.0.0.1:9880/tts", json={
    "text": "你好，我是 Fairy。",
    "text_lang": "zh",
    "ref_audio_path": "<绝对路径>/你的参考音频.wav",
    "prompt_text": "参考音频对应的文本",
    "prompt_lang": "zh",
    "text_split_method": "cut5",
}).content  # 返回 wav 二进制
```

> ⚠️ **`prompt_text` 必须与参考音频内容完全一致**，否则音色会飘。
> 不确定音频说了什么的话，先用 `../audio/README.md` 的办法转成文字。

### 2.3 用微调权重（配置三件套）

**如果你有微调权重**（`.ckpt` + `.pth`，来自成品或自己训练），按这一节配置。

微调产出的两个文件**必须与参考音频配套使用**（三件套，缺一音色就变）。

**★ 存放目录（注意是 `_v2` 后缀，放错会找不到）：**

```
GPT-SoVITS/
├── GPT_weights_v2/          ← ★ GPT 权重放这里（.ckpt）
│     你的模型.ckpt
└── SoVITS_weights_v2/       ← ★ SoVITS 权重放这里（.pth）
      你的模型.pth
```

> ⚠️ 若你用 v1/v2Pro 等其他版本，目录名可能是 `GPT_weights/`、`GPT_weights_v2Pro/` 等。
> **以 `tts_infer.yaml` 里 `version` 字段对应的版本为准**，或看 WebUI 保存时提示的路径。

**然后在 `tts_infer.yaml` 的 `custom:` 段指向它们：**

```yaml
custom:
  version: v2
  # ★ 三件套要配套（同一批训练产出），混用会导致音色走样
  t2s_weights_path: GPT_weights_v2/你的模型.ckpt        # 相对 GPT-SoVITS 根目录
  vits_weights_path: SoVITS_weights_v2/你的模型.pth
  ref_audio_path: <绝对路径>/配套的参考音频.wav
  prompt_text: 那段参考音频的文本
  prompt_lang: zh
```

> ★ **三件套必须配套**，混用会导致音色走样。

### 2.4 接到小智服务器

编辑服务器的 `data/.config.yaml`：

```yaml
TTS:
  # ★ 节点名要与 server.selected_module 里配的一致
  #   本项目的控制台按 GPT_SOVITS_V2 这个节点名读写，建议沿用
  GPT_SOVITS_V2:
    api_url: http://127.0.0.1:9880
    # ★ 指向【你自己的】参考音频（本仓库不含音频，取法见 ../audio/README.md）
    ref_audio_path: <绝对路径>/你的参考音频.wav
    prompt_text: 参考音频里说的那句话    # ★ 必须与实际内容一致
    # ★ 若你用了微调权重，这三项必须同步改，否则音色走样：
    #   gpt_model / sovits_model / ref_audio_path
```

---

## 3. 训练你自己的音色（可选）

**★ 先明确：不训练也能用。** 第 2 节的两种来源（成品权重 / 零样本）
都不需要你自己训练。这一节只是**留给想自己训练的人**。

准备好 **5~10 秒**的清晰人声（要求见 `../audio/README.md`），然后：

### 3.1 启动 WebUI

```bash
cd GPT-SoVITS

# 手动安装的：
python webui.py

# ★ 整合包：直接双击（不是 python 命令）
#   go-webui.bat
```

浏览器打开后（默认 <http://127.0.0.1:9874>）：

```
① 推理（Inference）→ 上传参考音频直接试听（零样本，不用训练）
    ★ 想先听效果，就点这里；满意了就不用往下走

② 训练（Training）→ 按顺序：
   1. 切分音频（Slice）        —— 把长音频切成 3~10 秒的小段
   2. ASR 打标（标注文本）      —— 自动识别每段说了什么
   3. 训练 SoVITS              —— 第一个模型
   4. 训练 GPT                 —— 第二个模型
   ★ 数据少时（< 1 分钟）每步都很快，总共十几分钟

③ 训好的模型保存在（★ 注意 _v2 后缀）：
      GPT_weights_v2/           ← .ckpt
      SoVITS_weights_v2/        ← .pth
   在「模型管理」里可以切换/试听
```

> ⚠️ **如果你不走自己训练这条路，第 3 节整节都可以跳过**
> （路线 A 用别人的成品权重、路线 B 零样本，都不需要这一节）。

### 3.2 训练参数建议（小数据量）

| 参数 | 建议值 | 说明 |
|---|---|---|
| 总轮数 (epochs) | 8~15 | 数据少时别太多，会过拟合（音色变"僵"） |
| batch size | 4~8 | 显存不够就调小（12GB 显存用 4~8 没问题） |
| 保存频率 | 每 2~4 轮 | 便于最后挑最好的一版试听 |

### 3.3 训完怎么用

```
① 从 GPT_weights_v2/ 和 SoVITS_weights_v2/ 里挑出你认为最好的那一版
   （多试听几版 —— 不同 epoch 的音色差别不小）

② 把两个文件【连同你用的参考音频】放在一起，就是一份新音色包

③ 按第 2.3 节的方式在 tts_infer.yaml 里指向它们
```

> ⚠️ **三件套必须来自同一批训练**（`.ckpt` + `.pth` + 那次训练用的参考音频）。
> 混用不同批次的文件 ⇒ 音色走样、甚至报错。

---

## 4. 常见问题（★ 都是我们实际踩过的）

### 4.1 启动 / 环境相关

| 现象 | 原因 / 解法 |
|---|---|
| `git clone` 卡住 / 超时 | 国内直连 github 不稳 ⇒ 用 Gitee 镜像或代理（见 1.1） |
| `torch.cuda.is_available()` 返回 `False` | 装了 CPU 版 torch。重装：`pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu124`（50 系显卡用 cu128） |
| 启动报 `unrecognized arguments: -d` | ★ **`api_v2.py` 没有 `-d` 参数**（只有 `-a` / `-p` / `-c`）⇒ 走 CPU/GPU 要改配置文件里的 `device` |
| `FileNotFoundError: ...pretrained_models/...` | 底模没下全或**目录名被改了** ⇒ 对照 1.3 的目录树检查（`gsv-v2final-pretrained` 不能改名） |
| 启动报 `onnxruntime 找不到 cudart` | CUDA 版本与 onnxruntime 不匹配。**可以无视**（只影响多音字，不影响速度）；想修就装匹配版本 |
| 显存不够（OOM） | 训练时调小 batch；推理时把配置文件的 `device` 改成 `cpu`（慢但能用） |

### 4.2 音色相关（★ 最常被问）

| 现象 | 原因 / 解法 |
|---|---|
| **第一次合成要等 50 秒** | **正常**。要加载 G2PW 多音字模型（约 600MB），之后就 0.5 秒 |
| **音色不对 / 走样** | 按顺序查：① 三件套是否配套（`.ckpt` + `.pth` + **训练时那份**参考音频）② `prompt_text` 是否与参考音频**逐字**一致 ③ 参考音频本身质量（是否有 BGM/混响） |
| 音色"僵"、不像本人 | 训练轮数过多（过拟合）⇒ 换早期 epoch 的权重试听 |
| 换模型后音色变了 / 变差 | ★ **换微调模型必须同步改三件套**，并**清空** GPT-SoVITS 的缓存（删 `server_tmp/` 或用 API 的切换接口） |
| 念不出罗马数字 Ⅲ | GPT-SoVITS 的文本归一化不处理罗马数字（本项目固件侧已附补丁，见 `../server/patches/`） |
| 中文多音字读错 | G2PW 没加载成功（看启动日志）。首次运行需要联网下载，或手动放 `GPT_SoVITS/text/G2PWModel/` |

### 4.3 接小智服务器相关

| 现象 | 原因 / 解法 |
|---|---|
| 设备说话但没声音 | ① 采样率不匹配（服务器与设备要严格相等）② 服务器响应没带 `Connection: close` ③ WiFi 省电模式（详见 `../INSTALL.md` 排查节） |
| 服务器日志报连不上 9880 | GPT-SoVITS 没启动，或 `api_url` 写错（应是 `http://127.0.0.1:9880`） |
| 服务器能出声但设备是官方音色 | 设备的服务器地址指向了官方（见 `../firmware/README.md` 的「自建地址怎么记」） |

---

## 5. 相关链接

- GPT-SoVITS 上游：<https://github.com/RVC-Boss/GPT-SoVITS>
- GPT-SoVITS 官方文档：<https://github.com/RVC-Boss/GPT-SoVITS/blob/main/docs/cn/README.md>
- 整合包下载：<https://github.com/RVC-Boss/GPT-SoVITS/releases>
- 本项目的音色来源：B 站「白菜工厂1145号员工」<https://space.bilibili.com/518098961>
- 本项目的参考音频说明：[../audio/README.md](../audio/README.md)

---

## 6. ★ 最小可跑通路径（15 分钟，照这个顺序做）

> 前面各节是完整资料。如果你只想**尽快听到效果**，照下面这条最短路径走。

```
① 下载整合包（Windows）
     https://github.com/RVC-Boss/GPT-SoVITS/releases
     ⇒ 解压到一个【没有中文和空格】的路径，例如 D:\GPT-SoVITS

② 下载底模（整合包通常已自带，若缺则用镜像补齐）
     看 GPT_SoVITS\pretrained_models\ 下有没有 gsv-v2final-pretrained \
     ⇒ 没有就按 1.3 节下载

③ 准备音色来源（二选一）
     A. 微调权重：.ckpt + .pth + 配套参考音频（三件套，必须配套）
        ⇒ 按 2.3 节配置
     B. 零样本：只要一段 5~10 秒参考音频
        放哪都行，记住【绝对路径】。要求：纯人声、无 BGM、无混响
        （没有素材？见 ../audio/README.md 的三种办法）
        ⇒ 按 2.2 节配置

④ 双击 go-api.bat 启动 API 服务
     看到 "Uvicorn running on http://127.0.0.1:9880" 就算成功
     ★ 第一次启动会下载 G2PW（约 600MB），耐心等

⑤ 自测一下能不能出声
     浏览器打开 http://127.0.0.1:9880/docs
     ⇒ 用 "POST /tts" 试合成一句，或在 WebUI（go-webui.bat）里试听

⑥ 接到小智服务器
     改 data/.config.yaml 的 TTS 段（见本文 2.4）
     ⇒ 重启服务器 ⇒ 对设备说句话，听是不是你的音色
```

**卡在哪一步了？** 直接跳到第 4 节的对应小节查（4.1 启动 / 4.2 音色 / 4.3 接服务器）。
