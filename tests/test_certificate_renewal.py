"""Pure tests; never invoke Certbot, network, systemd or live certificate files."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

if sys.platform == 'win32':
    sys.modules.setdefault('fcntl', types.ModuleType('fcntl'))
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cert_renew', root / 'deploy/renew-plane-pet-certificate.py')
renew = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renew)


class RenewalTest(unittest.TestCase):
    def test_only_one_explicit_certificate_and_no_global_hooks(self):
        args = renew.command()
        self.assertEqual(args[args.index('--cert-name') + 1], '8.166.124.212')
        self.assertIn('--no-directory-hooks', args)
        self.assertIn('--webroot', args)
        self.assertNotIn('--nginx', args)
        for hook in ('--pre-hook', '--post-hook', '--deploy-hook'):
            self.assertEqual(args[args.index(hook) + 1], '/bin/true')

    def test_hourly_task_does_not_force_issuance(self):
        self.assertNotIn('--force-renewal', renew.command())
        self.assertNotIn('--dry-run', renew.command())
        self.assertIn('--dry-run', renew.command(dry_run=True))
        self.assertIn('--force-renewal', renew.command(force=True))
        timer = (root / 'deploy/plane-pet-cert-renew.timer').read_text()
        self.assertIn('OnCalendar=*-*-* *:17:00', timer)
        self.assertIn('Persistent=true', timer)
        service = (root / 'deploy/plane-pet-cert-renew.service').read_text()
        self.assertNotIn('--force', service)
        self.assertIn('renew-plane-pet-certificate.py', service)

    def test_default_served_check_validates_tls(self):
        with patch.object(renew.socket, 'create_connection') as connect, \
                patch.object(renew.ssl, 'create_default_context') as context:
            tls = context.return_value.wrap_socket.return_value.__enter__.return_value
            tls.getpeercert.return_value = b'certificate'
            self.assertEqual(renew.served(), renew.hashlib.sha256(b'certificate').hexdigest())
            self.assertEqual(context.return_value.wrap_socket.call_args.kwargs['server_hostname'], renew.IP)
            self.assertEqual(connect.call_args.args[0], ('127.0.0.1', 32112))


if __name__ == '__main__':
    unittest.main()
