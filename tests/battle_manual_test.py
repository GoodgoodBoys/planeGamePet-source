"""Opt-in visible production clients for manual public acceptance; isolated QA data."""
import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import time
import uuid
from public_latency_probe import fingerprints, valid_credential
from server_edge_test import control, recv_until

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--allow-public-test', action='store_true', required=True)
    parser.add_argument('--auto-invite', action='store_true', help='Open the first game using the existing production CLI')
    options = parser.parse_args()
    task = ROOT/'dist'/('battle-manual-'+uuid.uuid4().hex)
    profile = task/'PlanePet'; profile.mkdir(parents=True)
    original = Path(os.environ['LOCALAPPDATA'])/'PlanePet'
    before = fingerprints(original)
    env = dict(os.environ, LOCALAPPDATA=str(task))
    tunnels, clients, copies, ports = [], [], [], []
    code, identity = secrets.randbelow(899999)+100000, secrets.randbelow(500000000)+1000000000
    try:
        for i, side in enumerate(('a', 'b')):
            source = original/f'tunnel-dual-qa-{side}.credential'; valid_credential(source)
            instance = 'manual-'+task.name[-8:]+'-'+side
            copy = profile/f'tunnel-{instance}.credential'; shutil.copyfile(source, copy); copies.append(copy)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0)); ports.append(probe.getsockname()[1])
            tunnels.append(subprocess.Popen([str(ROOT/'dist/PlanePetTunnel.exe'), f'--instance={instance}',
                '--enroll=0', f'--local-port={ports[i]}', '--url=wss://8.166.124.212:32112/v1/tunnel'],
                env=env, creationflags=subprocess.CREATE_NO_WINDOW))
        deadline = time.monotonic()+35
        while time.monotonic() < deadline:
            logs = list(profile.glob('*.log'))
            if len(logs) == 2 and all(' connected error=0' in p.read_text() for p in logs): break
            assert all(p.poll() is None for p in tunnels)
            time.sleep(.1)
        else: raise RuntimeError('Public tunnel timeout')
        for i, side in enumerate(('a', 'b')):
            clients.append(subprocess.Popen([str(ROOT/'dist/PlanePetClient.exe'),
                f'--server=127.0.0.1:{ports[i]}', f'--state={task/(side+".binding")}',
                f'--client={identity+i}', f'--code={code}', f'--slot={i+1}',
                f'--name=BATTLE-QA-{side.upper()}', f'--pet-x={120+i*400}', '--pet-y=170',
                '--telemetry=0', '--update-enabled=0', *(['--auto-accept=1'] if i else
                    (['--auto-invite=1'] if options.auto_invite else []))], env=env))
        print('BATTLE_MANUAL_READY '+str(task), flush=True)
        deadline = time.monotonic()+300
        while time.monotonic() < deadline and any(p.poll() is None for p in clients) and not (task/'DONE').exists():
            time.sleep(.2)
    finally:
        for p in clients:
            if p.poll() is None: p.terminate(); p.wait(timeout=5)
        try:
            binding = task/'a.binding'
            if binding.is_file() and tunnels and tunnels[0].poll() is None:
                fields = binding.read_text().split()
                assert fields[:2] == ['PLANE_PET_CLIENT', '1']
                device, pair, low, high = map(int, fields[2:6]); assert device == identity
                if pair:
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                        sock.bind(('127.0.0.1', 0)); sock.connect(('127.0.0.1', ports[0])); sock.settimeout(.15)
                        for attempt in range(8):
                            sock.send(control(6, device, identity+3000, binding=pair, low=low, high=high))
                            try:
                                recv_until(sock, lambda p: p[:4] == b'PB\x03\x04' and p[32] == 7)
                                print('BATTLE_MANUAL_SYNTHETIC_BINDING_REMOVED_OK', flush=True); break
                            except AssertionError:
                                if attempt == 7: raise
        finally:
            for p in tunnels:
                if p.poll() is None: p.terminate(); p.wait(timeout=5)
            for copy in copies:
                assert copy.resolve().parent == profile.resolve(); copy.unlink(missing_ok=True)
            result = {'original_profiles_unchanged': fingerprints(original) == before,
                'qa_copies_removed': all(not p.exists() for p in copies)}
            (task/'safety.json').write_text(json.dumps(result, indent=2)); assert all(result.values())
            print('BATTLE_MANUAL_DATA_SAFETY_OK', flush=True)


if __name__ == '__main__': main()
