"""数据库连接管理（SQLite 版）

★ 自改说明（2026-09-22，改自上游 pymysql/MySQL 实现）
  原版用 pymysql 连 MySQL，意味着要额外跑一个 MySQL/MariaDB 常驻服务、
  建库建表、管账号密码。而本项目实际只存几个声纹特征向量（每人几百字节、
  最多几条记录）——SQLite 完全够用，且：
    · 免安装、免服务、零运维（少一个常驻进程）
    · 表自动创建（原版要求手工执行 CREATE TABLE）
    · 单文件存储，备份=拷一个文件
  ⇒ 改为 SQLite。

★ 接口保持与原版完全一致：外部只需 `db_connection.get_cursor()` 上下文
  管理器（voiceprint_db.py / services / api 各层无需改动，只需 SQL 方言）。

★ 配置：data/.voiceprint.yaml 里可选 `sqlite: { path: data/voiceprints.db }`，
  不写则用默认路径 data/voiceprints.db。
"""
import sqlite3
from typing import Optional
from contextlib import contextmanager
from pathlib import Path

from ..core.config import settings
from ..core.logger import get_logger

logger = get_logger(__name__)


class DatabaseConnection:
    """数据库连接管理类（SQLite）"""

    DEFAULT_PATH = "data/voiceprints.db"

    def __init__(self):
        self._connection: Optional[sqlite3.Connection] = None
        self._connect()
        self._ensure_schema()

    def _db_path(self) -> str:
        """解析数据库文件路径（相对当前工作目录，与配置文件的解析方式一致）"""
        cfg = settings.sqlite or {}
        path = cfg.get("path") or self.DEFAULT_PATH
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        return path

    def _connect(self) -> None:
        """建立数据库连接"""
        try:
            self._connection = sqlite3.connect(
                self._db_path(),
                # FastAPI/uvicorn 会用不同线程处理请求，SQLite 默认禁止跨线程
                check_same_thread=False,
                timeout=30,
            )
            # WAL 模式：读写并发更稳（单人使用其实用不上，但没坏处）
            self._connection.execute("PRAGMA journal_mode=WAL")
            logger.success("数据库连接成功 (SQLite)")
        except Exception as e:
            logger.fail(f"数据库连接失败: {e}")
            raise

    def _ensure_schema(self) -> None:
        """确保表存在（原版靠手工建表；SQLite 下自动完成更省事）

        与原版 MySQL 建表语句等价：
            id INT AUTO_INCREMENT PRIMARY KEY
            speaker_id VARCHAR(255) NOT NULL UNIQUE
            feature_vector LONGBLOB NOT NULL
            created_at / updated_at TIMESTAMP
            INDEX idx_speaker_id (speaker_id)
        """
        try:
            with self.get_cursor() as cursor:
                cursor.execute(
                    """
                    CREATE TABLE IF NOT EXISTS voiceprints (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        speaker_id TEXT NOT NULL UNIQUE,
                        feature_vector BLOB NOT NULL,
                        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                    )
                    """
                )
                cursor.execute(
                    "CREATE INDEX IF NOT EXISTS idx_speaker_id "
                    "ON voiceprints(speaker_id)"
                )
            logger.info("voiceprints 表已就绪")
        except Exception as e:
            logger.fail(f"初始化数据表失败: {e}")
            raise

    @contextmanager
    def get_cursor(self):
        """获取数据库游标的上下文管理器（接口与原版一致）

        与原版的差别：commit 移到这里统一做（sqlite3 需要显式提交），
        调用方无需改动。
        """
        if not self._connection:
            self._connect()

        cursor = None
        try:
            cursor = self._connection.cursor()
            yield cursor
            self._connection.commit()
        except Exception as e:
            logger.fail(f"数据库操作失败: {e}")
            if self._connection:
                self._connection.rollback()
            raise
        finally:
            if cursor:
                cursor.close()

    def close(self) -> None:
        """关闭数据库连接"""
        if self._connection:
            self._connection.close()
            self._connection = None
            logger.info("数据库连接已关闭")

    def __del__(self):
        """析构函数，确保连接被关闭"""
        try:
            self.close()
        except Exception:
            pass  # 忽略析构时的异常


# 全局数据库连接实例
db_connection = DatabaseConnection()
