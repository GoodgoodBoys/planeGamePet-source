"""Explicit opt-in public verification using two real Windows WinHTTP tunnels.

Uses fresh test IDs and an isolated local profile; no personal saved pair is read.
Creates two test gateway credentials. Desktop event logs stay LOCAL (upload off).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import time

from server_edge_test import control, recv_until

ROOT = Path(__file__).resolve().parents[1]


def wait_for(predicate, description, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.1)
    raise AssertionError(description)


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow-public-test', action='store_true', required=True)
    parser.parse_args()
    assert os.name == 'nt'
    task = ROOT / 'dist' / ('public-online-check-' + secrets.token_hex(6))
    task.mkdir()
    print(f'PUBLIC_CHECK_DIRECTORY={task}', flush=True)
    env = dict(os.environ, LOCALAPPDATA=str(task))
    processes, tunnels, clients = [], [], []
    reservations = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
    for sock in reservations:
        sock.bind(('127.0.0.1', 0))
    ports = [sock.getsockname()[1] for sock in reservations]
    for sock in reservations:
        sock.close()
    tokens = [secrets.token_hex(32) for _ in range(2)]
    metadata = {'digests': [hashlib.sha256(t.encode()).hexdigest() for t in tokens],
                'started_at_ms': int(time.time() * 1000), 'telemetry_uploaded': False}
    (task / 'test-identity-manifest.json').write_text(json.dumps(metadata, indent=2))

    def launch(name, arguments):
        info = subprocess.STARTUPINFO()
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = 0
        process = subprocess.Popen([str(ROOT / 'dist' / name), *arguments],
                                   env=env, cwd=task, startupinfo=info,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        processes.append(process)
        return process

    def events(name, event):
        path = task / (name + '.events.csv')
        return path.read_text(encoding='utf-8-sig').count(',' + event + ',') if path.exists() else 0

    code = secrets.randbelow(899999) + 100000
    base = secrets.randbelow(0x3fffffff) + 0x40000000
    metadata['desktop_devices'] = [base, base + 1]
    metadata['protocol_devices'] = [base + 10001, base + 10002]
    (task / 'test-identity-manifest.json').write_text(json.dumps(metadata, indent=2))

    def desktop(index, initial, automated=True):
        name = ('alice', 'bob')[index]
        args = [f'--server=127.0.0.1:{ports[index]}', f'--code={code if initial else 0}',
                f'--state={task / (name + ".binding")}', f'--client={base + index}',
                '--telemetry=1', '--telemetry-upload=0', '--update-enabled=0',
                '--hidden=1', f'--slot={index+1}']
        if automated:
            args.append('--auto-invite=1' if index == 0 else '--auto-accept=1')
        return launch('PlanePetClient.exe', args)

    def cleanup_test_binding():
        path = task / 'alice.binding'
        if not path.exists():
            return
        fields = path.read_text().split()
        assert fields[:2] == ['PLANE_PET_CLIENT', '1']
        device, binding, low, high = map(int, fields[2:6])
        if not binding:
            return
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0))
            sock.connect(('127.0.0.1', ports[0]))
            sock.settimeout(.1)
            for attempt in range(5):
                sock.send(control(6, device, base + 3000, binding=binding, low=low, high=high))
                try:
                    recv_until(sock, lambda p: p[:4] == b'PB\x03\x04' and p[32] == 7)
                    print('PUBLIC_TEST_GUI_BINDING_CLEANUP_OK', flush=True)
                    return
                except AssertionError:
                    if attempt == 4:
                        raise

    try:
        for index in range(2):
            tunnels.append(launch('PlanePetTunnel.exe', [f'--instance=verify-{index}',
                f'--local-port={ports[index]}', f'--token={tokens[index]}']))
        for index in range(2):
            log = task / 'PlanePet' / f'tunnel-verify-{index}.log'
            wait_for(lambda: log.exists() and ' connected error=0' in log.read_text(),
                     'Public WinHTTP tunnel did not connect')
        print('PUBLIC_WINHTTP_TLS_TWO_TUNNELS_OK', flush=True)
        result = subprocess.run([str(ROOT / 'dist' / 'PlanePetIntegrationTest.exe'),
            str(ports[0]), str(code), str(ports[1]), 'public', str(base + 10000)],
            cwd=task, env=env, capture_output=True, text=True, timeout=40,
            creationflags=subprocess.CREATE_NO_WINDOW)
        (task / 'integration.log').write_text(result.stdout + result.stderr)
        print(result.stdout, flush=True)
        assert result.returncode == 0, 'Public protocol integration failed'
        clients.extend([desktop(1, True), desktop(0, True)])
        wait_for(lambda: events('alice', 'game_started') == 1 and events('bob', 'game_started') == 1,
                 'Actual desktop clients did not enter public game')
        print('PUBLIC_DESKTOP_PAIR_INVITE_HIDDEN_PEER_COUNTDOWN_PLAYING_OK', flush=True)
        stop(clients[1])
        clients.append(desktop(0, False, False))
        wait_for(lambda: events('bob', 'game_finished') == 1 and events('alice', 'game_finished') == 1,
                 'Restarted desktop round did not settle on both sides')
        assert events('alice', 'game_started') == 1, 'Restart revived an old round'
        assert events('alice', 'game_end_reason') == 1 and events('bob', 'game_end_reason') == 1
        for name in ('alice', 'bob'):
            history = (task / (name + '.history')).read_text().splitlines()[0].split()
            assert history[:2] == ['PLANE_PET_HISTORY', '2'] and int(history[2]) == 1
        print('PUBLIC_DESKTOP_AUTO_RESUME_DISCONNECT_REASON_HISTORY_ONCE_OK', flush=True)
    finally:
        for process in clients:
            stop(process)
        try:
            cleanup_test_binding()
        finally:
            for process in reversed(processes):
                stop(process)
    print('PUBLIC_WINDOWS_VERIFICATION_OK formal_profile_untouched=1 telemetry_upload=0', flush=True)


if __name__ == '__main__':
    main()
