"""One-shot, hash-pinned PlanePet public 1.0.0 activation; additive telemetry epoch migration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tempfile
import time
import urllib.request

STAGE = Path('/opt/plane-pet/staging/release-1.0.0-20260909')
SERVER = Path('/opt/plane-pet/bin/plane-pet-server')
GATEWAY = Path('/opt/plane-pet/gateway/plane_pet_gateway.py')
BASELINES = {SERVER: 'b6191436b9793387f67fc9535d53c3d7c6a9c17e33bef5f9e32f208da739fbdd',
            GATEWAY: '4e498542fbcfdbc7757855c005e0fdf68c6424419b20eb3102618a4a5dba09ec'}


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def users():
    return run('ss', '-Htn', 'state', 'established', '( sport = :32111 or sport = :32112 )')


def protected():
    files = [Path('/etc/systemd/system') / name for name in (
        'plane-pet.service', 'plane-pet-gateway.service', 'fingerknight-lobby.service',
        'fingerknight-server.service', 'plane-link.service')]
    for name in ('/etc/nginx', '/etc/plane-pet', '/etc/fingerknight-server', '/opt/fingerknight-server/current'):
        directory = Path(name)
        if directory.exists():
            files.extend(p for p in directory.rglob('*') if p.is_file())
    return {'files': {str(p): digest(p) for p in files if p.is_file()},
            'services': run('systemctl', 'show', 'fingerknight-lobby.service', 'fingerknight-server.service',
                'plane-link.service', 'nginx.service', '-p', 'Id', '-p', 'ActiveState', '-p', 'MainPID')}


def install(source, target):
    assert target in BASELINES and target.resolve() == target and target.is_file()
    metadata = target.stat()
    fd, name = tempfile.mkstemp(prefix=target.name + '.fix-', dir=target.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        shutil.copyfile(source, temporary)
        os.chown(temporary, metadata.st_uid, metadata.st_gid)
        temporary.chmod(metadata.st_mode & 0o777)
        with temporary.open('rb') as stream:
            os.fsync(stream.fileno())
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server-sha256', required=True)
    parser.add_argument('--gateway-sha256', required=True)
    args = parser.parse_args()
    os.umask(0o077)
    assert os.getuid() == 0 and STAGE.resolve() == STAGE
    assert not (STAGE / 'ACTIVATION_OK').exists(), 'Already activated'
    candidates = {SERVER: STAGE / 'plane-pet-server', GATEWAY: STAGE / 'gateway/plane_pet_gateway.py'}
    approved = {SERVER: args.server_sha256, GATEWAY: args.gateway_sha256}
    for target, source in candidates.items():
        assert digest(target) == BASELINES[target] and digest(source) == approved[target], 'Changed baseline/candidate'
    assert not users(), 'Active Plane Pet users: do not interrupt'
    before = protected()
    data_files = [Path('/var/lib/plane-pet') / name for name in ('server_bindings.db', 'gateway_tokens.db')]
    data_hashes = {p: digest(p) for p in data_files}
    spec = importlib.util.spec_from_file_location('scoped_backup', STAGE / 'deploy/backup-plane-pet.py')
    backup_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(backup_module)
    backup = backup_module.backup(Path('/var/lib/plane-pet'), Path('/opt/plane-pet/backups'))
    for target in candidates:
        shutil.copy2(target, backup / target.name)
    (backup / 'protected-before.json').write_text(json.dumps(before, indent=2))
    # Run new analytics only against a backup copy, never the live database.
    verify_db = STAGE / 'verification-telemetry.db'
    shutil.copyfile(backup / 'telemetry.db', verify_db)
    os.environ['PLANE_PET_TOKEN_STORE'] = str(STAGE / 'verification-tokens.db')
    os.environ['PLANE_PET_TELEMETRY_STORE'] = str(verify_db)
    os.environ['PLANE_PET_ADMIN_PASSWORD_FILE'] = str(STAGE / 'no-admin-password')
    spec = importlib.util.spec_from_file_location('candidate_stats', candidates[GATEWAY])
    gateway = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gateway)
    with sqlite3.connect(verify_db) as db:
        old_rows = db.execute('SELECT id,installation_id,session_id,event_id,received_at_ms,client_at_ms,app_version,invite_id,round_id,event,value FROM telemetry_events ORDER BY id').fetchall()
    gateway.telemetry_db = gateway.open_telemetry_store(verify_db)
    try:
        metrics = gateway.analytics_snapshot()
        assert metrics['games_completed'] <= metrics['games_started']
        assert gateway.telemetry_db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        assert gateway.render_admin_page().startswith(b'<!doctype html>')
        migrated = gateway.telemetry_db.execute('SELECT id,installation_id,session_id,event_id,received_at_ms,client_at_ms,app_version,invite_id,round_id,event,value FROM telemetry_events ORDER BY id').fetchall()
        assert migrated == old_rows, 'Migration changed historical events'
        assert gateway.telemetry_db.execute('SELECT COUNT(*) FROM telemetry_events WHERE release_epoch != 0').fetchone()[0] == 0
    finally:
        gateway.telemetry_db.close()
    assert protected() == before and not users(), 'Concurrent change/users; stop'
    changed = False
    try:
        changed = True
        for target, source in candidates.items():
            install(source, target)
        run('systemctl', 'restart', 'plane-pet.service', 'plane-pet-gateway.service')
        for attempt in range(30):
            try:
                if urllib.request.urlopen('http://127.0.0.1:32111/readyz', timeout=2).read() == b'ready\n':
                    break
            except OSError:
                pass
            time.sleep(.25)
        else:
            raise RuntimeError('Readiness failed')
        assert protected() == before, 'Protected project/config changed'
        assert all(digest(p) == value for p, value in data_hashes.items()), 'Binding/credential store changed'
        assert all(digest(target) == approved[target] for target in candidates)
        with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as db:
            assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
            assert 'release_epoch' in {row[1] for row in db.execute('PRAGMA table_info(telemetry_events)')}
            # Compare all pre-deployment rows; do not erase newer events.
            if old_rows:
                original = db.execute('SELECT id,installation_id,session_id,event_id,received_at_ms,client_at_ms,app_version,invite_id,round_id,event,value FROM telemetry_events WHERE id<=? ORDER BY id', (old_rows[-1][0],)).fetchall()
                assert original == old_rows, 'Historical telemetry changed'
        result = {'version': '1.0.0', 'release_epoch': 1, 'backup': str(backup), 'protected_unchanged': True,
                  'binding_credentials_unchanged': True, 'existing_statistics_copy_verified': True,
                  'sha256': {str(p): digest(p) for p in candidates}}
        (STAGE / 'ACTIVATION_OK').write_text(json.dumps(result, indent=2))
        changed = False
        print('PLANE_PET_PUBLIC_1_0_0_ACTIVATED ' + json.dumps(result))
    finally:
        if changed:
            for target in candidates:
                install(backup / target.name, target)
            run('systemctl', 'restart', 'plane-pet.service', 'plane-pet-gateway.service')
            print('PLANE_PET_CODE_ROLLED_BACK; no data/config restore')


if __name__ == '__main__':
    main()
