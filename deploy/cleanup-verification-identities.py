"""Remove only the identities of the three recorded release-verification runs."""
import binascii
import importlib.util
from pathlib import Path
import os
import shutil
import socket
import sqlite3
import stat
import struct
import subprocess
import tempfile
import time

STAGE = Path('/opt/plane-pet/staging/fix-1.0.1-20260905-1335')
spec = importlib.util.spec_from_file_location('live_audit', STAGE / 'audit-live-preservation.py')
live_audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live_audit)
BACKUP, LIVE, bindings, audit = live_audit.BACKUP, live_audit.LIVE, live_audit.bindings, live_audit.main
TEST_PAIRS = {(1403151192, 1403151193), (1347493263, 1347493264)}
TEST_DIGESTS = {
    'd7f0b2e227f59a3d2aef74970694225b4b8f42aafebc35696790dd9ae244fee3',
    '25dd1e9323b6a7e455044236bffe90f5a7f38ff508b83ee2c77c62ee8f0953ff',
    '2d0fb2e267d975edfc803ae23524449f4e849d3266f56aa70fc9201880445ab3',
    'e050c211e3ead12ec2ae07b4f7c911526da9d65896049694da93a6ebbe6905a8',
    '2a614761d9e6a1c62a5f3e4cf9ce19fb57c6d52fbb66ea418b5f09072cf83aeb',
    '6594906044bbf70d2c3aba5ed0ed6c9e178b5f08f98a950b63f558901ea8811e',
}


def unbind(row):
    packet = bytearray(48)
    struct.pack_into('<2sBB6I', packet, 0, b'PB', 3, 6, row[1], 90506001,
                     0, row[0], row[3], row[4])
    struct.pack_into('<H', packet, 38, binascii.crc_hqx(packet[:38], 0xffff))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(('127.0.0.1', 32110))
        sock.settimeout(.2)
        for _ in range(8):
            sock.send(packet)
            end = time.monotonic() + .5
            while time.monotonic() < end:
                try:
                    response = sock.recv(4096)
                    if (len(response) == 48 and response[:4] == b'PB\x03\x04'
                            and response[32] == 7
                            and struct.unpack_from('<I', response, 4)[0] == row[1]):
                        return
                except socket.timeout:
                    pass
    raise RuntimeError('Test-only unbind did not confirm')


def main():
    assert os.getuid() == 0 and STAGE.resolve() == STAGE
    audit()
    baseline = bindings(BACKUP / 'server_bindings.db')
    current = bindings(LIVE / 'server_bindings.db')
    targets = [row for key, row in current.items() if tuple(row[1:3]) in TEST_PAIRS]
    assert len(targets) == 2
    assert all(row[0] not in baseline for row in targets)
    original_tokens = (BACKUP / 'gateway_tokens.db').read_text().splitlines()
    assert not TEST_DIGESTS.intersection(line.split()[0] for line in original_tokens)
    with sqlite3.connect('file:' + str(LIVE / 'telemetry.db') + '?mode=ro', uri=True) as db:
        assert all(db.execute('SELECT COUNT(*) FROM telemetry_events WHERE installation_id=?',
                              (digest[:24],)).fetchone()[0] == 0 for digest in TEST_DIGESTS)
    recovery = Path(tempfile.mkdtemp(prefix='test-cleanup-', dir=STAGE))
    shutil.copy2(LIVE / 'server_bindings.db', recovery / 'server_bindings.db')
    for row in targets:
        unbind(row)
    # Stop only the gateway briefly so its in-memory registry cannot resurrect
    # deleted test tokens. No main game server restart and no stats DB writes.
    service = 'plane-pet-gateway.service'
    subprocess.run(['systemctl', 'is-active', '--quiet', service], check=True)
    subprocess.run(['systemctl', 'stop', service], check=True)
    try:
        registry = LIVE / 'gateway_tokens.db'
        contents = registry.read_text().splitlines()
        assert TEST_DIGESTS.issubset(line.split()[0] for line in contents)
        retained = [line for line in contents if line.split()[0] not in TEST_DIGESTS]
        assert len(contents) - len(retained) == 6
        assert all(line in retained for line in original_tokens)
        shutil.copy2(registry, recovery / 'gateway_tokens.db')
        metadata = registry.stat()
        descriptor, temporary = tempfile.mkstemp(prefix='gateway-test-cleanup-', dir=LIVE)
        with os.fdopen(descriptor, 'w', encoding='ascii') as output:
            output.write('\n'.join(retained) + '\n')
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, stat.S_IMODE(metadata.st_mode))
        os.chown(temporary, metadata.st_uid, metadata.st_gid)
        os.replace(temporary, registry)
    finally:
        subprocess.run(['systemctl', 'start', service], check=True)
    subprocess.run(['systemctl', 'is-active', '--quiet', service], check=True)
    audit()
    print(f'TEST_IDENTITY_CLEANUP_OK bindings=2 credentials=6 deleted_formal_events=0 backup={recovery}')


if __name__ == '__main__':
    main()
