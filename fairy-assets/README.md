# fairy-assets/ —— 原来的 Fairy 表情素材就放在这个目录

## 这个目录现在为什么是空的

**作者自己的 Fairy 表情 GIF（`idle.gif` / `thinking.gif`）不随本仓库提供** ——
它们是从游戏里提取/二创的美术文件，版权属于原版权方，不能随开源包分发。

所以这里**只留一个说明**：目录结构照旧，素材你自己准备。

> 你在这台设备上看到的全屏表情，就是这个目录（上游仓库的 `fairy-assets/`）
> 里的 GIF 驱动的 —— 换脸 = 换这里的 GIF，不需要改固件。

## 作者成品长什么样（想复刻就照这个规格做）

| 文件 | 尺寸 | 帧数 | 帧间隔 | 循环 | 体积 |
|---|---|---|---|---|---|
| `idle.gif` | 320×240 | 98 帧 | 50 ms | 无限 | ≈1.1 MB |
| `thinking.gif` | 320×240 | 99 帧 | 50 ms | 无限 | ≈1.0 MB |

情绪别名表 `_emote_aliases.json` 把情绪名映射到这两个文件
（23 个情绪名 → `idle.gif`，另加 `thinking` → `thinking.gif`，共 24 项）——
也就是说**两个 GIF 就够用**，不用为每个情绪各做一张。

## 怎么拿到素材（三选一）

### ① 用仓库里的代码生成（★ 不涉及任何版权素材，一条命令）

```bash
python3 tools/make_face.py --out fairy-assets
```

画出的是「Fairy 风格」的发光眼睛（六层同心环 + 光晕 + 双高光），
参数就在 `tools/make_face.py` 文件顶部：改半径、改颜色、改呼吸幅度即可，
`--tint 0x8a5cf6` 能整体偏色，做成你自己的角色。
生成的规格与上表一致（320×240 / 98 帧 / 50 ms / 无限循环）。

### ② 自己画

规格要求和导出方法见 [`../firmware/README.md`](../firmware/README.md)
的「表情素材（GIF）的硬参数表」与「从矢量图 / 代码导出『设备能吃』的素材」。

### ③ 用现成素材

作者当时的做法是在浏览器里调参数、导出逐帧 PNG、再拼成 GIF
（参数与做法记在 [`../CREDITS.md`](../CREDITS.md) 三之二）。
那份浏览器工具基于参考项目（Fairy-DSH）的代码改的 ⇒ **同属版权线，未随本仓库提供**。

## 放进来之后怎么生效

```bash
# ① 检查规格对不对（尺寸/帧数/体积/是否无限循环）
python3 tools/verify_artifact.py --gif-dir fairy-assets

# ② 把素材接进上游固件（会把 CoreS3 的表情包名改成 fairy）
python3 tools/apply_to_upstream.py <上游 xiaozhi-esp32 目录> --gif-dir fairy-assets
```

⚠️ 本目录里的 GIF **不要 git add**（`.gitignore` 已排除，只有这个 `README.md` 会进仓库）。
