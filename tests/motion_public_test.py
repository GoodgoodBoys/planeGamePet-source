"""Opt-in public acceptance using existing isolated QA tunnel credentials."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import uuid
from public_latency_probe import fingerprints, valid_credential

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--allow-public-test', action='store_true', required=True)
    parser.add_argument('--expect-abort', action='store_true')
    parser.add_argument('--battle', action='store_true')
    parser.add_argument('--blackout', choices=('countdown', 'playing'))
    options = parser.parse_args()
    task = ROOT/'dist'/('motion-public-'+uuid.uuid4().hex)
    profile = task/'PlanePet'; profile.mkdir(parents=True)
    original = Path(os.environ['LOCALAPPDATA'])/'PlanePet'
    before = fingerprints(original)
    owned, copies, ports = [], [], []
    try:
        for side in ('a', 'b'):
            source = original/f'tunnel-dual-qa-{side}.credential'; valid_credential(source)
            instance = f'motion-{task.name[-8:]}-{side}'
            copy = profile/f'tunnel-{instance}.credential'; shutil.copyfile(source, copy); copies.append(copy)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]; ports.append(port)
            startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
            owned.append(subprocess.Popen([str(ROOT/'dist/PlanePetTunnel.exe'), f'--instance={instance}',
                '--enroll=0', f'--local-port={port}', '--url=wss://8.166.124.212:32112/v1/tunnel'],
                env=dict(os.environ, LOCALAPPDATA=str(task)), startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic()+35
        while time.monotonic() < deadline:
            logs = list(profile.glob('*.log'))
            if len(logs) == 2 and all(' connected error=0' in p.read_text() for p in logs): break
            assert all(p.poll() is None for p in owned), 'Tunnel exited early'
            time.sleep(.1)
        else: raise RuntimeError('Public tunnel connection timeout')
        subprocess.run([sys.executable, str(ROOT/'tests/motion_live_test.py'), '--ports', *map(str, ports),
            '--rtt', '0', '--loss', '0', *(['--battle'] if options.battle else []),
            *(['--expect-abort'] if options.expect_abort else []),
            *(['--blackout', options.blackout] if options.blackout else [])], check=True)
        print('MOTION_PUBLIC_TLS_REAL_CLIENTS_OK ' + str(task), flush=True)
    finally:
        for p in owned:
            if p.poll() is None: p.terminate(); p.wait(timeout=5)
        for copy in copies:
            assert copy.resolve().parent == profile.resolve()
            copy.unlink(missing_ok=True)
        result = {'original_profiles_unchanged': fingerprints(original) == before, 'qa_copies_removed': all(not p.exists() for p in copies)}
        (task/'safety.json').write_text(json.dumps(result, indent=2))
        assert all(result.values())
        print('MOTION_PUBLIC_ORIGINAL_DATA_UNCHANGED_QA_COPIES_REMOVED_OK', flush=True)


if __name__ == '__main__': main()
