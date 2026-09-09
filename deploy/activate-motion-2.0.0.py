"""One-shot PlanePet server-code activation. Existing gateway dependency
restarts with the server; gateway code/config and other projects stay unchanged.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import tempfile
import time
import urllib.request

STAGE = Path('/opt/plane-pet/staging/motion-2.0.0-final-20260907')
TARGET = Path('/opt/plane-pet/bin/plane-pet-server')
CANDIDATE = STAGE/'plane-pet-server'
BASELINE = '2b74161c265eb6bba05f01f1443368a76b8fb4b31e9f461485e5a04c272b7b18'
APPROVED = '726d6828dd166b84e96e03e10ca4255a9c977c7ed2a6ece98be28a761adc2a2e'


def run(*args): return subprocess.check_output(args, text=True).strip()
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def users(): return run('ss', '-Htn', 'state', 'established', '( sport = :32111 or sport = :32112 )')


def protected():
    files = [Path('/etc/systemd/system')/name for name in (
        'plane-pet.service', 'plane-pet-gateway.service', 'fingerknight-lobby.service',
        'fingerknight-server.service', 'plane-link.service')]
    files.append(Path('/opt/plane-pet/gateway/plane_pet_gateway.py'))
    for folder in ('/etc/nginx', '/etc/plane-pet', '/etc/fingerknight-server', '/opt/fingerknight-server/current'):
        root = Path(folder)
        if root.exists(): files.extend(p for p in root.rglob('*') if p.is_file())
    return {'files': {str(p): digest(p) for p in files if p.is_file()},
        'services': run('systemctl', 'show', 'fingerknight-lobby.service', 'fingerknight-server.service',
            'plane-link.service', 'nginx.service', '-p', 'Id', '-p', 'ActiveState', '-p', 'MainPID')}


def install(source):
    assert TARGET.resolve() == TARGET and TARGET.is_file()
    fd, name = tempfile.mkstemp(prefix='plane-pet-server.motion-', dir=TARGET.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        shutil.copyfile(source, temporary); temporary.chmod(0o755)
        with temporary.open('rb') as f: os.fsync(f.fileno())
        temporary.replace(TARGET)
    finally: temporary.unlink(missing_ok=True)


def main():
    os.umask(0o077)
    assert os.getuid() == 0 and STAGE.resolve() == STAGE
    assert not (STAGE/'ACTIVATION_OK').exists(), 'One-shot activation already done'
    assert digest(TARGET) == BASELINE and digest(CANDIDATE) == APPROVED, 'Baseline/candidate changed'
    assert not users(), 'Active PlanePet users: do not interrupt'
    before = protected()
    data_files = [Path('/var/lib/plane-pet')/name for name in ('server_bindings.db', 'gateway_tokens.db')]
    data_hashes = {str(p): digest(p) for p in data_files}
    # Independent executable + UDP loopback port + copied bindings. The live
    # endpoint, identities, service and config remain untouched by this probe.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(('127.0.0.1', 0)); port = reservation.getsockname()[1]
    smoke = STAGE/'smoke-bindings.db'; shutil.copyfile(data_files[0], smoke)
    p = subprocess.Popen([str(CANDIDATE), f'--port={port}', f'--store={smoke}'], stdout=subprocess.DEVNULL)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.settimeout(.25)
            for attempt in range(20):
                nonce = os.urandom(16); probe.sendto(b'PPPROBE1'+nonce, ('127.0.0.1', port))
                try:
                    if probe.recv(64) == b'PPREADY1'+nonce: break
                except socket.timeout: pass
            else: raise RuntimeError('Isolated candidate not ready')
    finally:
        p.terminate(); p.wait(timeout=5)
    assert digest(smoke) == data_hashes[str(data_files[0])], 'Binding load altered copied data'
    assert protected() == before and not users(), 'Concurrent service/config change or active users'
    backup = Path(tempfile.mkdtemp(prefix='motion-2.0.0-final-', dir='/opt/plane-pet/backups'))
    (backup/'protected-before.json').write_text(json.dumps(before, indent=2))
    shutil.copy2(TARGET, backup/TARGET.name)
    for file in data_files: shutil.copy2(file, backup/file.name)
    with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as live:
        with sqlite3.connect(backup/'telemetry.db') as copied: live.backup(copied)
    changed = False
    try:
        assert not users()
        install(CANDIDATE); changed = True
        run('systemctl', 'restart', 'plane-pet.service', 'plane-pet-gateway.service')
        for attempt in range(30):
            try:
                if urllib.request.urlopen('http://127.0.0.1:32111/readyz', timeout=2).read() == b'ready\n': break
            except OSError: pass
            time.sleep(.25)
        else: raise RuntimeError('Production readiness failed')
        assert protected() == before, 'Other service/config changed; stop deployment'
        assert all(digest(p) == data_hashes[str(p)] for p in data_files), 'Existing data changed'
        with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as live:
            assert live.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        result = {'version': '2.0.0', 'server_sha256': digest(TARGET), 'backup': str(backup),
            'only_server_code_changed': True, 'plane_pet_services_restarted': ['plane-pet', 'plane-pet-gateway'],
            'protected_unchanged': True, 'binding_credentials_unchanged': True}
        (STAGE/'ACTIVATION_OK').write_text(json.dumps(result, indent=2))
        changed = False
        print('MOTION_SERVER_ACTIVATED ' + json.dumps(result))
    finally:
        if changed:
            install(backup/TARGET.name)
            run('systemctl', 'restart', 'plane-pet.service', 'plane-pet-gateway.service')
            print('PLANE_PET_SERVER_ROLLED_BACK; data/config/other services not restored or altered')


if __name__ == '__main__': main()
