import importlib.util
import json
import pathlib
import re
import tempfile
import unittest


GATEWAY_PATH = pathlib.Path(__file__).parents[1] / "gateway" / "plane_pet_gateway.py"
SPEC = importlib.util.spec_from_file_location("plane_pet_gateway_test", GATEWAY_PATH)
gateway = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gateway)


class GatewayTelemetryTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        gateway.telemetry_db = gateway.open_telemetry_store(
            pathlib.Path(self.temporary.name) / "telemetry.db")
        gateway.last_telemetry_prune = 0.0
        gateway.TOKENS = {}

    def tearDown(self):
        gateway.telemetry_db.close()
        self.temporary.cleanup()

    @staticmethod
    def packet(event="app_started", event_id=1, value=0,
               invite="0000000000000000", round_id="0000000000000000",
               version="0.6.7", epoch=None):
        body = {
            "v": 1, "s": "0123456789abcdef", "q": event_id, "t": 1000,
            "a": version, "i": invite, "r": round_id,
            "e": event, "x": value,
        }
        if epoch is not None:
            body['g'] = epoch
        return gateway.TELEMETRY_MAGIC + json.dumps(
            body, separators=(",", ":")).encode()

    def test_valid_event_is_stored_once(self):
        digest = "a" * 64
        self.assertTrue(gateway.store_telemetry(digest, self.packet(), 2000))
        self.assertFalse(gateway.store_telemetry(digest, self.packet(), 3000))
        self.assertEqual(gateway.analytics_snapshot(4000)["sessions"], 1)

    def test_public_1_0_0_is_not_interpreted_as_legacy_result(self):
        for q, event in enumerate(('game_started', 'game_finished'), 1):
            self.assertTrue(gateway.store_telemetry('a' * 64, self.packet(
                event, q, 10000, round_id='1' * 16, version='1.0.0', epoch=1), 2000 + q))
        self.assertEqual(gateway.analytics_snapshot(4000)['games_completed'], 0)
        self.assertTrue(gateway.store_telemetry('a' * 64, self.packet('game_end_reason',
            3, 1, round_id='1' * 16, version='1.0.0', epoch=1), 2003))
        self.assertEqual(gateway.analytics_snapshot(4000)['games_completed'], 1)
        self.assertEqual(gateway.telemetry_db.execute(
            'SELECT DISTINCT release_epoch FROM telemetry_events').fetchall(), [(1,)])

    def test_epoch_is_validated_and_missing_epoch_stays_legacy(self):
        self.assertEqual(gateway.parse_telemetry(self.packet())[-1], 0)
        for value in (True, -1, 2, '1', 1.0):
            self.assertIsNone(gateway.parse_telemetry(self.packet(epoch=value)))

    def test_migration_preserves_existing_rows_and_is_idempotent(self):
        import sqlite3
        path = pathlib.Path(self.temporary.name) / 'old-schema.db'
        db = sqlite3.connect(path)
        db.executescript('''CREATE TABLE telemetry_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT, installation_id TEXT NOT NULL,
            session_id TEXT NOT NULL, event_id INTEGER NOT NULL, received_at_ms INTEGER NOT NULL,
            client_at_ms INTEGER NOT NULL, app_version TEXT NOT NULL, invite_id TEXT NOT NULL,
            round_id TEXT NOT NULL, event TEXT NOT NULL, value INTEGER NOT NULL,
            UNIQUE(installation_id, session_id, event_id));
            INSERT INTO telemetry_events VALUES (1,'install','session',1,1,1,'1.0.0','i','r','app_started',0);''')
        db.close()
        for _ in range(2):
            upgraded = gateway.open_telemetry_store(path)
            try:
                self.assertEqual(upgraded.execute('SELECT id,app_version,release_epoch FROM telemetry_events').fetchall(),
                                 [(1, '1.0.0', 0)])
                self.assertEqual(upgraded.execute('PRAGMA integrity_check').fetchone(), ('ok',))
            finally:
                upgraded.close()

    def test_invalid_or_unapproved_fields_are_rejected(self):
        self.assertIsNone(gateway.parse_telemetry(b"ordinary game packet"))
        self.assertIsNone(gateway.parse_telemetry(self.packet("arbitrary_event")))
        malformed = self.packet().replace(b"0123456789abcdef", b"not-an-id")
        self.assertIsNone(gateway.parse_telemetry(malformed))

    def test_dashboard_deduplicates_two_sides_of_a_game(self):
        round_id = "1111111111111111"
        for index, digest in enumerate(("a" * 64, "b" * 64), 1):
            self.assertTrue(gateway.store_telemetry(
                digest, self.packet("game_started", index, round_id=round_id),
                2000 + index))
            self.assertTrue(gateway.store_telemetry(
                digest, self.packet("game_finished", index + 10, 25000,
                                    round_id=round_id), 3000 + index))
        snapshot = gateway.analytics_snapshot(4000)
        self.assertEqual(snapshot["games_started"], 1)
        self.assertEqual(snapshot["games_completed"], 1)
        self.assertEqual(snapshot["game_average"], 25000)
        self.assertIn("Plane Pet 匿名测试统计", gateway.render_admin_page().decode())

    def test_gateway_allows_every_client_event(self):
        source = (pathlib.Path(__file__).parents[1] / "desktop" / "main.cpp").read_text(
            encoding="utf-8")
        client_events = set()
        for call in re.findall(r"LogEvent\((.*?);", source, flags=re.DOTALL):
            client_events.update(re.findall(r'"([a-z][a-z0-9_]*)"', call))
        self.assertTrue(client_events)
        self.assertEqual(client_events - gateway.ALLOWED_EVENTS, set())

    def test_quick_emotes_are_counted_separately(self):
        digest = "c" * 64
        events = (("quick_emote_sent", 1), ("quick_emote_sent", 4),
                  ("quick_emote_received", 1))
        for event_id, (event, value) in enumerate(events, 1):
            self.assertTrue(gateway.store_telemetry(
                digest, self.packet(event, event_id, value), 2000 + event_id))
        snapshot = gateway.analytics_snapshot(4000)
        self.assertEqual(snapshot["emotes_sent"], 2)
        self.assertEqual(snapshot["emotes_received"], 1)
        self.assertEqual(snapshot["emote_users"], 1)
        self.assertEqual(snapshot["emote_counts"], {1: 1, 4: 1})
        page = gateway.render_admin_page().decode()
        self.assertIn("快捷表情", page)
        self.assertIn("😂 大笑", page)

    def test_enrollment_address_bucket_does_not_store_raw_ip(self):
        first = gateway.address_bucket("203.0.113.25")
        second = gateway.address_bucket("203.0.113.25")
        self.assertEqual(first, second)
        self.assertRegex(first, r"^h:[0-9a-f]{24}$")
        self.assertNotIn("203.0.113.25", first)

    def add_round_event(self, event, q, value=0, round_id='1' * 16, version='3.0.1', digest='a' * 64):
        self.assertTrue(gateway.store_telemetry(digest,
            self.packet(event, q, value, round_id=round_id, version=version), 2000 + q))

    def test_sync_abort_never_completes_or_draws(self):
        for q, (event, value) in enumerate((('game_started', 0), ('game_finished', 5000),
                ('game_end_reason', 4), ('game_outcome', 0)), 1):
            self.add_round_event(event, q, value)
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_started'], s['games_completed'], s['games_aborted']), (1, 0, 1))
        self.assertEqual((s['game_completion'], s['game_average'], s['draws']), (0, 0, 0))
        self.assertIn('异常中止', gateway.render_admin_page().decode())

    def test_duration_confirmation_grace_and_corrupt_values(self):
        for index, duration in enumerate((180000, 180200, 181999, 182000, 182001, 99999999), 1):
            rid = f'{index:016x}'
            for offset, (event, value) in enumerate((('game_started', 0), ('game_finished', duration),
                    ('game_end_reason', 2), ('game_outcome', 0))):
                self.add_round_event(event, index * 10 + offset, value, rid)
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_completed'], s['draws']), (6, 6))
        self.assertEqual(s['game_average'], (180000 + 180200 + 181999 + 182000) // 4)

    def test_timeout_late_authority_and_duplicate_peers(self):
        self.add_round_event('game_countdown', 1)
        self.add_round_event('game_started', 2)
        self.add_round_event('game_connection_lost', 3)
        self.add_round_event('game_abandoned', 4)  # old client guessed a defeat
        self.assertEqual(gateway.analytics_snapshot(4000)['games_aborted'], 1)
        self.add_round_event('game_finished', 5, 180200)
        self.add_round_event('game_outcome', 6, 0)
        self.assertEqual(gateway.analytics_snapshot(4000)['games_completed'], 0)
        self.add_round_event('game_end_reason', 7, 2, digest='b' * 64)
        self.add_round_event('game_finished', 8, 180100, digest='b' * 64)
        self.add_round_event('game_outcome', 9, 0, digest='b' * 64)
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_completed'], s['games_aborted'], s['games_exited'], s['draws']), (1, 0, 0, 1))
        self.assertEqual(s['game_average'], 180200)
        self.assertEqual(sum(d['games'] for d in s['daily']), 1)

    def test_current_missing_or_conflicting_reason_not_normal(self):
        self.add_round_event('game_started', 1)
        self.add_round_event('game_finished', 2, 4000)
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_completed'], s['games_unclassified']), (0, 1))
        self.add_round_event('game_end_reason', 3, 1)
        self.assertEqual(gateway.analytics_snapshot(4000)['games_completed'], 1)
        self.add_round_event('game_end_reason', 4, 4, digest='b' * 64)
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_completed'], s['games_aborted']), (0, 1))

    def test_exit_and_countdown_abort_separate_from_normal(self):
        for index, reason in enumerate((3, 4), 1):
            rid = str(index) * 16
            self.add_round_event('game_countdown', index * 10, round_id=rid)
            self.add_round_event('game_finished', index * 10 + 1, 0, rid)
            self.add_round_event('game_end_reason', index * 10 + 2, reason, rid)
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_started'], s['games_completed'], s['games_exited'], s['games_aborted']), (2, 0, 1, 1))
        self.assertEqual(s['game_average'], 0)

    def test_legacy_unknown_preserved_without_rewriting_records(self):
        self.add_round_event('game_started', 1, version='2.0.0')
        self.add_round_event('game_finished', 2, 25000, version='2.0.0')
        self.add_round_event('game_end_reason', 3, 4, round_id='2' * 16)  # no start: excluded
        before = gateway.telemetry_db.execute('SELECT * FROM telemetry_events').fetchall()
        s = gateway.analytics_snapshot(4000)
        self.assertEqual((s['games_completed'], s['games_legacy_unclassified'], s['games_aborted']), (1, 1, 0))
        self.assertEqual(s['game_average'], 25000)
        self.assertEqual(before, gateway.telemetry_db.execute('SELECT * FROM telemetry_events').fetchall())

    def test_update_events_coexist_with_older_statistics(self):
        digest = "d" * 64
        self.assertTrue(gateway.store_telemetry(
            digest, self.packet("app_started", 1, version="0.6.7"), 2000))
        update_events = (
            "update_check", "update_available", "update_accepted",
            "update_install_succeeded", "forced_update_required")
        for event_id, event in enumerate(update_events, 2):
            self.assertTrue(gateway.store_telemetry(
                digest, self.packet(event, event_id, version="1.0.0"),
                2000 + event_id))
        snapshot = gateway.analytics_snapshot(4000)
        self.assertEqual(snapshot["sessions"], 1)
        self.assertEqual(snapshot["update_checks"], 1)
        self.assertEqual(snapshot["update_available"], 1)
        self.assertEqual(snapshot["update_accepted"], 1)
        self.assertEqual(snapshot["update_installed"], 1)
        self.assertEqual(snapshot["forced_updates"], 1)
        page = gateway.render_admin_page().decode()
        self.assertIn("更新成功", page)
        self.assertIn("强制兼容更新", page)


if __name__ == "__main__":
    unittest.main()
