"""Read-only live post-deployment checks; no mail/login/test identities created."""
import importlib.util
import json
from pathlib import Path
import smtplib
import sqlite3
import ssl
import sys
import urllib.request

stage = Path(sys.argv[1]).resolve()
backup = Path(sys.argv[2]).resolve()
assert stage.parent == Path('/opt/plane-pet/staging')
assert backup.parent == Path('/opt/plane-pet/backups')
spec = importlib.util.spec_from_file_location('alert_live_audit', stage / 'deploy/activate-online-alerts.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
manifest = json.loads((stage / 'manifest.json').read_text())
for relative in ('gateway/plane_pet_gateway.py', 'gateway/online_alerts.py'):
    assert audit.digest(Path('/opt/plane-pet') / relative) == manifest[relative]
before = json.loads((backup / 'protected-before.json').read_text())
after = audit.protected_snapshot()
assert before == after, 'Protected files/services changed'
current = audit.status()
assert current['connection_limit'] == 0 and current['online_alerts_recording']
assert not current['online_alerts_persistence_error'] and current['backend_ready']
for endpoint, expected in (('/healthz', b'ok\n'), ('/readyz', b'ready\n')):
    with urllib.request.urlopen('http://127.0.0.1:32111' + endpoint, timeout=5) as response:
        assert response.read() == expected
with sqlite3.connect('file:/var/lib/plane-pet/online-alerts.db?mode=ro', uri=True) as db:
    assert db.execute('PRAGMA quick_check').fetchone()[0] == 'ok'
    rows = db.execute('SELECT threshold,status FROM milestones ORDER BY threshold').fetchall()
smtp_tls = False
try:
    with smtplib.SMTP_SSL('smtp.163.com', 465, timeout=8, context=ssl.create_default_context()) as connection:
        smtp_tls = connection.ehlo()[0] == 250
except Exception as error:
    smtp_detail = type(error).__name__
else:
    smtp_detail = 'TLS certificate and EHLO verified; no AUTH or mail submitted'
print(json.dumps(dict(gateway=current, milestones=rows,
                     sender_service=audit.service('plane-pet-online-alerts.service'),
                     protected_unchanged=True, smtp_tls_reachable=smtp_tls,
                     smtp_detail=smtp_detail), indent=2))
