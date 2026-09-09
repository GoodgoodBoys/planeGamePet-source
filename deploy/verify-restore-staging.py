"""Start restored PlanePet copies on loopback; never operate live services."""
import argparse
import base64
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def port(kind):
    with socket.socket(socket.AF_INET, kind) as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backup', required=True, type=Path)
    parser.add_argument('--server', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = args.backup.resolve(strict=True)
    server_binary = args.server.resolve(strict=True)
    assert os.name == 'posix' and str(root).startswith('/opt/plane-pet/staging/')
    assert str(source).startswith('/opt/plane-pet/backups/')
    assert str(server_binary).startswith('/opt/plane-pet/staging/')
    os.umask(0o077)
    backup = load('backup_restore_verifier', root / 'deploy/backup-plane-pet.py')
    protocol = load('restore_protocol', root / 'tests/server_edge_test.py')
    backup.verify(source)
    result = {}
    with tempfile.TemporaryDirectory(prefix='restore-check-', dir=root) as folder:
        restored = Path(folder)
        for name in json.loads((source / 'manifest.json').read_text()):
            shutil.copyfile(source / name, restored / name)
        udp_port, http_port = port(socket.SOCK_DGRAM), port(socket.SOCK_STREAM)
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
            PLANE_PET_GATEWAY_HOST='127.0.0.1', PLANE_PET_GATEWAY_PORT=str(http_port),
            PLANE_PET_UDP_HOST='127.0.0.1', PLANE_PET_UDP_PORT=str(udp_port),
            PLANE_PET_TOKEN_STORE=str(restored / 'gateway_tokens.db'),
            PLANE_PET_TELEMETRY_STORE=str(restored / 'telemetry.db'),
            PLANE_PET_ONLINE_ALERTS_ENABLED='0',
            PLANE_PET_ADMIN_PASSWORD=secrets.token_hex(24))
        processes = []
        try:
            processes.append(subprocess.Popen([str(server_binary), '--bind=127.0.0.1',
                f'--port={udp_port}', f'--store={restored / "server_bindings.db"}'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            processes.append(subprocess.Popen([sys.executable, str(root / 'gateway/plane_pet_gateway.py')],
                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            base = f'http://127.0.0.1:{http_port}'
            for attempt in range(40):
                try:
                    assert urllib.request.urlopen(base + '/readyz', timeout=1).read() == b'ready\n'
                    break
                except (OSError, AssertionError):
                    if attempt == 39: raise
                    time.sleep(.1)
            headers = {'Authorization': 'Basic ' + base64.b64encode(
                ('plane-pet:' + env['PLANE_PET_ADMIN_PASSWORD']).encode()).decode()}
            page = urllib.request.urlopen(urllib.request.Request(base + '/admin', headers=headers), timeout=5).read()
            assert b'<html' in page and len(page) > 1000
            state = json.load(urllib.request.urlopen(urllib.request.Request(base + '/admin/status', headers=headers), timeout=5))
            lines = (restored / 'server_bindings.db').read_text().splitlines()
            assert lines[0] == 'PLANE_PET_BINDINGS 1'
            records = [list(map(int, line.split())) for line in lines[1:] if line.strip()]
            confirmed = [row for row in records if len(row) == 9 and row[7:] == [0, 0]]
            assert confirmed, 'No confirmed pair available to exercise recovery'
            resumed = 0
            for row in confirmed:
                for slot in range(2):
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                        client.connect(('127.0.0.1', udp_port))
                        client.settimeout(.1)
                        client.send(protocol.control(3, row[1 + slot], 50000 + resumed,
                            binding=row[0], low=row[3 + slot * 2], high=row[4 + slot * 2]))
                        protocol.recv_until(client, lambda p: p[:4] == b'PL\x05\x02', seconds=2)
                        resumed += 1
            assert state['backend_ready'] and state['telemetry_write_failures'] == 0
            result = dict(backend_ready=True, admin_readable=True, confirmed_pairs=len(confirmed),
                          restored_endpoints=resumed, credentials=state['enrollments'],
                          production_untouched=True, backup_unchanged=True)
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
        backup.verify(source)
    print('ISOLATED_BACKUP_RESTORE_BOOT_OK ' + json.dumps(result))


if __name__ == '__main__':
    main()
