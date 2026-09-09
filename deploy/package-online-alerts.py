"""Create an allowlisted source-only staging archive; never includes credentials."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    'desktop/main.cpp',  # Read-only fixture for client/server telemetry compatibility test.
    'gateway/plane_pet_gateway.py', 'gateway/online_alerts.py',
    'deploy/activate-online-alerts.py', 'deploy/plane-pet-online-alerts.service',
    'deploy/backup-plane-pet.py', 'tests/test_gateway_regressions.py',
    'tests/test_gateway_telemetry.py', 'tests/test_online_alerts.py',
    'tests/test_release_operations.py', 'tests/gateway_capacity_probe.py',
)

if __name__ == '__main__':
    destination = ROOT / 'dist' / 'plane-pet-online-alerts-staging.zip'
    destination.parent.mkdir(exist_ok=True)
    manifest = {}
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for relative in FILES:
            content = (ROOT / relative).read_bytes()
            manifest[relative] = hashlib.sha256(content).hexdigest()
            archive.writestr(relative, content)
        archive.writestr('manifest.json', json.dumps(manifest, sort_keys=True, indent=2))
    print(json.dumps(dict(path=str(destination), sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                         files=len(FILES))))
