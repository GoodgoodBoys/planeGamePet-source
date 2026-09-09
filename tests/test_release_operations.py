"""Backup/restore verification uses synthetic local stores only."""
import importlib.util
from pathlib import Path
import sqlite3
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('backup_plane_pet',
    Path(__file__).parents[1] / 'deploy/backup-plane-pet.py')
backup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backup)


class BackupTest(unittest.TestCase):
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
