"""Backup/restore verification uses synthetic local stores only."""
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('backup_plane_pet',
    Path(__file__).parents[1] / 'deploy/backup-plane-pet.py')
backup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backup)


class BackupTest(unittest.TestCase):
    def test_optional_milestone_database_is_preserved_and_verified(self):
        with tempfile.TemporaryDirectory(prefix='plane-pet-alert-backup-') as folder:
            root = Path(folder)
            source = root / 'source'
            source.mkdir()
            (source / 'server_bindings.db').write_text('synthetic bindings')
            (source / 'gateway_tokens.db').write_text('synthetic tokens')
            for name in ('telemetry.db', 'online-alerts.db'):
                db = sqlite3.connect(source / name)
                try:
                    db.execute('CREATE TABLE durable_record(value TEXT)')
                    db.execute("INSERT INTO durable_record VALUES('sent-lifetime')")
                    db.commit()
                finally:
                    db.close()
            result = backup.backup(source, root / 'backups')
            backup.verify(result)
            self.assertIn('online-alerts.db', json.loads((result / 'manifest.json').read_text()))
            db = sqlite3.connect(result / 'online-alerts.db')
            try:
                self.assertEqual(db.execute('SELECT * FROM durable_record').fetchall(), [('sent-lifetime',)])
            finally:
                db.close()
            (result / 'online-alerts.db').write_bytes(b'corrupt')
            with self.assertRaises(ValueError):
                backup.verify(result)

    def test_online_sqlite_backup_and_tamper_detection(self):
        with tempfile.TemporaryDirectory(prefix='plane-pet-backup-test-') as folder:
            root = Path(folder)
            source = root / 'source'
            source.mkdir()
            (source / 'server_bindings.db').write_text('synthetic bindings\n')
            (source / 'gateway_tokens.db').write_text('synthetic tokens\n')
            db = sqlite3.connect(source / 'telemetry.db')
            try:
                db.execute('PRAGMA journal_mode=WAL')
                db.execute('CREATE TABLE events(value TEXT)')
                db.execute("INSERT INTO events VALUES('synthetic')")
                db.commit()
                result = backup.backup(source, root / 'backups')
                backup.verify(result)
                restored = sqlite3.connect(result / 'telemetry.db')
                try:
                    self.assertEqual(restored.execute('SELECT * FROM events').fetchall(), [('synthetic',)])
                finally:
                    restored.close()
                self.assertEqual((source / 'server_bindings.db').read_text(), 'synthetic bindings\n')
                (result / 'server_bindings.db').write_text('tampered')
                with self.assertRaises(ValueError):
                    backup.verify(result)
            finally:
                db.close()

    def test_missing_input_never_creates_partial_success(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(ValueError):
                backup.backup(root, root / 'backups')
            self.assertFalse((root / 'backups').exists())


if __name__ == '__main__':
    unittest.main()
