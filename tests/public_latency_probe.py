"""Read-only public RTT probe through two real Windows TLS tunnels.

Uses copies of existing encrypted QA credentials, never enrolls/pairs/invites,
and only sends the server's nonce readiness echo. Original profiles are hashed
before/after and temporary credential copies are removed on exit.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import secrets
import select
import shutil
import socket
import statistics
import struct
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def fingerprints(folder):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in folder.iterdir() if p.is_file() and p.suffix in
            ('.credential', '.binding', '.settings', '.history')}


def valid_credential(path):
    class Blob(ctypes.Structure):
        _fields_ = [('size', ctypes.c_ulong), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    data = path.read_bytes()
    buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    source, plain = Blob(len(data), buffer), Blob()
    if not ctypes.windll.crypt32.CryptUnprotectData(
            ctypes.byref(source), None, None, None, None, 1, ctypes.byref(plain)):
        raise RuntimeError('Existing QA credential cannot decrypt in this user context')
    try:
        token = ctypes.string_at(plain.data, plain.size)
        if len(token) != 64 or any(c not in b'0123456789abcdef' for c in token):
            raise RuntimeError('Invalid existing QA credential')
    finally:
        ctypes.memset(plain.data, 0, plain.size)
        ctypes.windll.kernel32.LocalFree(plain.data)


def verify_release_wire(sock, destination):
    # A resume for a random nonexistent binding does not create a room, waiting
    # record, enrollment or telemetry. It tests the live V3/V4 response envelope.
    for wire, epoch, version in ((3, 0, (3, 0, 12)), (4, 1, (1, 0, 0))):
        packet = bytearray(48)
        packet[:4] = bytes((80, 66, wire, 3))
        device, request, binding = (secrets.randbelow(0xFFFFFFFE) + 1 for _ in range(3))
        struct.pack_into('<IIIIII', packet, 4, device, request, 0, binding, 1, 1)
        packet[32] = epoch << 4
        packet[34:37] = bytes(version)
        crc = 0xFFFF
        for byte in packet[:38]:
            crc ^= byte << 8
            for _ in range(8):
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        struct.pack_into('<H', packet, 38, crc)
        sock.sendto(packet, destination)
        ready, _, _ = select.select([sock], [], [], 5)
        if not ready:
            raise RuntimeError(f'No live pairing wire {wire} response')
        reply, _ = sock.recvfrom(4096)
        if (len(reply) != 48 or reply[:4] != bytes((80, 66, wire, 4)) or
                struct.unpack_from('<I', reply, 4)[0] != device or
                struct.unpack_from('<I', reply, 8)[0] != request or (reply[32] & 15) != 5):
            raise RuntimeError(f'Unexpected live pairing wire {wire} response')
    return True


def measure(binary, scope, credentials, url, seconds, release_wire=False):
    profile = scope / 'PlanePet'
    profile.mkdir(parents=True)
    processes, sockets, copies, logs, destinations = [], [], [], [], []
    try:
        for i, credential in enumerate(credentials):
            instance = f'latency-{scope.parent.name[-8:]}-{i}'
            copy = profile / f'tunnel-{instance}.credential'
            shutil.copyfile(credential, copy)
            copies.append(copy)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port_probe:
                port_probe.bind(('127.0.0.1', 0))
                port = port_probe.getsockname()[1]
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(('127.0.0.1', 0))
            sock.setblocking(False)
            sockets.append(sock)
            destinations.append(('127.0.0.1', port))
            env = dict(os.environ, LOCALAPPDATA=str(scope))
            processes.append(subprocess.Popen([str(binary), f'--instance={instance}',
                '--enroll=0', f'--local-port={port}', f'--url={url}'], env=env,
                creationflags=subprocess.CREATE_NO_WINDOW))
            logs.append(profile / f'tunnel-{instance}.log')
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            if any(p.poll() is not None for p in processes):
                raise RuntimeError('Isolated tunnel exited before connection')
            if all(p.exists() and ' connected error=0' in p.read_text() for p in logs):
                break
            time.sleep(.1)
        else:
            raise RuntimeError('TLS connection deadline exceeded')
        print(f'CONNECTED {scope.name} pids={[p.pid for p in processes]}', flush=True)
        if release_wire:
            for sock, destination in zip(sockets, destinations):
                verify_release_wire(sock, destination)
            print('PUBLIC_LEGACY_V3_AND_RELEASE_V4_RESUME_OK no_binding_created=1', flush=True)
        pending, samples = [{}, {}], [[], []]
        sent = [0, 0]
        start = next_send = time.perf_counter()
        end = start + seconds
        while time.perf_counter() < end + 3:
            now = time.perf_counter()
            if now < end and now >= next_send:
                for i, sock in enumerate(sockets):
                    nonce = secrets.token_bytes(16)
                    pending[i][nonce] = time.perf_counter()
                    sock.sendto(b'PPPROBE1' + nonce, destinations[i])
                    sent[i] += 1
                next_send += .05  # nominal 20 input packets/sec, without cumulative drift
            readable, _, _ = select.select(sockets, [], [], .005)
            for sock in readable:
                data, _ = sock.recvfrom(4096)
                i = sockets.index(sock)
                if len(data) == 24 and data[:8] == b'PPREADY1':
                    timestamp = pending[i].pop(data[8:], None)
                    if timestamp is not None:
                        samples[i].append((time.perf_counter() - timestamp) * 1000)
            if now >= end and not any(pending):
                break
        results = []
        for i, values in enumerate(samples):
            ordered = sorted(values)
            if not values:
                raise RuntimeError('No nonce replies; public RTT not measured')
            results.append(dict(peer=i, sent=sent[i], received=len(values),
                missing=len(pending[i]), min_ms=round(min(values), 2),
                median_ms=round(statistics.median(values), 2),
                p95_ms=round(ordered[min(len(ordered)-1, int(len(ordered)*.95))], 2),
                max_ms=round(max(values), 2), first_ms=[round(x, 2) for x in values[:5]],
                last_ms=[round(x, 2) for x in values[-5:]]))
        return dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    seconds=seconds, peers=results, release_wire_verified=release_wire)
    finally:
        for p in processes:
            if p.poll() is None:
                p.terminate()
            p.wait(timeout=10)
        for sock in sockets:
            sock.close()
        for copy in copies:
            assert copy.resolve().parent == profile.resolve()
            copy.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--allow-public-test', action='store_true', required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--candidate', type=Path, default=ROOT/'dist/PlanePetTunnel.exe')
    parser.add_argument('--seconds', type=int, default=12)
    parser.add_argument('--verify-release-wire', action='store_true')
    parser.add_argument('--url', default='wss://8.166.124.212:32112/v1/tunnel')
    args = parser.parse_args()
    if not 5 <= args.seconds <= 60:
        parser.error('--seconds must be between 5 and 60')
    source = Path(os.environ['LOCALAPPDATA']) / 'PlanePet'
    credentials = [source/f'tunnel-dual-qa-{side}.credential' for side in ('a', 'b')]
    for credential in credentials:
        valid_credential(credential)
    before = fingerprints(source)
    scope = ROOT/'dist'/f'latency-probe-{uuid.uuid4().hex}'
    result = {}
    try:
        for name, binary in [('baseline', args.baseline), ('candidate', args.candidate)]:
            if binary:
                result[name] = measure(binary.resolve(), scope/name, credentials, args.url, args.seconds, args.verify_release_wire)
                print(name + ' ' + json.dumps(result[name]), flush=True)
    finally:
        result['original_profiles_unchanged'] = fingerprints(source) == before
        scope.mkdir(parents=True, exist_ok=True)
        (scope/'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(f'REPORT {scope / "results.json"}', flush=True)
        assert result['original_profiles_unchanged'], 'Original profile changed during probe'


if __name__ == '__main__':
    main()
