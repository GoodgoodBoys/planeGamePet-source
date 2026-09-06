"""Read-only comparison against this release's pre-maintenance data backup."""
from pathlib import Path
import json
import sqlite3
import time

BACKUP = Path('/opt/plane-pet/backups/activation-1.0.1-20260905.L4bFjtmS')
LIVE = Path('/var/lib/plane-pet')


def bindings(path):
    lines = path.read_text().splitlines()
    assert lines[0] == 'PLANE_PET_BINDINGS 1'
    return {int(row[0]): tuple(map(int, row)) for line in lines[1:] if (row := line.split())}


def main():
    old, live = bindings(BACKUP / 'server_bindings.db'), bindings(LIVE / 'server_bindings.db')
    assert all(live.get(key) == value for key, value in old.items()), 'Formal binding changed'
    previous = {line.split()[0]: line for line in (BACKUP / 'gateway_tokens.db').read_text().splitlines()}
    current = {line.split()[0]: line for line in (LIVE / 'gateway_tokens.db').read_text().splitlines()}
    assert all(current.get(key) == value for key, value in previous.items()), 'Formal credential changed'
    with sqlite3.connect('file:' + str(BACKUP / 'telemetry.db') + '?mode=ro', uri=True) as db:
        records = db.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
    with sqlite3.connect('file:' + str(LIVE / 'telemetry.db') + '?mode=ro', uri=True) as db:
        assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        rows = {row[0]: row for row in db.execute('SELECT * FROM telemetry_events')}
    cutoff = int(time.time() * 1000) - 90 * 86400000
    kept = [row for row in records if row[4] >= cutoff]
    assert all(rows.get(row[0]) == row for row in kept), 'Retained history changed'
    print(json.dumps({'formal_bindings_preserved': len(old),
                      'formal_credentials_preserved': len(previous),
                      'historical_events_preserved': len(kept),
                      'current_events': len(rows),
                      'added_credentials': len(current) - len(previous),
                      'added_binding_device_pairs': [value[1:3] for key, value in live.items() if key not in old]}, indent=2))


if __name__ == '__main__':
    main()
