#!/usr/bin/env python3
"""Non-destructive, allowlisted Plane Pet data backup; no service mutations."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import uuid

FILES = ('server_bindings.db', 'gateway_tokens.db', 'telemetry.db')
OPTIONAL_FILES = ('online-alerts.db',)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def verify(directory):
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    if not set(FILES) <= set(manifest) or not set(manifest) <= set(FILES + OPTIONAL_FILES):
        raise ValueError('Unexpected backup inventory')
    for name in manifest:
        path = directory / name
        if path.is_symlink() or not path.is_file() or digest(path) != manifest[name]:
            raise ValueError('Backup verification failed: ' + name)
    for name in ('telemetry.db', 'online-alerts.db'):
        if name not in manifest:
            continue
        db = sqlite3.connect((directory / name).as_uri() + '?mode=ro', uri=True)
        try:
            if db.execute('PRAGMA quick_check').fetchone() != ('ok',):
                raise ValueError('SQLite integrity check failed')
        finally:
            db.close()


def backup(source, destination):
    source, destination = source.resolve(), destination.resolve()
    selected = FILES + tuple(name for name in OPTIONAL_FILES if (source / name).exists())
    for name in selected:
        path = source / name
        if path.is_symlink() or not path.is_file() or path.resolve().parent != source:
            raise ValueError('Missing or unsafe source: ' + name)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    target = destination / ('plane-pet-' + stamp + '-' + uuid.uuid4().hex[:8])
    target.mkdir(parents=True, mode=0o700, exist_ok=False)
    # Bindings precede credentials; writers use atomic replacement. For a
    # globally quiescent checkpoint, an operator must stop only Plane Pet first.
    for name in FILES[:2]:
        shutil.copyfile(source / name, target / name)
        (target / name).chmod(0o600)
    for name in selected[2:]:
        original = sqlite3.connect((source / name).as_uri() + '?mode=ro', uri=True)
        copied = sqlite3.connect(target / name)
        try:
            original.backup(copied)
        finally:
            copied.close()
            original.close()
        (target / name).chmod(0o600)
    manifest = {name: digest(target / name) for name in selected}
    (target / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (target / 'manifest.json').chmod(0o600)
    verify(target)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('/var/lib/plane-pet'))
    parser.add_argument('--destination', type=Path, default=Path('/var/backups/plane-pet'))
    parser.add_argument('--verify', type=Path)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify.resolve())
        print('PLANE_PET_BACKUP_VERIFIED')
    else:
        print('PLANE_PET_BACKUP_CREATED', backup(args.source, args.destination))
