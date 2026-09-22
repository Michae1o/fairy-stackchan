# 第三方代码与许可（★ 二次分发时请一起带上本文件）

本项目**自己写的代码**按 MIT 发布（见 [`LICENSE`](LICENSE)）。
这里列的是**本包内用到的别人的东西**、各自的许可，以及必须保留的署名。

---

## 1. Fairy-DSH —— Apache-2.0 ★ 本项目**确实使用了**它的代码

- **来源**：<https://github.com/Chengzhibense/Fairy-DSH>（作者 **Chengzhibense**，B 站「橙汁本色」）
- **许可**：Apache License 2.0 —— 全文见 [`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt)
- **用在哪**：`server/patches/core/api/admin_page.html`（自建服务器的网页控制台 `/admin`）
- **用了什么**（该 HTML 里也逐处标了「抄自 Fairy-DSH」）：
  - 配色 / 风格变量 —— 取自它的 `style.js`（`--dsw-alias-*` 那一套）
  - 吉祥物（眼睛）的 CSS 动画与 SVG 结构
  - 状态切换 / 抖动动画的时序与随机区间 —— 逐字对齐它的 `mascot-runtime.js`
- **我们做的改动**（Apache-2.0 §4(b) 要求的「修改过的文件」声明）：
  - 类名与 `data-` 属性前缀 `dsh-fairy-` → `fv7-`
  - 去掉与官方宿主耦合的部分（mount / lifecycle / token 代际校验 / 低电量分支）
  - 内嵌进控制台的单文件 HTML，并补了默认变量值
- **它的 NOTICE**（Apache-2.0 §4(d) 要求一并保留）：

```text
Fairy-DSH
Copyright 2026 Chengzhibense.

This repository contains original Fairy-DSH code and configuration released
under the Apache License, Version 2.0. Third-party dependencies retain their
own licenses; see THIRD_PARTY_NOTICES.md.

Fairy, DSH, DeepSeek, Zenless Zone Zero, and related names or marks are not
licensed by this NOTICE. See TRADEMARKS.md. Game text, official assets,
private world data, user data, and personal corpora are intentionally not
distributed by this repository.
```

> ⚠️ 它明确声明：**游戏文本 / 官方素材不在它的 Apache-2.0 授权范围内** ——
> 那部分版权属米哈游（见 [`CREDITS.md`](CREDITS.md)）。

> ℹ️ `tools/make_face.py`（用代码画脸的那个脚本）**不是**它的代码：
> 是本项目用 Python 自己写的，只是参数（六层半径 / 颜色 / 动态幅度）取自对参考实现外观的测量。

> ℹ️ **[`docs/console-home.png`](docs/console-home.png)**（`CONSOLE.md` 里那张「控制台长什么样」的截图）
> 拍的是**本项目自己的网页控制台**：界面里那只发光眼睛是本项目自绘的 Fairy 风格图形（内联 SVG），
> 吉祥物样式与动画则来自上面这个项目（Apache-2.0）⇒ 这张截图里含它的代码产物，
> 二次分发时请连本文件一起带上。它**不含任何官方素材**（游戏原画 / 官方图片一张都没有）。
> 详见 [`CREDITS.md`](CREDITS.md) 的「重要声明 ②-0」。

---

## 2. 上游「小智」（MIT）

| 上游项目 | 许可 | 用在哪 |
|---|---|---|
| [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32) | **MIT** | 固件本体 —— 本仓库 `firmware/` 是它之上的改动 |
| [`xinnan-tech/xiaozhi-esp32-server`](https://github.com/xinnan-tech/xiaozhi-esp32-server) | **MIT** | 服务器本体 —— 本仓库 `server/patches/` 是它之上的改动 |

> MIT 要求：分发时保留版权声明与许可文本 ⇒ **带上这一节**。

---

## 3. M5Stack 官方几何脸代码（MIT）

`firmware/board-core-s3/stackchan_avatar/`（眼睛 / 嘴 / 气泡 / 装饰器的绘制代码）
来自 M5Stack 官方 StackChan 固件，**MIT**。可以商用。

---

## 4. Bosch BMI270 SensorAPI（BSD-3-Clause）

`firmware/board-core-s3/drivers/bmi270/BMI270_SensorAPI/` 自带 `LICENSE`（BSD-3-Clause）。

> ★ 本版**没有编译**这些文件（本项目直接读寄存器，符号来自上游的 `espressif/bmi270_sensor` 组件），
> 保留它只为完整性与可追溯。要编译请自行遵守其许可。

---

## 5. GPT-SoVITS（MIT）—— 不随本包

音色引擎属独立项目 [`RVC-Boss/GPT-SoVITS`](https://github.com/RVC-Boss/GPT-SoVITS)（MIT），
本仓库不包含它的代码，只教你接上（见 [`voice-package/README.md`](voice-package/README.md)）。

---

## 6. 不在上述任何许可范围内的东西（版权提醒）

```text
· Fairy 形象 / 《绝区零》角色与素材 —— 版权属【米哈游】
    （Fairy-DSH 的 NOTICE 也点名：游戏文本与官方素材不在它的 Apache-2.0 里）
· 音色 / 参考音频 / 微调权重 —— 版权属【原声者或训练者】
· 本项目【不附带】以上任何素材：没有任何角色美术素材、0 个音频、0 个权重
  （唯一的图片是控制台界面截图 `docs/console-home.png`，见 CREDITS.md ②-0）
```

想商用：**代码部分**（本项目 MIT + 上游 MIT + M5Stack MIT）可以；
**形象 / 音色部分**要你自己解决版权（最干净的做法是用 `tools/make_face.py` 画风格相似的脸、
用你自己的声音录参考音频）。
