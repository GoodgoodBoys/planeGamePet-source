"""Validate old/new gateway readers on an isolated copy, never live state."""
import importlib.util
import os
from pathlib import Path
import sqlite3
import sys
import time


def main():
    directory = Path(sys.argv[1]).resolve(strict=True)
    if not str(directory).startswith('/opt/plane-pet/staging/'):
        raise RuntimeError('Compatibility check must use a staging directory')
    database = directory / 'telemetry.db'
    os.environ['PLANE_PET_TOKEN_STORE'] = str(directory / 'gateway_tokens.db')
    os.environ['PLANE_PET_TELEMETRY_STORE'] = str(database)
    os.environ['PLANE_PET_ADMIN_PASSWORD_FILE'] = '/etc/plane-pet/gateway-admin.password'
    with sqlite3.connect(f'file:{database}?mode=ro', uri=True) as db:
        before = db.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
        schema = db.execute('PRAGMA table_info(telemetry_events)').fetchall()
    original_tokens = (directory / 'gateway_tokens.db').read_bytes()
    for index, source in enumerate(sys.argv[2:]):
        spec = importlib.util.spec_from_file_location(f'compat_gateway_{index}', source)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.telemetry_db = module.open_telemetry_store(database)
        snapshot = module.analytics_snapshot()
        assert isinstance(snapshot, dict)
        assert len(module.render_admin_page()) > 1000
        assert module.telemetry_db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        assert module.telemetry_db.execute('PRAGMA table_info(telemetry_events)').fetchall() == schema
        remaining = module.telemetry_db.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
        remaining_by_id = {row[0]: row for row in remaining}
        cutoff = int(time.time() * 1000) - 90 * 86400000
        for row in before:
            if row[4] >= cutoff:
                assert remaining_by_id.get(row[0]) == row, 'Retained history changed'
        module.telemetry_db.close()
        print(f'COPY_COMPAT_OK reader={index} before={len(before)} after={len(remaining)}')
    assert (directory / 'gateway_tokens.db').read_bytes() == original_tokens
    print('COPY_ROLLBACK_COMPAT_OK schema_unchanged=1 retained_history_unchanged=1 tokens_unchanged=1')


if __name__ == '__main__':
    main()
