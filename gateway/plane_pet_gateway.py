#!/usr/bin/env python3
"""Authenticated Plane Pet WSS bridge with opt-in anonymous analytics."""

import asyncio
import base64
import contextlib
import concurrent.futures
import datetime as dt
import hashlib
import hmac
import html
import json
import ipaddress
import logging
import os
import re
import socket
import secrets
import sqlite3
import statistics
import struct
import time
from pathlib import Path

LISTEN_HOST = os.environ.get("PLANE_PET_GATEWAY_HOST", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("PLANE_PET_GATEWAY_PORT", "32111"))
UDP_HOST = os.environ.get("PLANE_PET_UDP_HOST", "127.0.0.1")
UDP_PORT = int(os.environ.get("PLANE_PET_UDP_PORT", "32110"))
TOKEN_STORE = Path(os.environ.get(
    "PLANE_PET_TOKEN_STORE", "/var/lib/plane-pet/gateway_tokens.db"))
TELEMETRY_STORE = Path(os.environ.get(
    "PLANE_PET_TELEMETRY_STORE", "/var/lib/plane-pet/telemetry.db"))
TELEMETRY_RETENTION_DAYS = max(1, int(os.environ.get(
    "PLANE_PET_TELEMETRY_RETENTION_DAYS", "90")))
ADMIN_PASSWORD_FILE = Path(os.environ.get(
    "PLANE_PET_ADMIN_PASSWORD_FILE", "/etc/plane-pet/gateway-admin.password"))


def load_admin_password():
    direct = os.environ.get("PLANE_PET_ADMIN_PASSWORD", "")
    if direct:
        return direct.strip()
    try:
        return ADMIN_PASSWORD_FILE.read_text(encoding="ascii").strip()
    except (OSError, UnicodeError):
        return ""


ADMIN_PASSWORD = load_admin_password()
# 0 disables only the aggregate admission ceiling, not abuse controls or resource limits.
MAX_CONNECTIONS = int(os.environ.get("PLANE_PET_MAX_CONNECTIONS", "0"))
if MAX_CONNECTIONS < 0:
    raise ValueError("PLANE_PET_MAX_CONNECTIONS must be zero or positive")
MAX_ENROLLMENTS = int(os.environ.get("PLANE_PET_MAX_ENROLLMENTS", "1000"))
MAX_ENROLLMENTS_PER_IP = int(os.environ.get(
    "PLANE_PET_MAX_ENROLLMENTS_PER_IP", "64"))
ENROLLMENT_WINDOW_SECONDS = 3600
MAX_CONNECTIONS_PER_INSTALL = 2
MAX_FRAME = 4096
MAX_TELEMETRY_FRAME = 768
MAX_TELEMETRY_EVENTS_PER_MINUTE = 240
# 180 seconds of simulation plus the bounded confirmation window and one
# broadcast interval. Reject corrupt durations, not legitimate v3 final ACKs.
MAX_GAME_WALL_DURATION_MS = 182000
TELEMETRY_MAGIC = b"PPTELEM1\n"
WEBSOCKET_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CHINA_TZ = dt.timezone(dt.timedelta(hours=8))

ALLOWED_EVENTS = frozenset({
    "app_started", "app_exited", "session_heartbeat",
    "pairing_started", "pairing_stopped", "pairing_matched",
    "binding_missing", "binding_unbound", "unbind_requested",
    "unbind_storage_failed", "pairing_storage_failed", "test_auto_unbind",
    "peer_online", "peer_offline", "invite_sent", "invite_received",
    "invite_waiting", "invite_accepted", "invite_rejected",
    "invite_canceled", "invite_auto_rejected_dnd",
    "invite_accepted_by_peer", "invite_ended_without_game",
    "invite_rejected_by_peer", "invite_timed_out", "game_countdown",
    "game_started", "game_finished", "game_end_reason", "game_outcome",
    "game_abandoned", "game_connection_lost", "game_returned_to_pet",
    "history_recorded", "pet_hidden", "pet_shown", "game_hidden",
    "game_restored", "dnd_enabled", "dnd_disabled",
    "quick_emote_sent", "quick_emote_received",
    "update_check", "update_available", "update_accepted",
    "update_declined", "update_download_started",
    "update_download_completed", "update_verify_failed",
    "update_install_started", "update_install_succeeded",
    "update_install_failed", "update_rollback",
    "peer_version_mismatch", "peer_version_compatible",
    "forced_update_required",
    "usage_started", "usage_heartbeat", "usage_ended",
    "invite_blocked_dnd", "invite_interrupted_dnd",
    "dnd_usage_started", "dnd_usage_heartbeat", "dnd_usage_ended",
})

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
active_connections = 0
established_connections = 0
online_alert_recorder = None
active_installations = {}
active_lock = asyncio.Lock()
telemetry_db = None
telemetry_service = None
last_telemetry_prune = 0.0
telemetry_write_failures = 0
readiness_cache = (0.0, False)
readiness_lock = None


def address_bucket(address: str) -> str:
    """Return a stable abuse-control bucket without persisting a raw IP."""
    secret = (ADMIN_PASSWORD.encode("utf-8") if len(ADMIN_PASSWORD) >= 24
              else b"plane-pet-address-bucket-v1")
    return "h:" + hmac.new(
        secret, address.encode("utf-8"), hashlib.sha256).hexdigest()[:24]


def load_tokens():
    tokens = {}
    migrated = False
    if not TOKEN_STORE.exists():
        return tokens, migrated
    for line in TOKEN_STORE.read_text(encoding="ascii").splitlines():
        fields = line.split(" ")
        if len(fields) == 3 and len(fields[0]) == 64:
            bucket = fields[2]
            if not bucket.startswith("h:"):
                bucket = address_bucket(bucket)
                migrated = True
            tokens[fields[0]] = (fields[1], bucket)
    return tokens, migrated


TOKENS, TOKENS_NEED_REWRITE = load_tokens()


def token_digest(authorization: str):
    prefix = "Bearer "
    if not authorization.startswith(prefix):
        return None
    token = authorization[len(prefix):]
    if len(token) != 64 or any(ch not in "0123456789abcdef" for ch in token):
        return None
    return hashlib.sha256(token.encode("ascii")).hexdigest()


def persist_tokens(tokens=None) -> None:
    tokens = TOKENS if tokens is None else tokens
    TOKEN_STORE.parent.mkdir(parents=True, exist_ok=True)
    temporary = TOKEN_STORE.with_suffix(".tmp")
    with temporary.open("w", encoding="ascii", newline="\n") as output:
        for digest, (created, address) in sorted(tokens.items()):
            output.write(f"{digest} {created} {address}\n")
        output.flush()
        os.fsync(output.fileno())
    temporary.chmod(0o600)
    os.replace(temporary, TOKEN_STORE)


if TOKENS_NEED_REWRITE:
    persist_tokens()


def open_telemetry_store(path: Path = TELEMETRY_STORE):
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path, timeout=0.2)
    connection.execute("PRAGMA journal_mode=WAL")
    connection.execute("PRAGMA synchronous=NORMAL")
    connection.execute("PRAGMA busy_timeout=200")
    connection.execute("PRAGMA secure_delete=ON")
    connection.executescript("""
        CREATE TABLE IF NOT EXISTS telemetry_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            installation_id TEXT NOT NULL,
            session_id TEXT NOT NULL,
            event_id INTEGER NOT NULL,
            received_at_ms INTEGER NOT NULL,
            client_at_ms INTEGER NOT NULL,
            app_version TEXT NOT NULL,
            invite_id TEXT NOT NULL,
            round_id TEXT NOT NULL,
            event TEXT NOT NULL,
            value INTEGER NOT NULL,
            UNIQUE(installation_id, session_id, event_id)
        );
        CREATE INDEX IF NOT EXISTS telemetry_received
            ON telemetry_events(received_at_ms);
        CREATE INDEX IF NOT EXISTS telemetry_event_received
            ON telemetry_events(event, received_at_ms);
        CREATE INDEX IF NOT EXISTS telemetry_session
            ON telemetry_events(installation_id, session_id);
    """)
    # Additive migration: old events retain their historical interpretation.
    columns = {row[1] for row in connection.execute('PRAGMA table_info(telemetry_events)')}
    if 'release_epoch' not in columns:
        connection.execute('ALTER TABLE telemetry_events ADD COLUMN release_epoch INTEGER NOT NULL DEFAULT 0')
    connection.commit()
    return connection


def _valid_hex_id(value) -> bool:
    return (isinstance(value, str) and len(value) == 16 and
            all(ch in "0123456789abcdef" for ch in value))


def parse_telemetry(payload: bytes):
    if not payload.startswith(TELEMETRY_MAGIC) or len(payload) > MAX_TELEMETRY_FRAME:
        return None
    try:
        item = json.loads(payload[len(TELEMETRY_MAGIC):].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(item, dict) or item.get("v") != 1:
        return None
    event = item.get("e")
    version = item.get("a")
    event_id = item.get("q")
    client_at = item.get("t")
    value = item.get("x")
    epoch = item.get("g", 0)
    if type(epoch) is not int or epoch not in (0, 1):
        return None
    if (not isinstance(event, str) or event not in ALLOWED_EVENTS or not isinstance(version, str) or
            not version or len(version) > 16 or
            any(ch not in "0123456789." for ch in version) or
            not isinstance(event_id, int) or isinstance(event_id, bool) or
            event_id < 1 or event_id > 0x7fffffffffffffff or
            not isinstance(client_at, int) or isinstance(client_at, bool) or
            client_at < 0 or client_at > 0x7fffffffffffffff or
            not isinstance(value, int) or isinstance(value, bool) or
            value < -0x7fffffffffffffff or value > 0x7fffffffffffffff or
            not _valid_hex_id(item.get("s")) or
            not _valid_hex_id(item.get("i")) or
            not _valid_hex_id(item.get("r"))):
        return None
    return (item["s"], event_id, client_at, version, item["i"], item["r"],
            event, value, epoch)


def store_telemetry(digest: str, payload: bytes, received_at_ms=None) -> bool:
    global last_telemetry_prune, telemetry_write_failures
    if telemetry_db is None:
        return False
    event = parse_telemetry(payload)
    if event is None:
        return False
    now_ms = int(time.time() * 1000) if received_at_ms is None else received_at_ms
    try:
        cursor = telemetry_db.execute(
            """INSERT OR IGNORE INTO telemetry_events
               (installation_id, session_id, event_id, received_at_ms,
                client_at_ms, app_version, invite_id, round_id, event, value, release_epoch)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (digest[:24], event[0], event[1], now_ms, *event[2:]))
        telemetry_db.commit()
        inserted = cursor.rowcount == 1
    except sqlite3.Error:
        telemetry_write_failures += 1
        telemetry_db.rollback()
        logging.exception("telemetry database write failed")
        return False
    prune_telemetry(now_ms)
    return inserted


def prune_telemetry(now_ms=None, force=False) -> bool:
    global last_telemetry_prune
    if telemetry_db is None:
        return False
    if not force and time.monotonic() - last_telemetry_prune < 60:
        return True
    now_ms = int(time.time() * 1000) if now_ms is None else now_ms
    try:
        cutoff = now_ms - TELEMETRY_RETENTION_DAYS * 86400000
        cursor = telemetry_db.execute(
            "DELETE FROM telemetry_events WHERE received_at_ms < ?", (cutoff,))
        telemetry_db.commit()
        if cursor.rowcount:
            telemetry_db.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        last_telemetry_prune = time.monotonic()
        return True
    except sqlite3.Error:
        telemetry_db.rollback()
        logging.exception("telemetry retention cleanup failed")
        return False


def _count(event: str) -> int:
    return int(telemetry_db.execute(
        "SELECT COUNT(*) FROM telemetry_events WHERE event=?", (event,)).fetchone()[0])


def _distinct(column: str, event: str, value=None) -> int:
    if column not in {"installation_id", "round_id", "invite_id"}:
        raise ValueError("invalid distinct column")
    query = (f"SELECT COUNT(DISTINCT {column}) FROM telemetry_events "
             f"WHERE event=? AND {column}!='0000000000000000'")
    arguments = [event]
    if value is not None:
        query += " AND value=?"
        arguments.append(value)
    return int(telemetry_db.execute(query, arguments).fetchone()[0])


def _format_duration(milliseconds: int) -> str:
    seconds = max(0, milliseconds // 1000)
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)
    if hours:
        return f"{hours} 小时 {minutes} 分"
    if minutes:
        return f"{minutes} 分 {seconds} 秒"
    return f"{seconds} 秒"


def analytics_snapshot(now_ms=None):
    now_ms = int(time.time() * 1000) if now_ms is None else now_ms
    if not prune_telemetry(now_ms, force=True):
        raise sqlite3.OperationalError("retention cleanup unavailable")
    sessions = telemetry_db.execute(
        """SELECT MIN(received_at_ms), MAX(received_at_ms),
                  MAX(CASE WHEN event IN ('usage_started','usage_heartbeat','usage_ended')
                           THEN value END)
           FROM telemetry_events GROUP BY installation_id, session_id""").fetchall()
    durations = []
    duration_cap = TELEMETRY_RETENTION_DAYS * 86400000
    for first, last, enabled_usage in sessions:
        if enabled_usage is not None:
            observed = max(0, min(int(enabled_usage), duration_cap))
        else:
            # Legacy clients lack consent segment counters. Never extrapolate
            # backwards to a process start that predates their consent.
            observed = max(0, min(int(last) - int(first), duration_cap))
        durations.append(observed)
    total_usage = sum(durations)
    # Cumulative consent-enabled running time only. Old sessions lacking this
    # counter contribute no inferred DND duration; duplicate/out-of-order events
    # cannot inflate it. Do not change the database schema or historical rows.
    dnd_durations = telemetry_db.execute(
        """SELECT MAX(value) FROM telemetry_events
           WHERE event IN ('dnd_usage_started','dnd_usage_heartbeat','dnd_usage_ended')
           GROUP BY installation_id, session_id""").fetchall()
    dnd_usage = sum(max(0, min(int(row[0]), duration_cap)) for row in dnd_durations)
    median_usage = int(statistics.median(durations)) if durations else 0
    started_rounds = {row[0] for row in telemetry_db.execute(
        """SELECT DISTINCT round_id FROM telemetry_events
           WHERE event IN ('game_countdown','game_started')
             AND round_id!='0000000000000000'""")}
    playing_rounds = {row[0] for row in telemetry_db.execute(
        "SELECT DISTINCT round_id FROM telemetry_events WHERE event='game_started'")}
    finished_rounds = {row[0] for row in telemetry_db.execute(
        "SELECT DISTINCT round_id FROM telemetry_events WHERE event='game_finished'")}
    reasons = {}
    legacy_finished, connection_lost, abandoned = set(), set(), set()
    for round_id, event, value, version, epoch in telemetry_db.execute(
            """SELECT round_id, event, value, app_version, release_epoch FROM telemetry_events
               WHERE round_id!='0000000000000000' AND event IN
               ('game_finished','game_end_reason','game_connection_lost','game_abandoned')"""):
        if event == 'game_end_reason':
            reasons.setdefault(round_id, set()).add(int(value))
        elif event == 'game_finished' and epoch == 0 and re.fullmatch(r'[0-2]\.\d+\.\d+', version):
            legacy_finished.add(round_id)
        elif event == 'game_connection_lost':
            connection_lost.add(round_id)
        elif event == 'game_abandoned':
            abandoned.add(round_id)
    # MatchEndReason: 1 destroyed, 2 time-limit draw, 3 exit/disconnect,
    # 4 server/synchronization failure. Terminal evidence outranks a client's
    # later timeout. Conflicting terminal reasons never become a normal finish.
    normal = {r for r, values in reasons.items() if values and values <= {1, 2}}
    abnormal = {r for r, values in reasons.items() if 4 in values}
    disconnected = {r for r, values in reasons.items() if 3 in values} - abnormal
    known_terminal = normal | abnormal | disconnected
    aborted_rounds = started_rounds & (abnormal | (connection_lost - known_terminal))
    exited_rounds = (started_rounds & (disconnected | (abandoned - known_terminal))) - aborted_rounds
    # Preserve historical clients lacking reasons, but label their uncertainty.
    # Current clients with a missing reason stay unclassified until it arrives.
    legacy_rounds = (legacy_finished - set(reasons) - connection_lost - abandoned) & started_rounds
    completed_rounds = (started_rounds & finished_rounds & normal) | legacy_rounds
    games_started = len(started_rounds)
    games_completed = len(completed_rounds)
    def invite_ids(event):
        return {row[0] for row in telemetry_db.execute(
            "SELECT DISTINCT invite_id FROM telemetry_events "
            "WHERE event=? AND invite_id!='0000000000000000'", (event,))}
    sent_invites = invite_ids("invite_waiting")
    accepted_invites = invite_ids("invite_accepted_by_peer")
    rejected_invites = invite_ids("invite_rejected_by_peer")
    dnd_interrupted = invite_ids("invite_interrupted_dnd")
    # Legacy auto-rejection is evidence of DND but not the newer atomic outcome.
    legacy_dnd = invite_ids("invite_auto_rejected_dnd") - dnd_interrupted
    explicit_dnd = dnd_interrupted | legacy_dnd
    rejected_invites -= explicit_dnd
    timed_out_invites = invite_ids("invite_timed_out")
    invites = len(sent_invites)
    accepted = len(sent_invites & accepted_invites)
    eligible_invites = sent_invites - (explicit_dnd - accepted_invites)
    orphan_results = len((accepted_invites | rejected_invites | timed_out_invites)
                         - sent_invites)
    game_durations = [(int(value) if round_id in playing_rounds else 0)
                      for round_id, value in telemetry_db.execute(
        """SELECT round_id, MAX(value) FROM telemetry_events
           WHERE event='game_finished' AND round_id!='0000000000000000'
           GROUP BY round_id""").fetchall()
                      if round_id in completed_rounds and value is not None
                      and (round_id not in playing_rounds or 0 <= int(value) <= MAX_GAME_WALL_DURATION_MS)]

    daily_rows = telemetry_db.execute(
        """SELECT received_at_ms, installation_id, session_id, event, round_id
           FROM telemetry_events WHERE received_at_ms>=?""",
        (now_ms - 8 * 86400000,)).fetchall()
    today = dt.datetime.fromtimestamp(now_ms / 1000, CHINA_TZ).date()
    daily = []
    for offset in range(6, -1, -1):
        date = today - dt.timedelta(days=offset)
        rows = [row for row in daily_rows
                if dt.datetime.fromtimestamp(row[0] / 1000, CHINA_TZ).date() == date]
        daily.append({
            "date": date.strftime("%m-%d"),
            "users": len({row[1] for row in rows}),
            "sessions": len({(row[1], row[2]) for row in rows}),
            "games": len({row[4] for row in rows
                          if row[3] == "game_finished" and
                          row[4] in completed_rounds}),
        })

    recent = []
    for received, installation, event, value, version, epoch in telemetry_db.execute(
            """SELECT received_at_ms, installation_id, event, value, app_version, release_epoch
               FROM telemetry_events ORDER BY id DESC LIMIT 40""").fetchall():
        when = dt.datetime.fromtimestamp(received / 1000, CHINA_TZ)
        recent.append((when.strftime("%m-%d %H:%M:%S"), installation[:8] + "…",
                       event, value, ('正式 ' if epoch == 1 else '测试 ') + version))
    distinct_installs = int(telemetry_db.execute(
        "SELECT COUNT(DISTINCT installation_id) FROM telemetry_events").fetchone()[0])
    active = int(telemetry_db.execute(
        "SELECT COUNT(DISTINCT installation_id) FROM telemetry_events "
        "WHERE received_at_ms>=?", (now_ms - 120000,)).fetchone()[0])
    dau = int(telemetry_db.execute(
        "SELECT COUNT(DISTINCT installation_id) FROM telemetry_events "
        "WHERE received_at_ms>=?", (now_ms - 86400000,)).fetchone()[0])
    wau = int(telemetry_db.execute(
        "SELECT COUNT(DISTINCT installation_id) FROM telemetry_events "
        "WHERE received_at_ms>=?", (now_ms - 7 * 86400000,)).fetchone()[0])
    emote_rows = telemetry_db.execute(
        """SELECT value, COUNT(*) FROM telemetry_events
           WHERE event='quick_emote_sent' AND value BETWEEN 1 AND 4
           GROUP BY value""").fetchall()
    emote_counts = {int(value): int(count) for value, count in emote_rows}
    emote_users = int(telemetry_db.execute(
        """SELECT COUNT(DISTINCT installation_id) FROM telemetry_events
           WHERE event='quick_emote_sent'""").fetchone()[0])
    return {
        "capacity_limit": MAX_ENROLLMENTS,
        "capacity_remaining": max(0, MAX_ENROLLMENTS - len(TOKENS)),
        "telemetry_write_failures": telemetry_write_failures,
        "enrolled": len(TOKENS), "consenting_installs": distinct_installs,
        "active": active, "dau": dau, "wau": wau,
        "sessions": len(sessions), "total_usage": total_usage,
        "median_usage": median_usage, "pairing_started": _count("pairing_started"),
        "pairing_matched": _count("pairing_matched"), "invites": invites,
        "accepted": accepted,
        "rejected": len(sent_invites & rejected_invites),
        "timed_out": len(sent_invites & timed_out_invites),
        "invite_orphan_results": orphan_results,
        "invite_acceptance": accepted * 100 // invites if invites else 0,
        "dnd_blocked": _count("invite_blocked_dnd"),
        "dnd_interrupted": len(dnd_interrupted),
        "dnd_legacy_auto_rejected": len(legacy_dnd),
        "dnd_usage": dnd_usage, "dnd_measured_sessions": len(dnd_durations),
        "dnd_adjusted_invites": len(eligible_invites),
        "dnd_adjusted_acceptance": accepted * 100 // len(eligible_invites) if eligible_invites else 0,
        "games_started": games_started, "games_completed": games_completed,
        "games_aborted": len(aborted_rounds), "games_exited": len(exited_rounds),
        "games_legacy_unclassified": len(legacy_rounds),
        "games_unclassified": len((started_rounds & finished_rounds) -
            completed_rounds - aborted_rounds - exited_rounds),
        "game_completion": games_completed * 100 // games_started if games_started else 0,
        "game_average": sum(game_durations) // len(game_durations)
                        if game_durations else 0,
        "wins": len(completed_rounds & {row[0] for row in telemetry_db.execute(
            "SELECT DISTINCT round_id FROM telemetry_events WHERE event='game_outcome' AND value=1")}),
        "draws": len(completed_rounds & {row[0] for row in telemetry_db.execute(
            "SELECT DISTINCT round_id FROM telemetry_events WHERE event='game_outcome' AND value=0")}),
        "emotes_sent": _count("quick_emote_sent"),
        "emotes_received": _count("quick_emote_received"),
        "emote_users": emote_users, "emote_counts": emote_counts,
        "update_checks": _count("update_check"),
        "update_available": _count("update_available"),
        "update_accepted": _count("update_accepted"),
        "update_installed": _count("update_install_succeeded"),
        "update_failed": _count("update_verify_failed") +
                         _count("update_install_failed"),
        "update_rollbacks": _count("update_rollback"),
        "forced_updates": _count("forced_update_required"),
        "daily": daily, "recent": recent,
    }


def render_admin_page() -> bytes:
    data = analytics_snapshot()
    cards = [
        ("当前活跃", data["active"]), ("近 24 小时用户", data["dau"]),
        ("近 7 天用户", data["wau"]), ("同意统计的安装", data["consenting_installs"]),
        ("启动会话", data["sessions"]), ("累计使用", _format_duration(data["total_usage"])),
        ("会话中位时长", _format_duration(data["median_usage"])),
        ("登记凭据", data["enrolled"]), ("开始匹配", data["pairing_started"]),
        ("剩余测试名额", data["capacity_remaining"]),
        ("统计写入失败（本次运行）", data["telemetry_write_failures"]),
        ("当前连接", active_connections),
        ("总连接上限", MAX_CONNECTIONS if MAX_CONNECTIONS else "未设置（保留防滥用限制）"),
        ("统计队列丢弃（本次运行）", telemetry_service.dropped if telemetry_service else 0),
        ("匹配成功", data["pairing_matched"]), ("发出邀请", data["invites"]),
        ("邀请接受率", f'{data["invite_acceptance"]}%'),
        ("开始对局", data["games_started"]), ("完成对局", data["games_completed"]),
        ("对局完成率", f'{data["game_completion"]}%'),
        ("异常中止", data["games_aborted"]), ("退出／掉线结束", data["games_exited"]),
        ("结束原因待确认", data["games_unclassified"]),
        ("平均对局时长", _format_duration(data["game_average"])),
        ("快捷表情发送", data["emotes_sent"]),
        ("快捷表情用户", data["emote_users"]),
        ("检查更新", data["update_checks"]),
        ("发现更新", data["update_available"]),
        ("同意更新", data["update_accepted"]),
        ("更新成功", data["update_installed"]),
        ("更新失败", data["update_failed"]),
        ("自动回滚", data["update_rollbacks"]),
        ("强制兼容更新", data["forced_updates"]),
    ]
    card_html = "".join(
        f'<div class="card"><span>{html.escape(str(label))}</span>'
        f'<strong>{html.escape(str(value))}</strong></div>' for label, value in cards)
    daily_html = "".join(
        f'<tr><td>{row["date"]}</td><td>{row["users"]}</td>'
        f'<td>{row["sessions"]}</td><td>{row["games"]}</td></tr>'
        for row in data["daily"])
    recent_html = "".join(
        "<tr>" + "".join(f"<td>{html.escape(str(cell))}</td>" for cell in row) + "</tr>"
        for row in data["recent"])
    generated = dt.datetime.now(CHINA_TZ).strftime("%Y-%m-%d %H:%M:%S")
    emote_labels = {1: "😂 大笑", 2: "😭 大哭", 3: "😡 生气", 4: "☝ 勾手挑衅"}
    emote_html = " · ".join(
        f"{label} {data['emote_counts'].get(value, 0)}"
        for value, label in emote_labels.items())
    document = f"""<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="60"><title>Plane Pet 匿名统计</title>
<style>body{{font-family:"Microsoft YaHei",sans-serif;background:#09101d;color:#edf6ff;margin:0;padding:28px}}.wrap{{max-width:1120px;margin:auto}}h1{{margin:0 0 6px;color:#71dcff}}.sub{{color:#9fb2c8;margin-bottom:22px}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}}.card{{background:#131e30;border:1px solid #29405f;border-radius:12px;padding:15px}}.card span{{display:block;color:#9fb2c8;font-size:14px}}.card strong{{display:block;font-size:25px;margin-top:7px}}section{{margin-top:24px;background:#101a2a;border-radius:14px;padding:18px}}table{{width:100%;border-collapse:collapse}}th,td{{padding:9px 10px;text-align:left;border-bottom:1px solid #24364d}}th{{color:#74dfff}}.note{{font-size:13px;color:#8da2ba;line-height:1.6}}</style></head><body><div class="wrap">
<h1>Plane Pet 匿名测试统计</h1><div class="sub">生成时间：{generated}（北京时间） · 每 60 秒自动刷新</div>
<div class="grid">{card_html}</div>
<section><h2>最近 7 天</h2><table><thead><tr><th>日期</th><th>用户</th><th>启动会话</th><th>完成对局</th></tr></thead><tbody>{daily_html}</tbody></table></section>
<section><h2>邀请结果</h2><p>接受 {data['accepted']} · 拒绝 {data['rejected']} · 超时 {data['timed_out']} · 胜局 {data['wins']} · 平局 {data['draws']}</p><p>接受率仅计算有发出记录的同一批邀请；缺少发出记录的结果 {data['invite_orphan_results']} 条，不纳入比例。关闭统计、漏报或保留期边界会造成样本不完整，不能将其当作拒绝。</p></section>
<section><h2>快捷表情</h2><p>发送 {data['emotes_sent']} · 好友端收到 {data['emotes_received']} · 使用人数 {data['emote_users']}</p><p>{emote_html}</p></section>
<section><h2>勿扰模式</h2><p>邀请前拦截 {data['dnd_blocked']} 次 · 等待中被勿扰终止 {data['dnd_interrupted']} 次 · 旧版明确自动拒收 {data['dnd_legacy_auto_rejected']} 次</p><p>已授权运行期间的勿扰时长 {_format_duration(data['dnd_usage'])}（{data['dnd_measured_sessions']} 个有计时数据的会话）。未运行、未授权以及旧版缺失的时长不推算。</p><p>原邀请接受率不变；另列排除明确勿扰终止的接受率 {data['dnd_adjusted_acceptance']}%，分母 {data['dnd_adjusted_invites']} 次。邀请前拦截从未进入等待，不计为真实邀请或主动拒绝。旧版未标明原因的历史拒绝保持原分类；双方上报缺失时不能推断原因。</p></section>
<section><h2>最近匿名事件</h2><table><thead><tr><th>时间</th><th>匿名安装</th><th>事件</th><th>数值</th><th>版本</th></tr></thead><tbody>{recent_html}</tbody></table></section>
<section class="note">仅统计明确同意上传的客户端。新版使用时长只累计启用统计期间，由 60 秒心跳及停止/退出事件估算；旧版按实际收到事件的时间范围估算。异常断电可能产生少量误差。开始对局包括已接受邀请的倒数；完成率与平均时长不计异常中止或退出／掉线结束。平均时长使用正常完成对局的实际战斗时间（含确认等待，允许至 182 秒）。旧版缺少结束原因的 {data['games_legacy_unclassified']} 局保留原完成口径，不代表已确认正常结束；当前版本漏报原因单列待确认。双方上报按对局去重，迟到的服务器结算可更正先前断线分类。原始匿名事件保留 {TELEMETRY_RETENTION_DAYS} 天，过期不再展示，每分钟自动清理。不采集匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。</section>
</div></body></html>"""
    return document.encode("utf-8")


def admin_authorized(headers) -> bool:
    if len(ADMIN_PASSWORD) < 24:
        return False
    expected = "Basic " + base64.b64encode(
        f"plane-pet:{ADMIN_PASSWORD}".encode()).decode("ascii")
    return hmac.compare_digest(headers.get("authorization", ""), expected)


async def send_http(writer: asyncio.StreamWriter, status: str, body: bytes,
                    content_type="text/plain; charset=utf-8", extra_headers=()) -> None:
    headers = [f"HTTP/1.1 {status}", f"Content-Type: {content_type}",
               f"Content-Length: {len(body)}", "Connection: close",
               "Cache-Control: no-store", "X-Content-Type-Options: nosniff",
               "X-Frame-Options: DENY",
               "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'"]
    headers.extend(extra_headers)
    writer.write(("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body)
    await writer.drain()


async def read_request(reader: asyncio.StreamReader):
    raw = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=10)
    if len(raw) > 8192:
        raise ValueError("request headers too large")
    lines = raw.decode("latin1").split("\r\n")
    method, path, _ = lines[0].split(" ", 2)
    headers = {}
    for line in lines[1:]:
        if line and ":" in line:
            name, value = line.split(":", 1)
            key = name.strip().lower()
            if key in headers:
                raise ValueError("duplicate request header")
            headers[key] = value.strip()
    return method, path.split("?", 1)[0], headers


async def read_frame(reader: asyncio.StreamReader):
    return await asyncio.wait_for(_read_frame(reader), timeout=90)


async def _read_frame(reader: asyncio.StreamReader):
    first, second = await reader.readexactly(2)
    if first & 0x70:
        raise ValueError("reserved WebSocket bits are set")
    final, opcode, masked = bool(first & 0x80), first & 0x0F, bool(second & 0x80)
    length = second & 0x7F
    if not masked:
        raise ValueError("client frame is not masked")
    if length == 126:
        length = struct.unpack("!H", await reader.readexactly(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", await reader.readexactly(8))[0]
    if length > MAX_FRAME:
        raise ValueError("WebSocket frame is too large")
    if opcode >= 8 and (not final or length > 125):
        raise ValueError("invalid control frame")
    mask = await reader.readexactly(4)
    payload = bytearray(await reader.readexactly(length))
    for index in range(length):
        payload[index] ^= mask[index & 3]
    return final, opcode, bytes(payload)


async def send_frame(writer, opcode, payload, lock) -> None:
    if len(payload) > MAX_FRAME:
        raise ValueError("outgoing frame is too large")
    header = bytearray([0x80 | opcode])
    if len(payload) < 126:
        header.append(len(payload))
    else:
        header.append(126)
        header.extend(struct.pack("!H", len(payload)))
    async with lock:
        writer.write(header + payload)
        await writer.drain()


async def bridge(reader, writer, installation_digest: str) -> None:
    loop = asyncio.get_running_loop()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setblocking(False)
    udp.connect((UDP_HOST, UDP_PORT))
    write_lock = asyncio.Lock()
    telemetry_times = []
    game_times = []

    async def websocket_to_udp():
        while True:
            final, opcode, payload = await read_frame(reader)
            if not final:
                raise ValueError("fragmented messages are not supported")
            if opcode == 0x2:
                if payload.startswith(TELEMETRY_MAGIC):
                    now = time.monotonic()
                    telemetry_times[:] = [stamp for stamp in telemetry_times
                                          if now - stamp < 60]
                    if len(telemetry_times) < MAX_TELEMETRY_EVENTS_PER_MINUTE:
                        telemetry_times.append(now)
                        if telemetry_service is not None:
                            telemetry_service.enqueue(installation_digest, payload)
                elif payload:
                    now = time.monotonic()
                    game_times[:] = [stamp for stamp in game_times if now - stamp < 1]
                    if len(game_times) < 200:
                        game_times.append(now)
                        await loop.sock_sendall(udp, payload)
            elif opcode == 0x8:
                return
            elif opcode == 0x9:
                await send_frame(writer, 0xA, payload, write_lock)
            elif opcode != 0xA:
                raise ValueError("unsupported WebSocket opcode")

    async def udp_to_websocket():
        while True:
            payload = await loop.sock_recv(udp, MAX_FRAME)
            if payload:
                await send_frame(writer, 0x2, payload, write_lock)

    tasks = [asyncio.create_task(websocket_to_udp()),
             asyncio.create_task(udp_to_websocket())]
    try:
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in done:
            task.result()
        for task in pending:
            task.cancel()
    finally:
        for task in tasks:
            task.cancel()
        with contextlib.suppress(Exception):
            await asyncio.gather(*tasks, return_exceptions=True)
        udp.close()
        logging.info("tunnel closed install=%s", installation_digest[:8])


async def probe_backend():
    """Non-mutating authenticated-by-local-boundary UDP readiness probe."""
    global readiness_cache, readiness_lock
    if readiness_lock is None:
        readiness_lock = asyncio.Lock()
    async with readiness_lock:
        now = time.monotonic()
        if now - readiness_cache[0] < 5:
            return readiness_cache[1]
        nonce = secrets.token_bytes(16)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.setblocking(False)
        ok = False
        try:
            if not ipaddress.ip_address(UDP_HOST).is_loopback:
                return False
            udp.connect((UDP_HOST, UDP_PORT))
            loop = asyncio.get_running_loop()
            await loop.sock_sendall(udp, b"PPPROBE1" + nonce)
            reply = await asyncio.wait_for(loop.sock_recv(udp, 64), .75)
            ok = reply == b"PPREADY1" + nonce
        except (OSError, asyncio.TimeoutError):
            pass
        finally:
            udp.close()
        readiness_cache = (time.monotonic(), ok)
        return ok


def runtime_status():
    return {
        "connections": active_connections, "connection_limit": MAX_CONNECTIONS,
        "established_connections": established_connections,
        "online_alerts_recording": online_alert_recorder is not None,
        "online_alerts_pending_persistence": len(online_alert_recorder.pending) if online_alert_recorder else 0,
        "online_alerts_persistence_error": online_alert_recorder.last_error if online_alert_recorder else "",
        "enrollments": len(TOKENS), "enrollment_limit": MAX_ENROLLMENTS,
        "enrollment_remaining": max(0, MAX_ENROLLMENTS - len(TOKENS)),
        "capacity_warning": len(TOKENS) >= MAX_ENROLLMENTS * .8,
        "telemetry_queue": telemetry_service.queue.qsize() if telemetry_service else 0,
        "telemetry_dropped": telemetry_service.dropped if telemetry_service else 0,
        "telemetry_write_failures": telemetry_write_failures,
    }


async def handle_client(reader, writer) -> None:
    global active_connections, established_connections
    peer = writer.get_extra_info("peername")
    client_ip = peer[0] if peer else "unknown"
    digest = None
    counted = False
    established = False
    try:
        method, path, headers = await read_request(reader)
        # Only the loopback proxy is trusted. The rightmost value is safe with
        # both the previous append and the new overwrite Nginx configuration.
        if peer and ipaddress.ip_address(peer[0]).is_loopback:
            forwarded = headers.get("x-forwarded-for", "").rsplit(",", 1)[-1].strip()
            if forwarded:
                client_ip = str(ipaddress.ip_address(forwarded))
        if method == "GET" and path == "/healthz":
            await send_http(writer, "200 OK", b"ok\n")
            return
        if method == "GET" and path == "/readyz":
            # Not published through Nginx. Proxied external requests cannot
            # masquerade as the local monitor through X-Forwarded-For.
            if not peer or not ipaddress.ip_address(client_ip).is_loopback:
                await send_http(writer, "404 Not Found", b"not found\n")
                return
            ready = await probe_backend()
            await send_http(writer, "200 OK" if ready else "503 Service Unavailable",
                            b"ready\n" if ready else b"backend unavailable\n")
            return
        if method == "GET" and path == "/admin/status":
            if not admin_authorized(headers):
                await send_http(writer, "401 Unauthorized", b"authentication required\n")
            else:
                status = runtime_status()
                status["backend_ready"] = await probe_backend()
                await send_http(writer, "200 OK", json.dumps(status).encode(), "application/json")
            return
        if method == "GET" and path == "/admin":
            if not ADMIN_PASSWORD:
                await send_http(writer, "404 Not Found", b"not found\n")
            elif not admin_authorized(headers):
                await send_http(writer, "401 Unauthorized", b"authentication required\n",
                                extra_headers=('WWW-Authenticate: Basic realm="Plane Pet Analytics", charset="UTF-8"',))
            else:
                try:
                    page = await telemetry_service.admin_page() if telemetry_service else None
                    if page is None:
                        raise RuntimeError("analytics worker unavailable")
                    await send_http(writer, "200 OK", page, "text/html; charset=utf-8")
                except Exception:
                    logging.exception("analytics page unavailable")
                    await send_http(writer, "503 Service Unavailable", b"analytics temporarily unavailable\n")
            return
        if method != "GET" or path != "/v1/tunnel":
            await send_http(writer, "404 Not Found", b"not found\n")
            return
        digest = token_digest(headers.get("authorization", ""))
        if digest is None:
            await send_http(writer, "401 Unauthorized", b"unauthorized\n")
            return
        key = headers.get("sec-websocket-key", "")
        try:
            valid_key = len(base64.b64decode(key.encode("ascii"), validate=True)) == 16
        except (ValueError, UnicodeError):
            valid_key = False
        if (headers.get("upgrade", "").lower() != "websocket" or
                "upgrade" not in {part.strip().lower() for part in headers.get("connection", "").split(",")} or
                headers.get("sec-websocket-version") != "13" or not valid_key):
            await send_http(writer, "400 Bad Request", b"valid WebSocket upgrade required\n")
            return
        async with active_lock:
            if ((MAX_CONNECTIONS > 0 and active_connections >= MAX_CONNECTIONS) or
                    active_installations.get(digest, 0) >= MAX_CONNECTIONS_PER_INSTALL):
                await send_http(writer, "503 Service Unavailable", b"connection limit\n",
                                extra_headers=("X-Plane-Pet-Error: connection_limit", "Retry-After: 30"))
                return
            known = any(hmac.compare_digest(digest, stored) for stored in TOKENS)
            if not known:
                client_bucket = address_bucket(client_ip)
                now_seconds = int(time.time())
                enrolled_from_ip = sum(1 for created, address in TOKENS.values()
                                       if str(created).isdigit() and
                                       now_seconds - int(created) < ENROLLMENT_WINDOW_SECONDS and
                                       hmac.compare_digest(address, client_bucket))
                if headers.get("x-plane-pet-enroll") != "1":
                    await send_http(writer, "403 Forbidden", b"enrollment denied\n")
                    return
                if len(TOKENS) >= MAX_ENROLLMENTS:
                    await send_http(writer, "503 Service Unavailable", b"enrollment capacity\n",
                                    extra_headers=("X-Plane-Pet-Error: enrollment_capacity", "Retry-After: 300"))
                    return
                if enrolled_from_ip >= MAX_ENROLLMENTS_PER_IP:
                    await send_http(writer, "429 Too Many Requests", b"enrollment rate limit\n",
                                    extra_headers=("X-Plane-Pet-Error: enrollment_rate_limit", "Retry-After: 300"))
                    return
                updated = dict(TOKENS)
                updated[digest] = (str(now_seconds), client_bucket)
                try:
                    await asyncio.get_running_loop().run_in_executor(None, persist_tokens, updated)
                except OSError:
                    await send_http(writer, "503 Service Unavailable", b"credential store unavailable\n")
                    return
                TOKENS[digest] = updated[digest]
                logging.info("credential enrolled install=%s total=%d",
                             digest[:8], len(TOKENS))
            active_connections += 1
            active_installations[digest] = active_installations.get(digest, 0) + 1
            counted = True
        accept = base64.b64encode(hashlib.sha1(
            (key + WEBSOCKET_MAGIC).encode("ascii")).digest())
        writer.write(b"HTTP/1.1 101 Switching Protocols\r\n"
                     b"Upgrade: websocket\r\nConnection: Upgrade\r\n"
                     b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        await writer.drain()
        established_connections += 1
        established = True
        if online_alert_recorder is not None:
            try:
                online_alert_recorder.observe(established_connections, MAX_CONNECTIONS)
            except Exception:
                logging.exception("online milestone observation failed; tunnel remains available")
        logging.info("tunnel opened install=%s active=%d", digest[:8], active_connections)
        await bridge(reader, writer, digest)
    except (asyncio.IncompleteReadError, asyncio.LimitOverrunError,
            asyncio.TimeoutError, ConnectionError, ValueError):
        pass
    except Exception:
        logging.exception("unexpected gateway error install=%s",
                          digest[:8] if digest else "unknown")
    finally:
        if established:
            established_connections -= 1
        if counted:
            async with active_lock:
                active_connections -= 1
                remaining = active_installations.get(digest, 1) - 1
                if remaining:
                    active_installations[digest] = remaining
                else:
                    active_installations.pop(digest, None)
        writer.close()
        with contextlib.suppress(Exception):
            await writer.wait_closed()


class TelemetryService:
    """One SQLite owner thread and a bounded queue isolate game forwarding."""
    def __init__(self, path=TELEMETRY_STORE, queue_size=2048):
        self.path = path
        self.queue = asyncio.Queue(maxsize=queue_size)
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.tasks = []
        self.page_lock = asyncio.Lock()
        self.cached_page = None
        self.cached_at = 0.0
        self.dropped = 0

    async def run_db(self, function, *args):
        return await asyncio.get_running_loop().run_in_executor(self.executor, function, *args)

    def open_db(self):
        global telemetry_db
        telemetry_db = open_telemetry_store(self.path)
        if not prune_telemetry(force=True):
            logging.warning("initial retention cleanup deferred")

    async def start(self):
        await self.run_db(self.open_db)
        self.tasks = [asyncio.create_task(self.consume()), asyncio.create_task(self.cleanup())]

    def enqueue(self, digest, payload):
        if parse_telemetry(payload) is None:
            return False
        try:
            self.queue.put_nowait((digest, payload))
            return True
        except asyncio.QueueFull:
            self.dropped += 1
            return False

    async def consume(self):
        while True:
            event = await self.queue.get()
            try:
                await self.run_db(store_telemetry, *event)
            except Exception:
                logging.exception("isolated telemetry write failed")
            finally:
                self.queue.task_done()

    async def cleanup(self):
        while True:
            await asyncio.sleep(60)
            try:
                await self.run_db(prune_telemetry, None, True)
            except Exception:
                logging.exception("isolated retention task failed")

    async def admin_page(self):
        async with self.page_lock:
            if self.cached_page is None or time.monotonic() - self.cached_at >= 5:
                self.cached_page = await self.run_db(render_admin_page)
                self.cached_at = time.monotonic()
            return self.cached_page

    async def stop(self):
        with contextlib.suppress(asyncio.TimeoutError):
            await asyncio.wait_for(self.queue.join(), timeout=3)
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        if telemetry_db is not None:
            await self.run_db(telemetry_db.close)
        self.executor.shutdown(wait=False)


async def main() -> None:
    global telemetry_service, online_alert_recorder
    if os.environ.get("PLANE_PET_ONLINE_ALERTS_ENABLED", "0") == "1":
        from online_alerts import Recorder
        online_alert_recorder = Recorder(os.environ.get(
            "PLANE_PET_ONLINE_ALERT_STORE", "/var/lib/plane-pet/online-alerts.db"))
        online_alert_recorder.start()
    service = TelemetryService()
    try:
        await service.start()
        telemetry_service = service
    except Exception:
        logging.exception("analytics disabled; game gateway remains available")
        service.executor.shutdown(wait=False)
    server = await asyncio.start_server(handle_client, LISTEN_HOST, LISTEN_PORT)
    logging.info("gateway ready address=%s:%d udp=%s:%d analytics=%s",
                 LISTEN_HOST, LISTEN_PORT, UDP_HOST, UDP_PORT,
                 "enabled" if ADMIN_PASSWORD else "collection-only")
    try:
        async with server:
            await server.serve_forever()
    finally:
        if online_alert_recorder is not None:
            await online_alert_recorder.stop()
        if telemetry_service is not None:
            await telemetry_service.stop()


if __name__ == "__main__":
    asyncio.run(main())
