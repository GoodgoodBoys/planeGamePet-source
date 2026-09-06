"""Isolated functional regressions. Never contacts the public service."""
import argparse
import asyncio
import binascii
import importlib.util
import json
import logging
import os
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def control(kind, device, request, code=0, binding=0, low=0, high=0):
    data = bytearray(48)
    struct.pack_into('<2sBB7I', data, 0, b'PB', 3, kind, device, request,
                     code, binding, low, high, 0)
    data[34] = 1
    struct.pack_into('<H', data, 38, binascii.crc_hqx(data[:38], 0xffff))
    return bytes(data)


def game(kind, session, sequence, payload):
    data = struct.pack('<2sBBIIIIH', b'PL', 5, kind, session, sequence,
                       0, 0, len(payload)) + payload
    return data + struct.pack('<H', binascii.crc_hqx(data, 0xffff))


def recv_until(sock, predicate, seconds=2):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        if predicate(data):
            return data
    raise AssertionError('Expected local-server response not received')


def local_server_probes(temp, server_binary):
    reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    reservation.bind(('127.0.0.1', 0))
    port = reservation.getsockname()[1]
    reservation.close()
    log = open(temp / 'server.log', 'wb')
    server = subprocess.Popen([str(server_binary),
                               f'--port={port}', f'--store={temp / "bindings.db"}'],
                              stdout=log, stderr=subprocess.STDOUT,
                              creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    peers = []
    def peer():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
        sock.connect(('127.0.0.1', port))
        sock.settimeout(.1)
        peers.append(sock)
        return sock
    try:
        time.sleep(.3)
        a, b = peer(), peer()
        a.send(control(1, 101, 1001, 481592))
        assert recv_until(a, lambda d: d[:2] == b'PB')[32] == 1
        b.send(control(1, 102, 1002, 481592))
        matched_b = recv_until(b, lambda d: d[:2] == b'PB' and d[32] == 2)
        # Stop before processing A's queued Matched notification.
        a.send(control(2, 101, 1001, 481592))
        recv_until(a, lambda d: d[:2] == b'PB' and d[32] == 3)
        a.send(control(1, 101, 2001, 593621))
        result = recv_until(a, lambda d: d[:2] == b'PB' and struct.unpack_from('<I', d, 8)[0] == 2001)
        assert result[32] == 1, result[32]
        print('PAIR_CANCEL_RACE_OK', flush=True)

        # A fresh pair for countdown/disconnect duration.
        c, d = peer(), peer()
        c.send(control(1, 103, 1003, 592631))
        recv_until(c, lambda p: p[:2] == b'PB' and p[32] == 1)
        d.send(control(1, 104, 1004, 592631))
        mc = recv_until(c, lambda p: p[:2] == b'PB' and p[32] == 2)
        md = recv_until(d, lambda p: p[:2] == b'PB' and p[32] == 2)
        for sock, device, matched in ((c, 103, mc), (d, 104, md)):
            binding, low, high = struct.unpack_from('<III', matched, 16)
            sock.send(control(3, device, 3000 + device, binding=binding, low=low, high=high))
        wc = recv_until(c, lambda p: p[:2] == b'PL' and p[3] == 2)
        wd = recv_until(d, lambda p: p[:2] == b'PL' and p[3] == 2)
        sc, sd = struct.unpack_from('<I', wc, 4)[0], struct.unpack_from('<I', wd, 4)[0]
        time.sleep(1.3)  # idle time must not be counted as game duration
        c.send(game(8, sc, 1, b'\x01'))
        recv_until(d, lambda p: p[:2] == b'PL' and p[3] == 4 and p[30] == 1)
        d.send(game(8, sd, 1, b'\x02'))
        recv_until(c, lambda p: p[:2] == b'PL' and p[3] == 4 and p[30] == 2)
        binding, low, high = struct.unpack_from('<III', md, 16)
        d.send(control(5, 104, 0, binding=binding, low=low, high=high))
        finished = recv_until(c, lambda p: p[:2] == b'PL' and p[3] == 4 and p[30] == 4)
        elapsed = struct.unpack_from('<I', finished, 39)[0]
        assert elapsed == 0, elapsed
        print(f'COUNTDOWN_DISCONNECT_OK elapsed={elapsed}', flush=True)

        # Scoped messages must not carry an old Accept into a new invitation.
        e, f = peer(), peer()
        e.send(control(1, 105, 1005, 592632))
        recv_until(e, lambda p: p[:2] == b'PB' and p[32] == 1)
        f.send(control(1, 106, 1006, 592632))
        me = recv_until(e, lambda p: p[:2] == b'PB' and p[32] == 2)
        mf = recv_until(f, lambda p: p[:2] == b'PB' and p[32] == 2)
        for sock, device, matched in ((e, 105, me), (f, 106, mf)):
            binding, low, high = struct.unpack_from('<III', matched, 16)
            sock.send(control(3, device, 3000 + device, binding=binding, low=low, high=high))
        we = recv_until(e, lambda p: p[:2] == b'PL' and p[3] == 2)
        wf = recv_until(f, lambda p: p[:2] == b'PL' and p[3] == 2)
        se, sf = struct.unpack_from('<I', we, 4)[0], struct.unpack_from('<I', wf, 4)[0]
        def meta(sock, phase):
            return recv_until(sock, lambda p: p[:2] == b'PL' and p[3] == 9 and p[38] == phase)[22:39]
        def scoped(sock, session, seq, operation, action, context):
            sock.send(game(10, session, seq, struct.pack('<IB', operation, action) + context))
        menu = meta(e, 0)
        scoped(e, se, 1, 101, 1, menu)
        waiting = meta(f, 1)
        scoped(e, se, 2, 102, 3, waiting)
        menu = meta(e, 0)
        scoped(e, se, 3, 103, 1, menu)
        next_waiting = meta(f, 1)
        assert waiting != next_waiting
        # A delayed retransmission has a fresh transport sequence, but retains
        # the old operation and invite IDs.
        scoped(f, sf, 1, 201, 2, waiting)
        recv_until(f, lambda p: p[:2] == b'PL' and p[3] == 11)
        current = recv_until(f, lambda p: p[:2] == b'PL' and p[3] == 4)
        assert current[30] == 1
        scoped(f, sf, 2, 202, 2, next_waiting)
        recv_until(e, lambda p: p[:2] == b'PL' and p[3] == 4 and p[30] == 2)
        print('SCOPED_DELAYED_ACCEPT_REJECTED_NEW_ACCEPT_OK', flush=True)

        # Each loopback tunnel endpoint must have an independent control quota.
        time.sleep(1.1)
        load = [peer() for _ in range(45)]
        for i, sock in enumerate(load):
            sock.send(control(1, 500 + i, 5000 + i, 700000 + i))
        time.sleep(.1)
        replies = 0
        for sock in load:
            sock.setblocking(False)
            try:
                if sock.recv(4096)[:2] == b'PB':
                    replies += 1
            except BlockingIOError:
                pass
        assert replies == 45, replies
        print(f'INDEPENDENT_CONTROL_QUOTA_OK: 45 distinct UDP endpoints, {replies} replies', flush=True)
    finally:
        for sock in peers:
            sock.close()
        if server.poll() is None:
            server.terminate()
        server.wait(timeout=5)
        log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path,
                        default=ROOT / 'dist' / ('PlanePetServer.exe' if os.name == 'nt'
                                                else 'plane-pet-server'))
    args = parser.parse_args()
    server_binary = args.server.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="plane-pet-server-regression-") as directory:
        local_server_probes(Path(directory), server_binary)
