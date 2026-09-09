#!/usr/bin/env python3
"""Scoped activation; default is read-only audit. Never modify shared services."""
import argparse
import base64
import datetime as dt
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

GATEWAY = Path('/opt/plane-pet/gateway/plane_pet_gateway.py')
MODULE = Path('/opt/plane-pet/gateway/online_alerts.py')
DROPIN = Path('/etc/systemd/system/plane-pet-gateway.service.d/90-online-alerts.conf')
MAIL_UNIT = Path('/etc/systemd/system/plane-pet-online-alerts.service')
BASE_HASH = 'ee26382dde44490dc63209744e32b4a7802e2e4546613a5a40f72dbf5b98ee1a'
PROTECTED_SERVICES = ('plane-pet.service', 'fingerknight-lobby.service',
                      'fingerknight-server.service', 'plane-link.service', 'nginx.service')


def run(*args):
    return subprocess.check_output(args, text=True, timeout=20).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def service(name):
    output = run('systemctl', 'show', name, '-p', 'ActiveState', '-p', 'MainPID')
    return dict(line.split('=', 1) for line in output.splitlines())


def status():
    password = Path('/etc/plane-pet/gateway-admin.password').read_text().strip()
    headers = {'Authorization': 'Basic ' + base64.b64encode(('plane-pet:' + password).encode()).decode()}
    request = urllib.request.Request('http://127.0.0.1:32111/admin/status', headers=headers)
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


def protected_snapshot():
    files = {}
    roots = ('/etc/nginx', '/etc/fingerknight-server', '/opt/fingerknight-server/current', '/etc/plane-pet')
    for root in roots:
        path = Path(root)
        if path.exists():
            for item in path.rglob('*'):
                if item.is_file():
                    files[str(item)] = digest(item)
    for item in (Path('/opt/plane-pet/bin/plane-pet-server'),
                 Path('/etc/systemd/system/plane-pet-gateway.service')):
        if item.is_file():
            files[str(item)] = digest(item)
    for name in PROTECTED_SERVICES:
        for suffix in (name, name + '.d'):
            item = Path('/etc/systemd/system') / suffix
            for file in ([item] if item.is_file() else item.rglob('*') if item.is_dir() else []):
                if file.is_file():
                    files[str(file)] = digest(file)
    return dict(files=files, services={name: service(name) for name in PROTECTED_SERVICES})


def compact_protection(snapshot):
    return dict(file_count=len(snapshot['files']),
                combined_sha256=hashlib.sha256(json.dumps(snapshot['files'], sort_keys=True).encode()).hexdigest(),
                services=snapshot['services'])


def data_snapshot():
    root = Path('/var/lib/plane-pet')
    hashes = {name: digest(root / name) for name in ('gateway_tokens.db', 'server_bindings.db')}
    with sqlite3.connect((root / 'telemetry.db').as_uri() + '?mode=ro', uri=True) as db:
        assert db.execute('PRAGMA quick_check').fetchone()[0] == 'ok'
        rows = db.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
    return hashes, rows


def assert_preserved(before):
    previous_hashes, previous_rows = before
    hashes, rows = data_snapshot()
    assert hashes == previous_hashes, 'PlanePet credential/binding files changed'
    by_id = {row[0]: row for row in rows}
    cutoff = int(time.time() * 1000) - 90 * 86400000
    assert all(by_id.get(row[0]) == row for row in previous_rows if row[4] >= cutoff), 'Retained statistics changed'


def atomic_install(source, target, mode=0o644):
    assert not target.is_symlink(), 'Refuse symlink target'
    target.parent.mkdir(parents=True, exist_ok=True)
    handle, name = tempfile.mkstemp(prefix='.online-alert-install-', dir=target.parent)
    with os.fdopen(handle, 'wb') as stream:
        stream.write(source.read_bytes())
        stream.flush()
        os.fsync(stream.fileno())
    os.chmod(name, mode)
    os.replace(name, target)


def activate(stage, expected_pid):
    assert stage.is_dir() and stage.resolve().parent == Path('/opt/plane-pet/staging')
    assert not any(path.exists() for path in (MODULE, DROPIN, MAIL_UNIT)), 'Already deployed or conflicting files'
    assert digest(GATEWAY) == BASE_HASH, 'Gateway baseline changed; review before deploying'
    assert service('plane-pet-gateway.service')['MainPID'] == expected_pid, 'Gateway restarted since audit'
    live = status()
    assert live['connections'] == 0 and live['backend_ready'], 'Live clients present or backend not ready'
    assert live['telemetry_queue'] == 0 and live['telemetry_write_failures'] == 0
    protected = protected_snapshot()
    original_data = data_snapshot()
    manifest = json.loads((stage / 'manifest.json').read_text())
    required = {'gateway/plane_pet_gateway.py', 'gateway/online_alerts.py',
                'deploy/plane-pet-online-alerts.service', 'deploy/backup-plane-pet.py'}
    assert required <= set(manifest), 'Staging inventory incomplete'
    for relative, checksum in manifest.items():
        item = stage / relative
        assert item.resolve().is_relative_to(stage.resolve()) and item.is_file()
        assert digest(item) == checksum, 'Staging checksum mismatch'
    for relative in ('gateway/plane_pet_gateway.py', 'gateway/online_alerts.py'):
        compile((stage / relative).read_bytes(), relative, 'exec')
    subprocess.run(['systemd-analyze', 'verify', str(stage / 'deploy/plane-pet-online-alerts.service')],
                   check=True, timeout=20, stdout=subprocess.DEVNULL)
    spec = importlib.util.spec_from_file_location('scoped_backup', stage / 'deploy/backup-plane-pet.py')
    backup = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(backup)
    target = backup.backup(Path('/var/lib/plane-pet'), Path('/opt/plane-pet/backups'))
    shutil.copy2(GATEWAY, target / 'original-gateway.py')
    (target / 'protected-before.json').write_text(json.dumps(protected, sort_keys=True))
    assert status()['connections'] == 0, 'Client connected during preflight; activation postponed'
    assert run('ss', '-Htn', 'state', 'established', '( sport = :32111 or sport = :32112 )') == '', 'Live sockets present'
    settings = stage / 'online-alerts-dropin.conf'
    settings.write_text('[Service]\nEnvironment=PLANE_PET_MAX_CONNECTIONS=0\n'
                        'Environment=PLANE_PET_ONLINE_ALERTS_ENABLED=1\n')
    installed = []
    try:
        for source, destination in ((stage / 'gateway/online_alerts.py', MODULE),
                                    (stage / 'gateway/plane_pet_gateway.py', GATEWAY),
                                    (settings, DROPIN),
                                    (stage / 'deploy/plane-pet-online-alerts.service', MAIL_UNIT)):
            atomic_install(source, destination)
            installed.append(destination)
        run('systemctl', 'daemon-reload')
        run('systemctl', 'restart', 'plane-pet-gateway.service')
        for _ in range(30):
            try:
                current = status()
                if current['backend_ready'] and current['online_alerts_recording']:
                    break
            except Exception:
                pass
            time.sleep(.2)
        else:
            raise RuntimeError('Gateway readiness timed out')
        assert current['connection_limit'] == 0
        time.sleep(.3)
        assert status()['online_alerts_persistence_error'] == ''
        with sqlite3.connect('file:/var/lib/plane-pet/online-alerts.db?mode=ro', uri=True) as db:
            assert db.execute('PRAGMA quick_check').fetchone()[0] == 'ok'
        assert_preserved(original_data)
        after = protected_snapshot()
        assert after == protected, 'Protected project changed during activation; do not restore it automatically'
    except Exception:
        # Only restore the gateway we replaced. Keep outbox/data/backups, never restore other projects.
        atomic_install(target / 'original-gateway.py', GATEWAY)
        for item in installed:
            if item in (MODULE, DROPIN, MAIL_UNIT):
                item.unlink()
        run('systemctl', 'daemon-reload')
        run('systemctl', 'restart', 'plane-pet-gateway.service')
        raise
    result = dict(activated=True, gateway=status(), backup=str(target),
                  mail_sender_started=False, protected=compact_protection(after))
    (target / 'activation-result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--expected-pid')
    args = parser.parse_args()
    os.umask(0o077)
    if args.apply:
        assert args.stage and args.expected_pid
        activate(args.stage, args.expected_pid)
    else:
        protection = protected_snapshot()
        print(json.dumps(dict(gateway=status(), gateway_service=service('plane-pet-gateway.service'),
                              gateway_sha256=digest(GATEWAY),
                              credential_file_present=Path('/etc/plane-pet/plane-pet-smtp.password').exists(),
                              systemd=run('systemctl', '--version').splitlines()[0],
                              protected=compact_protection(protection)), indent=2))


if __name__ == '__main__':
    main()
