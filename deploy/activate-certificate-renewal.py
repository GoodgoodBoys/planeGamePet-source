"""Install the reviewed, PlanePet-only renewal unit; no certificate issuance."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

stage = Path(__file__).resolve().parent
assert str(stage).startswith('/opt/plane-pet/staging/cert-renew-'), 'Unexpected stage'
assert os.geteuid() == 0
os.umask(0o077)
spec = importlib.util.spec_from_file_location('renewal', stage / 'renew-plane-pet-certificate.py')
renewal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renewal)
run = renewal.run
if subprocess.run(['systemctl', 'is-active', '--quiet', 'plane-pet-cert-renew.service']).returncode == 0:
    raise SystemExit('Renewal currently running; retry after it finishes')
before = renewal.protected()
targets = {
    'renew-plane-pet-certificate.py': Path('/opt/plane-pet/ops/renew-plane-pet-certificate.py'),
    'certbot-plane-pet.ini': Path('/opt/plane-pet/ops/certbot-plane-pet.ini'),
    'plane-pet-cert-renew.service': Path('/etc/systemd/system/plane-pet-cert-renew.service'),
    'plane-pet-cert-renew.timer': Path('/etc/systemd/system/plane-pet-cert-renew.timer'),
}
for name, target in targets.items():
    assert (stage / name).is_file() and not target.is_symlink()
run('systemd-analyze', 'verify', str(stage / 'plane-pet-cert-renew.service'),
    str(stage / 'plane-pet-cert-renew.timer'))
backup_root = Path('/opt/plane-pet/backups')
backup_root.mkdir(mode=0o700, exist_ok=True)
backup = Path(tempfile.mkdtemp(prefix='cert-renew-20260909-', dir=backup_root))
for name, target in targets.items():
    if target.exists():
        shutil.copy2(target, backup / name)
(backup / 'protected-before.json').write_text(json.dumps(before, indent=2))
for name, target in targets.items():
    target.parent.mkdir(mode=0o755, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=target.name + '.new-', dir=target.parent)
    os.close(fd)
    shutil.copyfile(stage / name, temporary)
    os.chmod(temporary, 0o644)
    os.replace(temporary, target)
run('systemctl', 'daemon-reload')
run('systemctl', 'enable', 'plane-pet-cert-renew.timer')
run('systemctl', 'restart', 'plane-pet-cert-renew.timer')
assert renewal.protected() == before, 'Other configuration/service changed'
print(json.dumps(dict(installed=True, backup=str(backup), protected_unchanged=True,
                     timer=run('systemctl', 'show', 'plane-pet-cert-renew.timer',
                               '-p', 'ActiveState', '-p', 'NextElapseUSecRealtime'))))
