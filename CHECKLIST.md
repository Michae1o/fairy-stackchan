# 自检清单（CHECKLIST）

> 两部分：**一、装完怎么确认自己装对了**；**二、已知限制**（哪些没做、哪些有坑）。
> 第三部分是作者发布前用的检查（使用者可无视）。

---

## 一、装完自检

### 1. 固件

```text
□ idf.py build 成功，没有 undefined reference
    报错时先看 INSTALL.md §5 的对照表（真因大概率是 CMakeLists 清单没打上，
    或托管组件没下全 —— 不是「漏了某条 cp」那么简单）

□ build/xiaozhi.bin 存在（只看「编译成功」会被「✅完成但❌没产物」骗）

□ 板卡源码真被编进去了（确认板卡选对了）：
    Linux/WSL：  find build -path "*core-s3*" -name "*.obj" | wc -l
    Windows PS： (Get-ChildItem build -Recurse -Filter *.obj |
                  Where-Object FullName -like '*core-s3*').Count
    ⇒ 实测 11 个左右；**0 个 = 板卡没选对**（见 INSTALL §1.2/§1.4）

□ verify_artifact.py 对产物跑一遍：
    python3 tools/verify_artifact.py build/merged-binary.bin
    ⇒ 双唤醒词 ✅、两个 MCP 工具 ✅、无内网 IP ✅、内嵌 GIF > 0
```

### 2. 设备通电后

```text
□ 串口出现 WS: Connecting to ws://<你的服务器IP>:8000/...（连上服务器）
□ 屏幕有待机表情，且**在动**（GIF 播起来了 —— 不动 ⇒ 素材没打进去）
□ 喊唤醒词能唤醒：「Hi Fairy」和「你好小智」两个都要试
□ 摸头顶      → 开心表情 + 冒爱心
□ 用力甩      → 晕眩转圈
□ 说话        → 嘴一开一合
□ 点一下屏    → 状态栏短暂出现（左 WiFi / 右电量，约 3 秒后隐藏）
□ 按电源键短按 → 切换皮肤（重启约 10 秒）
```

### 3. 服务器 / 控制台

```text
□ http://<服务器IP>:8003/admin → 200，标题「Fairy 控制台」
□ http://<服务器IP>:8003/m    → 200（手机版）
□ http://<服务器IP>:8003/admin/api/state → 200（对话记录 + 设备注册表都通了）
□ 能改大模型 / 人设 / 音色，保存后重启生效
□ 皮肤页：点另一张卡片能切换（点当前那张会提示「已经是」）
□ 控制台开关点完后**不会自己跳回去**（会跳 = 服务器没落盘）
□ 让设备拍张照 → Fairy 能描述看到的东西
    报 "Failed to upload photo" / "MCP错误" ⇒ 见「已知坑」第 1 条
```

---

## 二、已知限制（没做的 / 有坑的）

### 1. 设备拍照识图失败：`Failed to upload photo`

**原因**：视觉接口带 JWT 鉴权，密钥来自配置里的 `server.auth_key`。
**没写这一项**（或写空）时，服务器每次启动会**随机生成新密钥**，
设备缓存的旧 token 立刻失效 ⇒ 401 上传失败。

**排查**：① 服务器日志搜 `mcp/vision` / `401` ② 看 `data/.config.yaml` 的 `server.auth_key`
③ 补上后**服务器和设备都重启一次**（清旧 token）。

```yaml
server:
  auth_key: <一串 32 位以上的随机字符串>   # 生成：python -c "import secrets; print(secrets.token_hex(32))"
```

> ★ 这条其实属于**安装必做**：不填的话，视觉功能会在服务器重启后随机失效。

### 2. 设备端菜单（右侧抽屉）：代码在，但点不中

状态栏最右有个 `☰`，本来可以拉出右侧抽屉改设置；
**实测这个图标的点击热区不生效**（点击常无反应），**未解决**。

⇒ 日常所有可调项都走 **Web 控制台**（`/admin`、手机 `/m`），
设备上只保留「电源键短按切皮肤」这个最常用的操作。

### 3. 舵机跟随（视线跟踪）：只有开关，**没有人脸检测**

控制台有「舵机跟随」开关，但**人脸检测逻辑没实现** —— 打开它不会跟着你的脸转。

> 想实现：摄像头出图 → 人脸检测 → 换算角度 → 驱动舵机。
> `m5stack_core_s3.cc` 里预留了开关位，检测部分要自己写。

### 4. 遥控器（K151-R）：**未实现**

### 5. 情绪词表两套皮肤各认各的

- **几何脸**只认 6 种：`neutral / happy / angry / sad / doubt / sleepy`
- **Fairy GIF** 通过别名表认 23 种

服务器（大模型）下发别的词（如 `relaxed`）时，几何脸会退化成普通脸 —— 影响轻微。
想扩展：改 `stackchan_avatar/avatar/elements/emotion.h` 加映射。

### 6. 唤醒词是**编译期固定**的

写死两个：`Hi Fairy` / `你好小智`（在 `sdkconfig.defaults.esp32s3`），**运行时不能改**。
想换词要自训练 ESP‑SR WakeNet 模型（官方有申请通道，约 5~10 工作日）。

### 7. 采样率警告

串口可能出现 `Server sample rate 16000 does not match device output 24000`。
实听通常没问题；介意就把服务器 TTS 输出采样率调成 24000。

### 8. 版本兼容性

本项目在 **ESP‑IDF v6.1** 下实测通过；v5.x 理论可用但**未实测**。
上游两个仓库都在快速更新 ⇒ `apply_to_*.py` 打不上时看脚本的报错（锚点找不到会直接报，不会静默跳过）。

---

## 三、发布者自检（要发出去时看）

```text
□ CREDITS.md 里的版权声明你同意
□ 包内没有 API Key            搜 "api_key" / "sk-" / "Bearer"（应只在文档示例里）
□ 包内没有内网 IP             搜 "192.168"（应只有 192.168.4.1 这个 SoftAP 默认地址）
□ 包内没有本机路径            搜 "E:\" / "C:\Users"（0 命中）
□ 包内没有大文件              find . -size +50M（0 命中；GitHub 单文件硬限 100MB）
□ 试一遍 INSTALL.md            ★ 最可靠：重新 clone 一份上游，照 INSTALL 走一遍
□ 跑一遍包校验                python3 tools/verify-opensource.py（应全绿）
```

### 体积构成

```text
代码 + 文档 ≈ 2~3 MB          ← 可直接 push
不含：微调权重（单个 148MB）、参考音频、Fairy GIF
      ⇒ 版权原因 + GitHub 限制，用法见 voice-package/README.md 与 fairy-assets/README.md
```

### 确实要带上权重时

```text
甲 Git LFS        git lfs track "*.ckpt" "*.pth"（免费 1GB 存储/1GB 月流量；别人 clone 要装 lfs）
乙 Releases（推荐）仓库只放代码文档，权重 zip 传 Releases（单文件限 2GB），不占 git 配额
⚠️ 无论哪种，先确认音色版权（见 CREDITS.md）
```
