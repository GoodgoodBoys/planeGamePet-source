"""Validate uploaded source in an isolated Linux directory, no live identities."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def main():
    stage = Path(sys.argv[1]).resolve()
    expected = sys.argv[2]
    assert stage.parent == Path('/opt/plane-pet/staging') and stage.name.startswith('online-alerts-')
    archive = stage / 'plane-pet-online-alerts-staging.zip'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected
    with zipfile.ZipFile(archive) as source:
        manifest = json.loads(source.read('manifest.json'))
        assert set(source.namelist()) == set(manifest) | {'manifest.json'}
        for name, checksum in manifest.items():
            destination = stage / name
            assert destination.resolve().is_relative_to(stage) and not destination.exists()
            assert hashlib.sha256(source.read(name)).hexdigest() == checksum
        source.extractall(stage)
    with tempfile.TemporaryDirectory(prefix='isolated-test-', dir=stage) as temporary:
        root = Path(temporary)
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                           PLANE_PET_TOKEN_STORE=str(root / 'tokens.db'),
                           PLANE_PET_TELEMETRY_STORE=str(root / 'telemetry.db'),
                           PLANE_PET_ADMIN_PASSWORD_FILE=str(root / 'no-password'),
                           PLANE_PET_ONLINE_ALERTS_ENABLED='0', PLANE_PET_MAX_CONNECTIONS='0')
        environment.pop('PLANE_PET_ADMIN_PASSWORD', None)
        for pattern in ('test_gateway*.py', 'test_online_alerts.py', 'test_release_operations.py'):
            subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', pattern],
                           cwd=stage, env=environment, check=True, timeout=45)
        subprocess.run([sys.executable, 'tests/gateway_capacity_probe.py', '--connections', '16', '--unlimited'],
                       cwd=stage, env=environment, check=True, timeout=45)
    subprocess.run(['systemd-analyze', 'verify', str(stage / 'deploy/plane-pet-online-alerts.service')],
                   check=True, timeout=20)
    print('LINUX_ISOLATED_ALERTS_VERIFIED', flush=True)


if __name__ == '__main__':
    main()
