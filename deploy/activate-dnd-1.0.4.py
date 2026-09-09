#!/usr/bin/env python3
"""One code-only PlanePet activation; fixed baseline, backup, rollback and protected checks.

Run after isolated Linux tests in the exact staging folder. Never installs units,
changes network settings, deletes records, or touches another project's files.
"""
import base64
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
import urllib.error
import urllib.request

STAGE = Path('/opt/plane-pet/staging/dnd-1.0.4-20260906')
SOURCE = STAGE / 'source'
TARGETS = {
    Path('/opt/plane-pet/bin/plane-pet-server'): (
        STAGE / 'plane-pet-server', 'abd3d3fe4526996882b6a68bb53e93991cc67ac494783555f0aeefa2eace6e19'),
    Path('/opt/plane-pet/gateway/plane_pet_gateway.py'): (
        SOURCE / 'gateway/plane_pet_gateway.py', '07e81700fadb10d0014ca81229fa9ac3981a02453ad2058fb8bb3702d8b340c4')}
SERVICES = ['plane-pet.service', 'plane-pet-gateway.service']


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def protected():
    files = [Path('/etc/systemd/system') / name for name in
             SERVICES + ['fingerknight-server.service', 'plane-link.service']]
    for root in (Path('/etc/nginx'), Path('/etc/plane-pet'), Path('/etc/fingerknight-server')):
        if root.exists(): files.extend(p for p in root.rglob('*') if p.is_file())
    current = Path('/opt/fingerknight-server/current')
    # All deployed application files, including any nested assets/configs.
    files.extend(p for p in current.rglob('*') if p.is_file())
    return dict(hashes={str(p): digest(p) for p in files if p.is_file()},
                fingerknight_current=str(current.resolve()),
                services=run('systemctl', 'show', 'fingerknight-server.service', 'plane-link.service',
                             'nginx.service', '-p', 'Id', '-p', 'ActiveState', '-p', 'MainPID'))


def atomic_install(source, target):
    if target not in TARGETS or target.resolve() != target:
        raise RuntimeError('Unsafe activation target')
    handle, temporary = tempfile.mkstemp(prefix=target.name + '.dnd-', dir=target.parent)
    os.close(handle)
    temporary = Path(temporary)
    try:
        shutil.copyfile(source, temporary)
        temporary.chmod(0o755)
        with temporary.open('rb') as stream: os.fsync(stream.fileno())
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def ready():
    for attempt in range(40):
        try:
            if urllib.request.urlopen('http://127.0.0.1:32111/readyz', timeout=2).read() == b'ready\n':
                return
        except OSError:
            pass
        time.sleep(.25)
    raise RuntimeError('PlanePet readiness failed')


def main():
    os.umask(0o077)
    assert os.getuid() == 0 and STAGE.resolve() == STAGE
    assert (STAGE / 'LINUX_TESTS_OK').is_file() and not (STAGE / 'ACTIVATION_OK').exists()
    assert not run('ss', '-Htn', 'state', 'established', '( sport = :32111 )'), 'Active PlanePet users; abort'
    for target, (candidate, expected) in TARGETS.items():
        assert digest(target) == expected and candidate.is_file(), 'Baseline changed; abort'
    run('systemctl', 'is-active', *SERVICES)
    baseline = protected()
    backup = Path(tempfile.mkdtemp(prefix='dnd-1.0.4-', dir='/opt/plane-pet/backups'))
    (backup / 'protected-before.json').write_text(json.dumps(baseline, indent=2))
    for target in TARGETS: shutil.copy2(target, backup / target.name)
    (STAGE / 'activation-backup.path').write_text(str(backup))
    spec = importlib.util.spec_from_file_location('scoped_backup', SOURCE / 'deploy/backup-plane-pet.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    changed = False
    try:
        assert protected() == baseline
        assert not run('ss', '-Htn', 'state', 'established', '( sport = :32111 )')
        changed = True
        run('systemctl', 'stop', 'plane-pet-gateway.service', 'plane-pet.service')
        data_backup = module.backup(Path('/var/lib/plane-pet'), backup / 'data')
        for target, (candidate, _) in TARGETS.items(): atomic_install(candidate, target)
        run('systemctl', 'start', *SERVICES)
        ready()
        assert urllib.request.urlopen('https://8.166.124.212:32112/healthz', timeout=10).read() == b'ok\n'
        try:
            urllib.request.urlopen('https://8.166.124.212:32112/admin', timeout=10)
            raise AssertionError('Unauthenticated admin access')
        except urllib.error.HTTPError as error:
            assert error.code == 401
        password = Path('/etc/plane-pet/gateway-admin.password').read_text().strip()
        headers = {'Authorization': 'Basic ' + base64.b64encode(('plane-pet:' + password).encode()).decode()}
        page = urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:32111/admin', headers=headers), timeout=10).read().decode()
        assert '勿扰模式' in page
        status = json.load(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:32111/admin/status', headers=headers), timeout=10))
        assert status['backend_ready'] and status['telemetry_write_failures'] == 0
        for name in ('server_bindings.db', 'gateway_tokens.db'):
            assert digest(data_backup / name) == digest(Path('/var/lib/plane-pet') / name)
        with sqlite3.connect((data_backup / 'telemetry.db').as_uri() + '?mode=ro', uri=True) as old:
            old_rows = old.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
            schema = old.execute('PRAGMA table_info(telemetry_events)').fetchall()
        with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as live:
            assert live.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
            assert live.execute('PRAGMA table_info(telemetry_events)').fetchall() == schema
            after = {row[0]: row for row in live.execute('SELECT * FROM telemetry_events')}
            retained = [row for row in old_rows if row[4] >= int(time.time()*1000) - 90*86400000]
            assert all(after.get(row[0]) == row for row in retained)
        assert protected() == baseline, 'Protected project/service changed during activation'
        (backup / 'protected-after.json').write_text(json.dumps(protected(), indent=2))
        (STAGE / 'ACTIVATION_OK').write_text(json.dumps(dict(
            version='1.0.4', backup=str(backup), historical_rows=len(retained),
            protected_unchanged=True, files={str(p): digest(p) for p in TARGETS}), indent=2))
        changed = False
        print('DND_1_0_4_ACTIVATED protected_unchanged=1 data_preserved=1 tls=200 admin=401/200 backup=' + str(backup))
    finally:
        if changed:
            run('systemctl', 'stop', 'plane-pet-gateway.service', 'plane-pet.service')
            for target in TARGETS: atomic_install(backup / target.name, target)
            run('systemctl', 'start', *SERVICES)
            print('PLANE_PET_CODE_ROLLED_BACK data_and_other_projects_untouched=1')


if __name__ == '__main__': main()
