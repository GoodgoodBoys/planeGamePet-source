"""Renew only PlanePet's IP certificate; verify the certificate actually served.

No nginx configuration edits, other certificate renewals or service restarts.
The shared nginx master is gracefully reloaded only when the served leaf differs.
"""
import argparse
import datetime as dt
import fcntl
import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import time

IP = '8.166.124.212'
LINEAGE = Path('/etc/letsencrypt/live') / IP
CERTBOT = '/opt/certbot-ip-v5/bin/certbot'
STATUS = Path('/var/lib/plane-pet-ops/certificate-status.json')
PROTECTED_SERVICES = ('fingerknight-lobby.service', 'fingerknight-server.service',
                      'plane-link.service', 'plane-pet.service', 'plane-pet-gateway.service', 'nginx.service')


def run(*args, timeout=120):
    result = subprocess.run(args, text=True, capture_output=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}): {result.stderr[-2500:]}')
    return result.stdout.strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def protected():
    files = {}
    for root in (Path('/etc/nginx'), Path('/etc/fingerknight-server'),
                 Path('/opt/fingerknight-server/current')):
        if root.exists():
            for path in sorted(root.rglob('*')):
                if path.is_file():
                    files[str(path)] = digest(path)
    for path in Path('/etc/letsencrypt/renewal').glob('*.conf'):
        if path.name != f'{IP}.conf':
            files[str(path)] = digest(path)
    for name in PROTECTED_SERVICES:
        path = Path('/etc/systemd/system') / name
        if path.is_file():
            files[str(path)] = digest(path)
    services = run('systemctl', 'show', *PROTECTED_SERVICES,
                   '-p', 'Id', '-p', 'ActiveState', '-p', 'MainPID')
    return dict(files=files, services=services)


def certificate(path=LINEAGE / 'cert.pem'):
    der = ssl.PEM_cert_to_DER_cert(path.read_text())
    expiry = run('openssl', 'x509', '-in', str(path), '-noout', '-enddate').split('=', 1)[1]
    stamp = dt.datetime.strptime(expiry, '%b %d %H:%M:%S %Y %Z').replace(tzinfo=dt.timezone.utc)
    return dict(sha256=hashlib.sha256(der).hexdigest(), expires_at=stamp.isoformat(),
                remaining_seconds=int((stamp - dt.datetime.now(dt.timezone.utc)).total_seconds()))


def served(verify=True):
    # Validate both trust and the public IP SAN, while avoiding an external loop.
    with socket.create_connection(('127.0.0.1', 32112), timeout=10) as connection:
        context = ssl.create_default_context() if verify else ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        if not verify:
            # Read only the local old leaf to decide whether deployment is
            # needed; even an expired old leaf must be recoverable. The final
            # served check below always validates trust and the IP SAN.
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
        with context.wrap_socket(connection, server_hostname=IP) as tls:
            return hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest()


def command(dry_run=False, force=False):
    args = [CERTBOT, 'renew', '--cert-name', IP, '--config',
            '/opt/plane-pet/ops/certbot-plane-pet.ini', '--non-interactive',
            '--no-directory-hooks', '--no-random-sleep-on-renew', '--webroot',
            '--webroot-path', '/var/www/html', '--pre-hook', '/bin/true',
            '--post-hook', '/bin/true', '--deploy-hook', '/bin/true']
    # Do not rely on Certbot hook exit codes: deployment and verification below
    # are part of this process and a failure makes the systemd unit fail.
    if dry_run:
        args.append('--dry-run')
    if force:
        args.append('--force-renewal')
    return args


def write_status(value):
    STATUS.parent.mkdir(mode=0o700, exist_ok=True)
    temporary = STATUS.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.chmod(0o600)
    temporary.replace(STATUS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--force-once', action='store_true', help='Explicit one-time issuance, never used by timer')
    parser.add_argument('--audit', action='store_true', help='Read-only inventory and certificate check')
    args = parser.parse_args()
    if args.dry_run and args.force_once:
        parser.error('dry-run and force-once are mutually exclusive')
    if args.audit:
        print(json.dumps(dict(protected=protected(), certificate=certificate(), served_sha256=served())))
        return
    if os.geteuid() != 0:
        raise SystemExit('Must run as root')
    os.umask(0o077)
    with open('/run/lock/plane-pet-cert-renew.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = protected()
        previous = certificate()
        result = dict(checked_at=dt.datetime.now(dt.timezone.utc).isoformat(),
                      dry_run=args.dry_run, ok=False)
        try:
            run(*command(args.dry_run, args.force_once), timeout=480)
            if protected() != before:
                raise RuntimeError('Concurrent protected config/service change; refusing nginx reload')
            current = certificate()
            if args.dry_run and current['sha256'] != previous['sha256']:
                raise RuntimeError('Dry-run changed the production certificate')
            deployed = served(verify=False)
            reloaded = False
            if deployed != current['sha256'] and not args.dry_run:
                run('/usr/sbin/nginx', '-t')
                if protected() != before:
                    raise RuntimeError('Concurrent protected change before reload')
                run('systemctl', 'reload', 'nginx.service')
                reloaded = True
                for _ in range(20):
                    deployed = served()
                    if deployed == current['sha256']:
                        break
                    time.sleep(.5)
            if deployed != current['sha256']:
                raise RuntimeError('Served TLS certificate differs from disk')
            if served() != current['sha256']:
                raise RuntimeError('TLS verification failed after deployment')
            if current['remaining_seconds'] < 48 * 3600:
                raise RuntimeError('TLS certificate has less than 48 hours remaining')
            if protected() != before:
                raise RuntimeError('Protected config/service changed during verification')
            result.update(ok=True, certificate=current, renewed=current['sha256'] != previous['sha256'],
                          nginx_gracefully_reloaded=reloaded, protected_unchanged=True)
        except Exception as exc:
            result['error'] = str(exc)
            write_status(result)
            print(json.dumps(result), flush=True)
            raise
        write_status(result)
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
