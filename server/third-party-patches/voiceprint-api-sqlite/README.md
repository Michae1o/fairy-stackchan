# voiceprint-api 的改造：MySQL → SQLite

本目录是 [`xinnan-tech/voiceprint-api`](https://github.com/xinnan-tech/voiceprint-api)
（**Apache License 2.0**）的 **3 个改造后文件**，用来把它的存储从 **MySQL 换成 SQLite**。

---

## 为什么要改

原版需要额外跑一个 **MySQL 服务**（又一个常驻进程 + 几百 MB 内存 + 一份下载）。
而声纹特征其实很小 —— **每人约 2 KB**，一个 SQLite 文件完全够用。
换掉之后：**少一个常驻服务**，对家用小机器更友好。

★ 还有一个硬理由：本项目的控制台「声纹」页要显示**哪些人已注册**，
它是**直接读这个 SQLite 库文件**的（上游没有「列出已注册」这种接口）。
⇒ **不覆盖这三个文件的话，控制台里所有人都会显示「未注册」。**

---

## 改了什么（逐文件）

| 文件 | 改动 |
|---|---|
| `app/core/config.py` | 新增 `sqlite` 配置属性（库文件路径 / 是否启用）；原有 MySQL 配置项保持不动 |
| `app/database/connection.py` | 存储层换成 SQLite。★ **保持 `get_cursor()` 接口不变** ⇒ 上层业务代码一行都不用改 |
| `app/database/voiceprint_db.py` | 建表与读写语句按 SQLite 语法调整（用 `sqlite3` 而不是 `pymysql`） |

**改动原则：只动存储层，业务逻辑与对外接口不动。**
所以将来上游更新了 `voiceprint_service.py` 或 API，直接用它新版即可。

---

## 怎么用

**Linux / macOS**

```bash
git clone https://github.com/xinnan-tech/voiceprint-api
cp -r third-party-patches/voiceprint-api-sqlite/app/*  voiceprint-api/app/
```

**Windows（PowerShell）**

```powershell
git clone https://github.com/xinnan-tech/voiceprint-api
Copy-Item third-party-patches\voiceprint-api-sqlite\app\* voiceprint-api\app\ -Recurse -Force
```

然后按 voiceprint-api 自己的 README 装依赖、启动（首次会下载 3D-Speaker 模型）。

**覆盖完核对一下是否真的生效：**

```bash
grep -n "sqlite3" voiceprint-api/app/database/connection.py    # 应【命中】
grep -n "pymysql" voiceprint-api/app/database/connection.py    # 应【不】命中
```

---

## 许可与署名（★ 按 Apache-2.0 的要求）

- 原项目：[`xinnan-tech/voiceprint-api`](https://github.com/xinnan-tech/voiceprint-api)，**Apache License 2.0**
- 本目录的文件是它的**修改版**，**版权仍归原作者**；
  许可全文见 [`../../licenses/Apache-2.0.txt`](../../licenses/Apache-2.0.txt)
- **修改声明**（Apache-2.0 §4(b)）：存储层由 MySQL 改为 SQLite；
  新增 `sqlite` 配置项；**未改动**业务逻辑与对外接口。
- 若上游仓库带有 `NOTICE` 文件，请一并保留。
  本项目分发时**未包含它的源码本体**，只提供这三个改动文件 + 本说明。

> 也就是说：**这个目录里的代码不是本项目的作者写的**，
> 作者只是把它的存储层换成了 SQLite，版权与许可义务都按上面处理。
