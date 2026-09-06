"""Local-only regression tests; never read or write the ECS stores."""
import asyncio
import importlib.util
import json
from pathlib import Path
import sqlite3
import struct
import tempfile
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "gateway_regressions", Path(__file__).parents[1] / "gateway/plane_pet_gateway.py")
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)


def packet(event="app_started", q=1, value=0, round_id="0" * 16):
    return g.TELEMETRY_MAGIC + json.dumps(dict(
        v=1, s="a" * 16, q=q, t=1000, a="1.0.1",
        i="0" * 16, r=round_id, e=event, x=value)).encode()


class Writer:
    def __init__(self, peer="127.0.0.1"):
        self.peer = peer
        self.output = bytearray()
    def get_extra_info(self, _): return (self.peer, 10001)
    def write(self, data): self.output.extend(data)
    async def drain(self): pass
    def close(self): pass
    async def wait_closed(self): pass


class GatewayAsyncTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="plane-pet-gateway-test-")
        self.path = Path(self.temp.name) / "telemetry.db"
        g.TOKENS = {}
        g.active_connections = 0
        g.active_installations.clear()
        g.active_lock = asyncio.Lock()
        g.last_telemetry_prune = 0
        g.telemetry_db = None
        self.service = None

    async def asyncTearDown(self):
        if self.service:
            await self.service.stop()
        g.telemetry_db = None
        self.temp.cleanup()

    async def request(self, index=1, forwarded="203.0.113.25",
                      valid=True, peer="127.0.0.1"):
        reader, writer = asyncio.StreamReader(), Writer(peer)
        headers = (f"GET /v1/tunnel HTTP/1.1\r\nAuthorization: Bearer {index:064x}\r\n"
                   f"X-Plane-Pet-Enroll: 1\r\nX-Forwarded-For: {forwarded}\r\n")
        if valid:
            headers += ("Upgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n")
        reader.feed_data((headers + "\r\n").encode())
        reader.feed_eof()
        async def bridge(*args): pass
        with patch.object(g, "bridge", bridge):
            await g.handle_client(reader, writer)
        return bytes(writer.output).split(b"\r\n")[0]

    async def test_malformed_upgrade_never_enrolls(self):
        with patch.object(g, "persist_tokens") as persist:
            for index in range(1, 10):
                self.assertIn(b"400", await self.request(index, valid=False))
            self.assertEqual(g.TOKENS, {})
            persist.assert_not_called()

    async def test_enrollment_uses_verified_rightmost_ip(self):
        with patch.object(g, "persist_tokens"):
            self.assertIn(b"101", await self.request(forwarded="198.51.100.1, 203.0.113.25"))
        self.assertEqual(next(iter(g.TOKENS.values()))[1], g.address_bucket("203.0.113.25"))

    async def test_direct_peer_cannot_forge_forwarded_ip(self):
        with patch.object(g, "persist_tokens"):
            self.assertIn(b"101", await self.request(peer="192.0.2.10"))
        self.assertEqual(next(iter(g.TOKENS.values()))[1], g.address_bucket("192.0.2.10"))

    async def test_enrollment_quota_expires_not_lifetime(self):
        g.TOKENS = {f"{n + 100:064x}": (str(int(time.time()) - 7200),
                    g.address_bucket("203.0.113.25")) for n in range(64)}
        with patch.object(g, "persist_tokens"):
            self.assertIn(b"101", await self.request())

    async def test_enrollment_store_failure_not_acknowledged(self):
        with patch.object(g, "persist_tokens", side_effect=OSError("test locked store")):
            self.assertIn(b"503", await self.request())
        self.assertEqual(g.TOKENS, {})

    async def test_installation_connection_limit(self):
        digest = g.token_digest("Bearer " + f"{1:064x}")
        g.active_installations[digest] = g.MAX_CONNECTIONS_PER_INSTALL
        with patch.object(g, "persist_tokens") as persist:
            self.assertIn(b"503", await self.request())
            persist.assert_not_called()

    async def test_locked_sqlite_does_not_block_event_loop(self):
        self.service = g.TelemetryService(self.path)
        await self.service.start()
        lock = sqlite3.connect(self.path)
        lock.execute("BEGIN IMMEDIATE")
        started = time.monotonic()
        try:
            self.assertTrue(self.service.enqueue("a" * 64, packet()))
            await asyncio.sleep(.02)
            self.assertLess(time.monotonic() - started, .15)
        finally:
            lock.rollback()
            lock.close()
        await self.service.queue.join()

    async def test_queue_is_bounded_and_rejects_invalid_event(self):
        service = g.TelemetryService(self.path, queue_size=1)
        try:
            self.assertFalse(service.enqueue("a" * 64, packet([])))
            self.assertTrue(service.enqueue("a" * 64, packet()))
            self.assertFalse(service.enqueue("a" * 64, packet(q=2)))
            self.assertEqual(service.queue.qsize(), 1)
        finally:
            service.executor.shutdown(wait=False)

    async def test_slow_admin_isolated_from_event_loop(self):
        self.service = g.TelemetryService(self.path)
        await self.service.start()
        def slow_page():
            time.sleep(.2)
            return b"ok"
        with patch.object(g, "render_admin_page", slow_page):
            task = asyncio.create_task(self.service.admin_page())
            started = time.monotonic()
            await asyncio.sleep(.02)
            self.assertLess(time.monotonic() - started, .15)
            self.assertEqual(await task, b"ok")

    async def test_real_websocket_udp_survives_bad_stats_and_locked_database(self):
        self.service = g.TelemetryService(self.path)
        await self.service.start()
        class Echo(asyncio.DatagramProtocol):
            def connection_made(self, transport): self.transport = transport
            def datagram_received(self, data, address): self.transport.sendto(data, address)
        udp, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            Echo, local_addr=("127.0.0.1", 0))
        server = await asyncio.start_server(g.handle_client, "127.0.0.1", 0)
        digest = g.token_digest("Bearer " + f"{1:064x}")
        g.TOKENS[digest] = (str(int(time.time())), "test")
        lock = sqlite3.connect(self.path)
        lock.execute("BEGIN IMMEDIATE")
        writer = None
        try:
            with patch.object(g, "UDP_PORT", udp.get_extra_info("sockname")[1]), \
                 patch.object(g, "telemetry_service", self.service):
                reader, writer = await asyncio.open_connection("127.0.0.1", server.sockets[0].getsockname()[1])
                writer.write((f"GET /v1/tunnel HTTP/1.1\r\nAuthorization: Bearer {1:064x}\r\n"
                    "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n").encode())
                await writer.drain()
                response = await reader.readuntil(b"\r\n\r\n")
                self.assertIn(b"101 Switching", response)
                def masked(payload):
                    mask = b"abcd"
                    header = bytes((0x82, 0x80 | len(payload))) if len(payload) < 126 else b"\x82\xfe" + struct.pack("!H", len(payload))
                    return header + mask + bytes(v ^ mask[n % 4] for n, v in enumerate(payload))
                writer.write(masked(packet([])) + masked(packet()) + masked(b"game-probe"))
                await writer.drain()
                header = await asyncio.wait_for(reader.readexactly(2), .3)
                self.assertEqual(header, b"\x82\x0a")
                self.assertEqual(await reader.readexactly(10), b"game-probe")
                writer.close()
                await writer.wait_closed()
                await asyncio.sleep(.02)
        finally:
            lock.rollback()
            lock.close()
            if writer: writer.close()
            server.close()
            await server.wait_closed()
            udp.close()


class StatsRegressionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        g.telemetry_db = g.open_telemetry_store(Path(self.temp.name) / "stats.db")
        g.last_telemetry_prune = 0
        self.now = int(time.time() * 1000)
    def tearDown(self):
        g.telemetry_db.close()
        g.telemetry_db = None
        self.temp.cleanup()
    def add(self, payload, delta=0):
        self.assertTrue(g.store_telemetry("a" * 64, payload, self.now + delta))
    def test_expired_events_removed_without_new_insert(self):
        self.add(packet())
        self.assertEqual(g.analytics_snapshot(self.now + 91 * 86400000)["sessions"], 0)
        self.assertEqual(g.telemetry_db.execute("SELECT COUNT(*) FROM telemetry_events").fetchone()[0], 0)
    def test_legacy_consent_does_not_count_before_first_event(self):
        self.add(packet("session_heartbeat"))
        self.add(packet("app_exited", 2, 3600000), 60000)
        self.assertEqual(g.analytics_snapshot(self.now + 60000)["total_usage"], 60000)
    def test_enabled_segments_sum_and_long_sessions_not_capped_at_day(self):
        for q, (event, value) in enumerate((
            ("usage_started", 0), ("usage_ended", 5000),
            ("usage_started", 5000), ("usage_ended", 25 * 3600000)), 1):
            self.add(packet(event, q, value), q)
        self.assertEqual(g.analytics_snapshot(self.now + 10)["total_usage"], 25 * 3600000)
    def test_countdown_abort_zero_duration_and_consistent_denominator(self):
        self.add(packet("game_countdown", 1, round_id="1" * 16))
        self.add(packet("game_finished", 2, 99999999, "1" * 16), 1)
        self.add(packet("game_finished", 3, 500, "2" * 16), 2)
        stats = g.analytics_snapshot(self.now + 3)
        self.assertEqual(stats["games_started"], 1)
        self.assertEqual(stats["games_completed"], 1)
        self.assertEqual(stats["game_average"], 0)


if __name__ == "__main__":
    unittest.main()
