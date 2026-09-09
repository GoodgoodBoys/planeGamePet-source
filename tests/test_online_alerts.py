"""Isolated outbox/SMTP tests. No network, live stores or real email credentials."""
import asyncio
import contextlib
import importlib.util
import os
from pathlib import Path
import smtplib
import sqlite3
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location(
    "online_alerts_test", Path(__file__).resolve().parents[1] / "gateway/online_alerts.py")
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


class OutboxTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="plane-pet-alert-test-")
        self.path = Path(self.folder.name) / "alerts.db"

    def tearDown(self):
        self.folder.cleanup()

    def seed(self, thresholds=(100, 200, 500)):
        return a.store_observations(self.path, {
            t: (1000., t, 0, f"<test-{t}@invalid>") for t in thresholds})

    def rows(self):
        with contextlib.closing(a.database(self.path)) as db:
            return [dict(row) for row in db.execute("SELECT * FROM milestones ORDER BY threshold")]

    def factory(self):
        factory = Mock()
        factory.return_value.send_message.return_value = {}
        return factory

    def test_repeat_observation_preserves_first_timestamp_and_message_id(self):
        self.seed((100,))
        a.store_observations(self.path, {100: (9999, 900, 0, "<different@invalid>")})
        self.assertEqual(self.rows()[0]["first_seen"], 1000.)
        self.assertEqual(self.rows()[0]["observed_count"], 100)
        self.assertEqual(self.rows()[0]["message_id"], "<test-100@invalid>")

    def test_all_thresholds_sent_only_once_across_restart(self):
        self.seed()
        factory = self.factory()
        for _ in range(2):
            with contextlib.closing(a.database(self.path)) as db:
                a.recover_interrupted(db)
                for _ in range(4):
                    a.process_one(db, "synthetic-password", now=5000, smtp_factory=factory)
            self.seed()
        self.assertEqual(factory.return_value.send_message.call_count, 3)
        self.assertEqual([r["status"] for r in self.rows()], ["sent"] * 3)

    def test_interrupted_send_quarantined_not_retried(self):
        self.seed((100,))
        with contextlib.closing(a.database(self.path)) as db:
            with db:
                db.execute("UPDATE milestones SET status='sending'")
            a.recover_interrupted(db)
            factory = self.factory()
            self.assertFalse(a.process_one(db, "unused", smtp_factory=factory))
            factory.assert_not_called()
        self.assertEqual(self.rows()[0]["status"], "uncertain")

    def test_login_failure_retries_with_backoff_without_sending(self):
        self.seed((100,))
        factory = self.factory()
        factory.return_value.login.side_effect = smtplib.SMTPAuthenticationError(535, b"private-detail")
        with contextlib.closing(a.database(self.path)) as db:
            self.assertTrue(a.process_one(db, "unused", 5000, factory))
            self.assertFalse(a.process_one(db, "unused", 5001, factory))
            factory.return_value.login.side_effect = None
            self.assertTrue(a.process_one(db, "unused", 5031, factory))
        self.assertEqual(factory.return_value.send_message.call_count, 1)
        self.assertEqual(self.rows()[0]["attempts"], 2)
        self.assertEqual(self.rows()[0]["status"], "sent")

    def test_timeout_during_send_never_automatically_resends(self):
        self.seed((100,))
        factory = self.factory()
        factory.return_value.send_message.side_effect = TimeoutError("private-detail")
        with contextlib.closing(a.database(self.path)) as db:
            a.process_one(db, "unused", 5000, factory)
            a.recover_interrupted(db)
            self.assertFalse(a.process_one(db, "unused", 9999, factory))
        row = self.rows()[0]
        self.assertEqual(row["status"], "uncertain")
        self.assertNotIn("private-detail", row["detail"])

    def test_explicit_data_rejection_4xx_retry_5xx_no_retry(self):
        for code, expected in ((451, "pending"), (554, "failed")):
            factory = self.factory()
            factory.return_value.send_message.side_effect = smtplib.SMTPDataError(code, b"private")
            self.assertEqual(a.deliver(a.make_message(dict(first_seen=1, message_id="<x@y>"), test=True),
                                       "unused", factory), (expected, "data_rejected"))

    def test_close_error_does_not_undo_success(self):
        factory = self.factory()
        factory.return_value.close.side_effect = OSError("closed")
        row = dict(first_seen=1, message_id="<x@y>")
        self.assertEqual(a.deliver(a.make_message(row, test=True), "unused", factory)[0], "sent")

    def test_tls_verification_and_self_mail(self):
        factory = self.factory()
        self.seed((100,))
        a.deliver(a.make_message(self.rows()[0]), "synthetic", factory)
        args, kwargs = factory.call_args
        self.assertEqual(args, ("smtp.163.com", 465))
        self.assertTrue(kwargs["context"].check_hostname)
        self.assertEqual(kwargs["timeout"], 10)
        self.assertEqual(factory.return_value.login.call_args.args, (a.MAILBOX, "synthetic"))
        message = factory.return_value.send_message.call_args.args[0]
        self.assertEqual(message["From"], a.MAILBOX)
        self.assertEqual(message["To"], a.MAILBOX)
        self.assertNotIn("synthetic", message.as_string())

    def test_only_declared_thresholds_are_valid(self):
        with self.assertRaises(sqlite3.IntegrityError):
            self.seed((99,))

    def test_corrupt_database_not_reset(self):
        self.path.write_bytes(b"corrupt persistent milestone archive")
        with self.assertRaises(sqlite3.DatabaseError):
            self.seed()
        self.assertEqual(self.path.read_bytes(), b"corrupt persistent milestone archive")

    def test_future_database_version_not_rewritten(self):
        with contextlib.closing(sqlite3.connect(self.path)) as db:
            db.execute("PRAGMA user_version=2")
        with self.assertRaises(ValueError):
            self.seed()

    def test_password_missing_or_multiline_rejected(self):
        path = Path(self.folder.name) / "synthetic-secret"
        with self.assertRaises(FileNotFoundError):
            a.read_password(path)
        path.write_text("synthetic\nsecond-line", encoding="utf8")
        path.chmod(0o600)
        with self.assertRaises(ValueError):
            a.read_password(path)

    def test_test_mail_does_not_use_or_create_outbox(self):
        text = a.make_message(dict(first_seen=1, message_id="<test@invalid>"), test=True)
        self.assertIn("配置测试", str(text["Subject"]))
        self.assertFalse(self.path.exists())

    def test_two_process_claims_cannot_send_same_row(self):
        self.seed((100,))
        factory = self.factory()
        with contextlib.closing(a.database(self.path)) as first, contextlib.closing(a.database(self.path)) as second:
            def send(*args, **kwargs):
                self.assertFalse(a.process_one(second, "unused", 5000, self.factory()))
                return {}
            factory.return_value.send_message.side_effect = send
            a.process_one(first, "unused", 5000, factory)
        self.assertEqual(self.rows()[0]["attempts"], 1)

    @unittest.skipUnless(os.name == "posix", "Production worker process lock is Linux-only")
    def test_exclusive_sender_lock_prevents_second_worker(self):
        with a.exclusive_sender(self.path):
            with self.assertRaises(BlockingIOError):
                with a.exclusive_sender(self.path):
                    self.fail("Second worker must not enter crash recovery")
        with a.exclusive_sender(self.path):
            pass

    def test_smtp_connect_failure_is_safe_to_retry(self):
        factory = Mock(side_effect=OSError("private diagnostic"))
        result = a.deliver(a.make_message(dict(first_seen=1, message_id="<test@invalid>"), test=True),
                           "unused", factory)
        self.assertEqual(result, ("pending", "OSError"))


class RecorderTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="plane-pet-alert-recorder-")
        self.path = Path(self.folder.name) / "alerts.db"
        self.recorder = a.Recorder(self.path, retry_seconds=.02)

    async def asyncTearDown(self):
        await self.recorder.stop()
        self.folder.cleanup()

    async def test_exact_boundaries_and_transient_peaks(self):
        for count, expected in ((99, []), (100, [100]), (199, [100]), (200, [100, 200]),
                                (499, [100, 200]), (500, [100, 200, 500]), (0, [100, 200, 500])):
            self.recorder.observe(count)
            self.assertEqual(sorted(self.recorder.pending), expected)
        await self.recorder.flush()
        self.assertEqual(self.recorder.recorded, {100, 200, 500})
        self.assertFalse(self.recorder.pending)
        for count in (1, 501, 0, 600):
            self.recorder.observe(count)
        self.assertFalse(self.recorder.pending)

    async def test_leap_records_all_three_and_survives_restart(self):
        self.recorder.observe(501)
        await self.recorder.flush()
        await self.recorder.stop()
        self.recorder = a.Recorder(self.path)
        # Even a connection spike before initial reload cannot overwrite lifetime entries.
        self.recorder.observe(600)
        await self.recorder.flush()
        with contextlib.closing(a.database(self.path)) as db:
            self.assertEqual([row[0] for row in db.execute("SELECT observed_count FROM milestones")], [501]*3)

    async def test_observe_never_touches_disk_and_pending_is_bounded(self):
        with patch.object(a, "store_observations", side_effect=AssertionError("no disk in observe")):
            for count in range(2000):
                self.recorder.observe(count)
        self.assertEqual(len(self.recorder.pending), 3)
        self.assertFalse(self.path.exists())

    async def test_busy_database_does_not_block_event_loop(self):
        await self.recorder.flush()
        with contextlib.closing(sqlite3.connect(self.path)) as db:
            db.execute("BEGIN IMMEDIATE")
            self.recorder.observe(100)
            task = asyncio.create_task(self.recorder.flush())
            started = time.monotonic()
            await asyncio.sleep(.03)
            self.assertLess(time.monotonic() - started, .2)
            db.rollback()
            await task
        self.assertEqual(self.recorder.recorded, {100})

    async def test_persistence_retries_without_losing_peak(self):
        real = a.store_observations
        failures = [True]
        def flaky(*args):
            if failures.pop() if failures else False:
                raise sqlite3.OperationalError("synthetic busy")
            return real(*args)
        with patch.object(a, "store_observations", flaky):
            self.recorder.observe(500)
            self.recorder.start()
            for _ in range(100):
                if len(self.recorder.recorded) == 3:
                    break
                await asyncio.sleep(.01)
        self.assertEqual(self.recorder.recorded, {100, 200, 500})
        self.assertEqual(self.recorder.last_error, "")


if __name__ == "__main__":
    unittest.main()
