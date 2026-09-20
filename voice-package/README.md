# 音色包（GPT-SoVITS）

**本项目【不附带】微调权重和参考音频** —— 原因见下，本节教你怎么自己弄出来。

---

## 0. ⚠️ 权重和参考音频去哪拿（★ 先看这里）

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

```
· 模型权重：从上面的网盘取（★ 优先，省事）
· 参考音频：网盘里通常也带；或从他公开视频里截一段清晰人声
            （5~10 秒即可，取法见 ../audio/README.md）
· ★ 其实【不微调也能用】：GPT-SoVITS 零样本（zero-shot）
  只要参考音频就能克隆音色，本项目线上就是这么用的
  想更稳再按第 3 节跑一遍微调
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

### 1.1 获取

```bash
git clone https://github.com/RVC-Boss/GPT-SoVITS.git
cd GPT-SoVITS
```

> 本项目**不包含** GPT-SoVITS 本体（体积几 GB，且它是独立项目）。

### 1.2 安装环境

**方式 A：用官方整合包（Windows，最省事）**

去 [Releases](https://github.com/RVC-Boss/GPT-SoVITS/releases) 下载整合包，
解压即用，跳过下面所有步骤。

**方式 B：手动装（Linux / 想自己控制）**

```bash
conda create -n GPTSoVits python=3.10
conda activate GPTSoVits
pip install -r requirements.txt

# ★ 有 NVIDIA 显卡的话（推荐）：装 CUDA 版 torch
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu128
```

> ⚠️ 国内网络：`pip install` 加 `-i https://pypi.tuna.tsinghua.edu.cn/simple`

### 1.3 下载预训练模型

GPT-SoVITS 需要几个基础模型（**与上面的音色包不是一个东西**）：

```bash
# 从 HuggingFace 或国内镜像（modelscope）下载，放到 GPT_SoVITS/pretrained_models/
#   s1bert25hz-2kh~2.5kh-15s.pt        （GPT 底模）
#   s2G488k.pth / s2G2333k.pth         （SoVITS 底模）
#   chinese-hubert-base / chinese-roberta-wwm-ext-large
```

> 官方 README 有详细清单和镜像地址。国内可用 [modelscope](https://www.modelscope.cn/models) 搜同名。

---

## 2. 用你的音色（★ 零样本，不用训练）

> ⚠️ 本仓库**不带**权重文件。下面用「你自己准备的参考音频」走**零样本**路线 ——
> GPT-SoVITS 只要 5~10 秒音频就能克隆音色，效果已经很好，**不需要微调**。

### 2.1 启动 API 服务

```bash
cd GPT-SoVITS
python api_v2.py -a 127.0.0.1 -p 9880 -c GPT_SoVITS/configs/tts_infer.yaml
```

> 若用整合包：双击 `go-api.bat`（或同类脚本）。

### 2.2 指定参考音频（零样本）

**方式 A：改配置文件**

编辑 `GPT_SoVITS/configs/tts_infer.yaml`：

```yaml
# 零样本：只填参考音频即可，权重留默认
ref_audio_path: <绝对路径>/你的参考音频.wav
prompt_text: 参考音频里说的那句话
prompt_lang: zh
```

**方式 B：API 请求里指定（不改配置）**

```python
import requests
requests.post("http://127.0.0.1:9880/tts", json={
    "text": "你好，我是 Fairy。",
    "text_lang": "zh",
    "ref_audio_path": "<绝对路径>/你的参考音频.wav",
    "prompt_text": "参考音频对应的文本",
    "prompt_lang": "zh",
    "text_split_method": "cut5",
})
```

> ⚠️ **`prompt_text` 必须与参考音频内容一致**，否则音色会飘。
> 若你不确定音频说了什么，可用 `../audio/README.md` 的方法先转文字。

### 2.3 如果你自己微调了（进阶）

微调产出的两个文件（`.ckpt` + `.pth`）**必须与参考音频一起用**，
指向方式：

```yaml
t2s_weights_path: <绝对路径>/你的模型.ckpt
vits_weights_path: <绝对路径>/你的模型.pth
ref_audio_path: <绝对路径>/配对的参考音频.wav
```

> ★ **三件套必须配套**，混用会导致音色走样。

### 2.4 接到小智服务器

编辑服务器的 `data/.config.yaml`：

```yaml
TTS:
  GPTSoVITSTTS:
    api_url: http://127.0.0.1:9880
    # ★ 指向【你自己的】参考音频（本仓库不含音频，取法见 ../audio/README.md）
    ref_audio_path: <绝对路径>/你的参考音频.wav
    prompt_text: 参考音频里说的那句话    # ★ 必须与实际内容一致
    # ★ 若你用了微调权重，这三项必须同步改，否则音色走样：
    #   gpt_model / sovits_model / ref_audio_path
```

---

## 3. 训练你自己的音色（可选）

准备好 **5~10 秒**的清晰人声（要求见 `../audio/README.md`），然后：

### 3.1 用 WebUI（推荐新手）

```bash
python webui.py
```

浏览器打开后：

```
① 推理 → 直接上传音频试听（零样本，不用训练）
② 训练 → 切分音频 → ASR 打标 → 训练 SoVITS + GPT
③ 微调好的模型在 GPT_SoVITS/SoVITS_weights/ 和 GPT_weights/
```

### 3.2 训练参数建议（小数据量）

| 参数 | 建议值 | 说明 |
|---|---|---|
| 总轮数 (epochs) | 8~15 | 数据少时别太多，会过拟合 |
| batch size | 4~8 | 显存不够就调小 |
| 保存频率 | 每 2~4 轮 | 便于挑最好的一版 |

> 本音色包就是 **10 轮**训练的结果，可作为参考。

### 3.3 训完怎么用

把产出的两个文件（`.ckpt` + `.pth`）**连同你用的参考音频**一起放到一个目录，
就是一份新的音色包 —— 按第 2 节的方式使用即可。

---

## 4. 常见问题

| 现象 | 原因 / 解法 |
|---|---|
| **第一次合成要等 50 秒** | 正常。要加载 G2PW 多音字模型（约 600MB），之后就 0.5 秒 |
| **音色不对 / 走样** | 检查三件套是否配套：`.ckpt` + `.pth` + `参考音频` 必须成对 |
| **念不出罗马数字 Ⅲ** | GPT-SoVITS 的文本归一化不处理罗马数字（本项目固件侧已附补丁，见 `../server/patches/`） |
| **报错 onnxruntime 找不到 cudart** | CUDA 版本不匹配。要么换匹配的 onnxruntime，要么无视（只影响多音字，不影响速度） |
| **显存不够** | 训练时调小 batch；推理时 `api_v2.py` 加 `-d cpu` 走 CPU（慢但能用） |

---

## 5. 相关链接

- GPT-SoVITS 上游：<https://github.com/RVC-Boss/GPT-SoVITS>
- 本项目的音色来源：B 站「白菜工厂1145号员工」<https://space.bilibili.com/518098961>
- 本项目的参考音频说明：`../audio/README.md`
