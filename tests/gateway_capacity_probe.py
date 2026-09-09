"""Bounded local WebSocket/UDP transport capacity test, no ECS or real IDs."""
import argparse
import asyncio
import importlib.util
import json
import logging
import os
from pathlib import Path
import statistics
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


async def run(folder, count, unlimited=False):
    os.environ['PLANE_PET_TOKEN_STORE'] = str(folder / 'tokens.db')
    os.environ['PLANE_PET_TELEMETRY_STORE'] = str(folder / 'telemetry.db')
    os.environ['PLANE_PET_ADMIN_PASSWORD_FILE'] = str(folder / 'no-password')
    os.environ['PLANE_PET_MAX_CONNECTIONS'] = '0' if unlimited else str(count)
    spec = importlib.util.spec_from_file_location('capacity_gateway', ROOT / 'gateway/plane_pet_gateway.py')
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)
    logging.getLogger().setLevel(logging.WARNING)
    g.TOKENS = {g.token_digest(f'Bearer {index:064x}'): ('1', 'synthetic') for index in range(1, count + 2)}
    original_tokens = dict(g.TOKENS)

    class Echo(asyncio.DatagramProtocol):
        def connection_made(self, transport): self.transport = transport
        def datagram_received(self, data, address): self.transport.sendto(data, address)

    udp, _ = await asyncio.get_running_loop().create_datagram_endpoint(Echo, local_addr=('127.0.0.1', 0))
    g.UDP_HOST, g.UDP_PORT = udp.get_extra_info('sockname')[:2]
    server = await asyncio.start_server(g.handle_client, '127.0.0.1', 0, backlog=1024)
    port = server.sockets[0].getsockname()[1]
    connections = []
    all_writers = []
    samples = []

    async def open_client(index, accepted=True):
        reader, writer = await asyncio.open_connection('127.0.0.1', port)
        all_writers.append(writer)
        writer.write((f'GET /v1/tunnel HTTP/1.1\r\nAuthorization: Bearer {index:064x}\r\n'
            'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n'
            'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n').encode())
        await writer.drain()
        reply = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 5)
        if accepted:
            assert reply.startswith(b'HTTP/1.1 101 '), reply.splitlines()[0]
        else:
            assert reply.startswith(b'HTTP/1.1 503 ') and b'Retry-After: 30' in reply
            writer.close()
            await writer.wait_closed()
        return reader, writer

    async def echo_client(connection, number):
        reader, writer = connection
        payload = b'PP-CAPACITY-' + number.to_bytes(4, 'big')
        mask = os.urandom(4)
        frame = bytes((0x82, 0x80 | len(payload))) + mask + bytes(x ^ mask[i % 4] for i, x in enumerate(payload))
        started = time.perf_counter()
        writer.write(frame)
        await writer.drain()
        reply = await asyncio.wait_for(reader.readexactly(2 + len(payload)), 5)
        assert reply == bytes((0x82, len(payload))) + payload
        samples.append((time.perf_counter() - started) * 1000)

    async def wait_count(expected):
        for _ in range(100):
            if g.active_connections == expected: return
            await asyncio.sleep(.01)
        raise AssertionError(f'Connection accounting: {g.active_connections}, expected {expected}')

    start, cpu = time.perf_counter(), time.process_time()
    try:
        connections.extend(await asyncio.gather(*(open_client(index) for index in range(1, count + 1))))
        assert g.active_connections == count
        extra = await open_client(count + 1, unlimited)
        if unlimited:
            await wait_count(count + 1)
            await echo_client(extra, 999)
            extra[1].close()
            await extra[1].wait_closed()
            await wait_count(count)
        for batch in range(20):
            await asyncio.gather(*(echo_client(connection, batch) for connection in connections))
            await asyncio.sleep(.05)
        for wave in range(3):
            for _, writer in connections[:16]: writer.close()
            await asyncio.gather(*(writer.wait_closed() for _, writer in connections[:16]))
            await wait_count(count - 16)
            connections[:16] = await asyncio.gather(*(open_client(index) for index in range(1, 17)))
            await wait_count(count)
            await asyncio.gather(*(echo_client(connection, 100 + wave) for connection in connections))
        assert g.TOKENS == original_tokens and not (folder / 'tokens.db').exists()
    finally:
        for writer in all_writers: writer.close()
        await asyncio.gather(*(writer.wait_closed() for writer in all_writers), return_exceptions=True)
        server.close()
        await server.wait_closed()
        await wait_count(0)
        assert not g.active_installations
        udp.close()
    elapsed = time.perf_counter() - start
    samples.sort()
    result = dict(connections=count, connection_limit=g.MAX_CONNECTIONS,
        extra_connection_rejected=not unlimited, extra_connection_accepted=unlimited, reconnect_waves=3,
        reconnects=48, roundtrips=len(samples), rtt_median_ms=round(statistics.median(samples), 2),
        rtt_p95_ms=round(samples[int(len(samples) * .95)], 2), elapsed_seconds=round(elapsed, 2),
        combined_client_gateway_cpu_seconds=round(time.process_time() - cpu, 2),
        final_connections=g.active_connections, tokens_unchanged=True,
        scope='local plaintext WebSocket+UDP echo; excludes TLS, Nginx and game simulation')
    print('LOCAL_CAPACITY_PROBE_OK ' + json.dumps(result), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--connections', type=int, default=500, choices=range(16, 501), metavar='16..500')
    parser.add_argument('--unlimited', action='store_true', help='Verify limit=0 admits client 501 as well')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='plane-pet-capacity-') as folder:
        asyncio.run(asyncio.wait_for(run(Path(folder), args.connections, args.unlimited), 45))
