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
               version="0.6.7"):
        body = {
            "v": 1, "s": "0123456789abcdef", "q": event_id, "t": 1000,
            "a": version, "i": invite, "r": round_id,
            "e": event, "x": value,
        }
        return gateway.TELEMETRY_MAGIC + json.dumps(
            body, separators=(",", ":")).encode()

    def test_valid_event_is_stored_once(self):
        digest = "a" * 64
        self.assertTrue(gateway.store_telemetry(digest, self.packet(), 2000))
        self.assertFalse(gateway.store_telemetry(digest, self.packet(), 3000))
        self.assertEqual(gateway.analytics_snapshot(4000)["sessions"], 1)

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
