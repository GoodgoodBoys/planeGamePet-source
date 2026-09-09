#!/usr/bin/env python3
"""Lifetime concurrent-client milestones. No player identifiers or SMTP secrets in DB.

Gateway calls Recorder.observe (memory only). A separate service delivers the outbox.
Ambiguous SMTP outcomes are quarantined rather than automatically sending twice.
"""
import argparse
import asyncio
import concurrent.futures
import contextlib
import datetime as dt
from email.message import EmailMessage
from email.utils import formatdate, make_msgid
import json
import logging
import os
from pathlib import Path
import smtplib
import sqlite3
import ssl
import time

THRESHOLDS = (100, 200, 500)
MAILBOX = "planepet_public@163.com"
DEFAULT_DB = "/var/lib/plane-pet/online-alerts.db"
DEFAULT_PASSWORD = "/etc/plane-pet/plane-pet-smtp.password"


def database(path):
    # Do not silently replace a corrupt database: it contains lifetime deduplication.
    db = sqlite3.connect(path, timeout=1)
    db.row_factory = sqlite3.Row
    try:
        version = db.execute("PRAGMA user_version").fetchone()[0]
        if version not in (0, 1):
            raise ValueError("unsupported online alert database version")
        db.execute("PRAGMA synchronous=FULL")
        db.execute("""CREATE TABLE IF NOT EXISTS milestones (
            threshold INTEGER PRIMARY KEY CHECK(threshold IN (100,200,500)),
            first_seen REAL NOT NULL, observed_count INTEGER NOT NULL,
            connection_limit INTEGER NOT NULL, message_id TEXT NOT NULL UNIQUE,
            status TEXT NOT NULL DEFAULT 'pending'
                CHECK(status IN ('pending','sending','sent','uncertain','failed')),
            attempts INTEGER NOT NULL DEFAULT 0, next_attempt REAL NOT NULL DEFAULT 0,
            submitted_at REAL, detail TEXT NOT NULL DEFAULT '')""")
        db.execute("PRAGMA user_version=1")
        db.commit()
        return db
    except BaseException:
        db.close()
        raise


def store_observations(path, observations):
    with contextlib.closing(database(path)) as db, db:
        for threshold, (timestamp, count, limit, message_id) in observations.items():
            db.execute("""INSERT INTO milestones
                (threshold, first_seen, observed_count, connection_limit, message_id)
                VALUES (?,?,?,?,?) ON CONFLICT(threshold) DO NOTHING""",
                (threshold, timestamp, count, limit, message_id))
        return {row[0] for row in db.execute("SELECT threshold FROM milestones")}


class Recorder:
    """At most three pending entries; all disk work on a dedicated thread."""
    def __init__(self, path=DEFAULT_DB, retry_seconds=5):
        self.path = path
        self.retry_seconds = retry_seconds
        self.pending = {}
        self.recorded = set()
        self.wake = asyncio.Event()
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.task = None
        self.last_error = ""

    def observe(self, count, limit=0):
        for threshold in THRESHOLDS:
            if count >= threshold and threshold not in self.recorded and threshold not in self.pending:
                self.pending[threshold] = (time.time(), count, limit, make_msgid(domain="163.com"))
                self.wake.set()

    def start(self):
        self.task = asyncio.create_task(self.consume())

    async def flush(self):
        batch = dict(self.pending)
        recorded = await asyncio.get_running_loop().run_in_executor(
            self.executor, store_observations, self.path, batch)
        self.recorded.update(recorded)
        for threshold in recorded:
            self.pending.pop(threshold, None)
        self.last_error = ""

    async def consume(self):
        while True:
            self.wake.clear()
            try:
                await self.flush()
            except Exception as error:
                # No raw exception text: safe even for future credential-related changes.
                self.last_error = type(error).__name__
                logging.error("online milestone persistence unavailable: %s", self.last_error)
                await asyncio.sleep(self.retry_seconds)
                continue
            if self.pending:
                continue
            await self.wake.wait()

    async def stop(self):
        if self.task:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
        try:
            await asyncio.wait_for(self.flush(), 3)
        except Exception as error:
            logging.error("online milestone shutdown persistence failed: %s", type(error).__name__)
        self.executor.shutdown(wait=False)


def make_message(row, test=False):
    message = EmailMessage()
    message["From"] = MAILBOX
    message["To"] = MAILBOX
    message["Message-ID"] = row["message_id"]
    message["Date"] = formatdate(row["first_seen"], localtime=False, usegmt=True)
    timestamp = dt.datetime.fromtimestamp(row["first_seen"], dt.timezone(dt.timedelta(hours=8)))
    if test:
        message["Subject"] = "[PlanePet] 邮件告警配置测试（不计入人数阈值）"
        message.set_content("这是 PlanePet 发信配置测试，不代表达到在线人数阈值。\n"
                            "100、200、500 个同时在线客户端的终身首次提醒记录未被修改。\n")
    else:
        message["Subject"] = f"[PlanePet] 首次达到 {row['threshold']} 个同时在线客户端"
        message.set_content(
            f"PlanePet 在线规模提醒\n\n阈值：{row['threshold']} 个同时在线客户端\n"
            f"首次观测时间：{timestamp:%Y-%m-%d %H:%M:%S}（北京时间）\n"
            f"观测连接数：{row['observed_count']}\n"
            "该指标是已完成认证及 WebSocket 握手的连接数，不是注册人数或对战局数。\n"
            "该阈值终身只自动提醒一次；人数回落、服务器重启不会重新提醒。\n"
            "业务总连接硬上限已取消，防滥用和服务器资源限制仍然有效。\n"
            "建议检查 CPU、内存、公网带宽和游戏延迟，再决定是否扩容。\n"
            "本邮件不含玩家身份、IP、匹配码或对战内容。\n")
    return message


def read_password(path):
    location = Path(path)
    if os.name == "posix" and location.stat().st_mode & 0o077:
        raise PermissionError("SMTP credential must not be group/world accessible")
    password = location.read_text(encoding="utf-8-sig").strip()
    if not password or len(password) > 256 or any(ch.isspace() for ch in password):
        raise ValueError("invalid SMTP credential file")
    return password


def deliver(message, password, smtp_factory=smtplib.SMTP_SSL):
    client = None
    data_started = False
    try:
        client = smtp_factory("smtp.163.com", 465, timeout=10, context=ssl.create_default_context())
        client.login(MAILBOX, password)
        data_started = True
        refused = client.send_message(message, from_addr=MAILBOX, to_addrs=[MAILBOX])
        return ("failed", "recipient_refused") if refused else ("sent", "smtp_accepted")
    except smtplib.SMTPDataError as error:
        return ("pending" if 400 <= error.smtp_code < 500 else "failed", "data_rejected")
    except (smtplib.SMTPSenderRefused, smtplib.SMTPRecipientsRefused):
        return "pending", "envelope_rejected"
    except Exception as error:
        # A timeout/disconnect around DATA can occur AFTER the provider accepted mail.
        return ("uncertain" if data_started else "pending", type(error).__name__)
    finally:
        if client is not None:
            # Closing failure cannot undo a successful DATA response. No second send.
            with contextlib.suppress(Exception):
                client.close()


def recover_interrupted(db):
    with db:
        db.execute("UPDATE milestones SET status='uncertain', detail='interrupted_submission' "
                   "WHERE status='sending'")


def process_one(db, password, now=None, smtp_factory=smtplib.SMTP_SSL):
    now = time.time() if now is None else now
    # Commit intent before network I/O; a crash can never cause automatic resubmission.
    with db:
        db.execute("BEGIN IMMEDIATE")
        row = db.execute("SELECT * FROM milestones WHERE status='pending' AND next_attempt<=? "
                         "ORDER BY threshold LIMIT 1", (now,)).fetchone()
        if row is None:
            return False
        message = make_message(row)
        db.execute("UPDATE milestones SET status='sending', attempts=attempts+1 WHERE threshold=?",
                   (row["threshold"],))
    outcome, detail = deliver(message, password, smtp_factory)
    delay = min(3600, 30 * 2 ** min(row["attempts"], 7))
    with db:
        db.execute("""UPDATE milestones SET status=?, detail=?, next_attempt=?, submitted_at=?
            WHERE threshold=? AND status='sending'""",
            (outcome, detail, now + delay, time.time() if outcome == "sent" else None, row["threshold"]))
    logging.info("online milestone threshold=%d outcome=%s detail=%s", row["threshold"], outcome, detail)
    return True


@contextlib.contextmanager
def exclusive_sender(path):
    # Deployed on Linux; process-wide lock also protects crash recovery from a second worker.
    import fcntl
    with open(str(path) + ".sender.lock", "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("worker", "status", "test-mail"))
    parser.add_argument("--db", default=DEFAULT_DB)
    parser.add_argument("--password-file", default=DEFAULT_PASSWORD)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    os.umask(0o077)
    if args.command == "test-mail":
        row = dict(first_seen=time.time(), message_id=make_msgid(domain="163.com"))
        outcome, detail = deliver(make_message(row, test=True), read_password(args.password_file))
        print(json.dumps(dict(outcome=outcome, detail=detail)))
        return 0 if outcome == "sent" else 1
    if args.command == "status":
        # Diagnostic command must not create/reset an absent outbox.
        if not Path(args.db).is_file():
            print(json.dumps(dict(enabled=False, reason="outbox_missing")))
            return 1
        with contextlib.closing(sqlite3.connect(Path(args.db).resolve().as_uri() + "?mode=ro", uri=True)) as db:
            db.row_factory = sqlite3.Row
            print(json.dumps([dict(row) for row in db.execute("SELECT * FROM milestones ORDER BY threshold")]))
        return 0
    with exclusive_sender(args.db), contextlib.closing(database(args.db)) as db:
        recover_interrupted(db)
        while True:
            try:
                password = read_password(args.password_file)
                worked = process_one(db, password)
                del password
            except Exception as error:
                logging.error("online alert worker unavailable: %s", type(error).__name__)
                # A DB write may have failed after sending; quarantine before any next attempt.
                # If this fails too, exit; systemd restarts and retries recovery safely.
                recover_interrupted(db)
                worked = False
            time.sleep(1 if worked else 10)


if __name__ == "__main__":
    raise SystemExit(main())
