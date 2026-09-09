"""Real Windows clients + real server, isolated loopback impairment proxies.

No global network settings or input injection. Motion test binary drives its
own normal mouse-target API; production builds contain no test driver.
"""
import argparse
import csv
import heapq
import json
import math
import os
from pathlib import Path
import random
import select
import socket
import subprocess
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rtt', type=int, default=100)
    parser.add_argument('--loss', type=int, default=2)
    parser.add_argument('--expect-abort', action='store_true')
    parser.add_argument('--battle', action='store_true', help='Use isolated v3 candidate binaries')
    parser.add_argument('--blackout', choices=('countdown', 'playing'), help='Drop ALL traffic on peer A for 7 seconds, then restore')
    parser.add_argument('--ports', type=int, nargs=2, help='Existing isolated public tunnels; do not start a local server')
    args = parser.parse_args()
    assert 0 <= args.rtt <= 300 and 0 <= args.loss <= 10
    assert not args.blackout or (args.battle and not args.expect_abort)
    task = ROOT / 'dist' / ('motion-live-' + uuid.uuid4().hex)
    task.mkdir()
    env = dict(os.environ, LOCALAPPDATA=str(task))
    owned, sockets, proxies = [], [], []
    stop = threading.Event()
    blackout = threading.Event()
    def launch(binary, options):
        startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
        p = subprocess.Popen([str(ROOT/'dist'/binary), *options], env=env, cwd=task,
            startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        owned.append(p); return p
    try:
        if args.ports:
            targets = args.ports
        else:
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]; probe.close()
            launch('PlanePetBattleServer.exe' if args.battle else 'PlanePetServer.exe', [f'--port={port}', f'--store={task / "server.db"}'])
            time.sleep(.3); targets = [port, port]
        for index in range(2):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.bind(('127.0.0.1', 0)); sock.setblocking(False)
            sockets.append(sock)
            def proxy(sock=sock, target=('127.0.0.1', targets[index]), seed=index):
                rng = random.Random(712 + seed); queue = []; serial = 0; client = None
                while not stop.is_set():
                    if seed == 0 and blackout.is_set():
                        queue.clear()
                    ready, _, _ = select.select([sock], [], [], .001)
                    if ready:
                        try: data, source = sock.recvfrom(4096)
                        except ConnectionResetError: continue # owned server was stopped during cleanup
                        if source == target:
                            destination = client
                        else:
                            client = source; destination = target
                        if destination and not (seed == 0 and blackout.is_set()) and rng.randrange(100) >= args.loss:
                            serial += 1
                            heapq.heappush(queue, (time.perf_counter() + args.rtt / 2000 + rng.random() * .02, serial, data, destination))
                    while queue and queue[0][0] <= time.perf_counter():
                        _, _, data, destination = heapq.heappop(queue); sock.sendto(data, destination)
            thread = threading.Thread(target=proxy, daemon=True); thread.start(); proxies.append(thread)
        code = random.randrange(100000, 999999); identity = random.randrange(1000000000, 2000000000)
        for i, name in enumerate(('a', 'b')):
            launch('PlanePetBattleLive.exe' if args.battle else 'PlanePetMotionLive.exe', [f'--server=127.0.0.1:{sockets[i].getsockname()[1]}',
                f'--code={code}', f'--client={identity+i}', f'--slot={i+1}',
                f'--state={task/(name+".binding")}', f'--motion-report={task/(name+".csv")}',
                '--telemetry=0', '--update-enabled=0', '--hidden=1',
                f'--motion-abort={int(args.expect_abort and i == 0)}',
                '--motion-invite=1' if i == 0 else '--auto-accept=1'])
        deadline = time.monotonic() + 55
        rows = []
        cut_at = None
        playing_at = None
        while time.monotonic() < deadline:
            rows = []
            for name in ('a', 'b'):
                try:
                    with (task/(name+'.csv')).open() as f:
                        rows.append([list(map(int, r)) for r in csv.reader(f) if len(r) == 13])
                except (OSError, ValueError): rows.append([])
            if args.blackout:
                phase = 2 if args.blackout == 'countdown' else 3
                if all(r and r[-1][1] == phase for r in rows) and playing_at is None:
                    playing_at = time.monotonic()
                if cut_at is None and playing_at is not None and time.monotonic() - playing_at >= (0 if phase == 2 else 2.5):
                    cut_at = time.monotonic(); blackout.set()
                if cut_at is not None and time.monotonic() - cut_at >= 7:
                    blackout.clear()
                if cut_at is not None and time.monotonic() - cut_at >= 13 and all(r and r[-1][11] for r in rows):
                    break
            elif all(r and any(v[1] == 4 for v in r) for r in rows):
                time.sleep(.3) # let both test binaries finish their final BMP write
                break
            assert all(p.poll() is None for p in owned), 'Owned process exited early'
            time.sleep(.15)
        if args.blackout:
            assert cut_at is not None and not blackout.is_set(), ('Blackout not exercised', task)
            authority_seen = []
            for index, name in enumerate(('a', 'b')):
                terminal = [v for v in rows[index] if v[1] == 4]
                authority_seen.append(bool(terminal))
                # B can return before A resumes. The protocol intentionally
                # does not replay a room that B already returned to Menu.
                # A must still have shown its local connection-lost state,
                # restored the connection, and recorded no guessed defeat.
                if index == 1:
                    assert terminal, ('Online peer did not receive abort', task)
                else:
                    start = next(i for i, v in enumerate(rows[index]) if v[1] == (2 if args.blackout == 'countdown' else 3))
                    assert any(v[1] == 0 and not v[11] for v in rows[index][start:]), ('No local timeout observed', task)
                assert all(v[7] == 1 for v in terminal), ('Wrong authoritative ending', task)
                assert all(v[12] == 0 for v in terminal), ('Abort has winner', task)
                # The recovered client deliberately returns to the pet after
                # acknowledging its late abort; Finished need not stay visible.
                assert rows[index][-1][1] in (0, 4) and rows[index][-1][11], ('Connection not restored', task)
                history = task/(name+'.history')
                assert not history.exists() or history.read_text().split()[2:6] == ['0', '0', '0', '0'], ('Network fault counted as competitive history', task)
            summary = {'blackout': args.blackout, 'seconds': 7, 'restored': True,
                'authoritative_abort_seen': authority_seen, 'both_history_unchanged': True}
            (task/'result.json').write_text(json.dumps(summary, indent=2))
            print('BATTLE_REAL_CLIENT_BLACKOUT_RESTORE_OK ' + str(task), flush=True)
            print(json.dumps(summary), flush=True)
            return
        summary = {'rtt_injected_ms': args.rtt, 'jitter_each_way_ms': 20, 'loss_percent': args.loss, 'public': bool(args.ports), 'expected_abort': args.expect_abort, 'battle_v3': args.battle, 'peers': []}
        for r in rows:
            playing = [v for v in r if v[1] == 3]
            assert len(playing) > 80 and playing[-1][0] - playing[0][0] > (2000000 if args.expect_abort else 4000000), ('No sustained gameplay', task)
            assert all((args.expect_abort or v[6] == v[7] == 0) and v[11] for v in playing), ('Motion fault', task)
            finished = [v for v in r if v[1] == 4]
            assert finished and bool(finished[-1][7]) == args.expect_abort, ('Wrong finish reason', task)
            max_step = 0
            for a, b in zip(playing, playing[1:]):
                dx, dy = (b[4]-a[4])/256, (b[5]-a[5])/256
                travel = math.hypot(dx, dy); max_step = max(max_step, travel)
                assert travel <= 1.5 * (b[2]-a[2]) + .00001, ('Uncommanded jump', a, b)
            assert max(v[4] for v in playing) - min(v[4] for v in playing) > 50 * 256
            summary['peers'].append({'frames': len(playing), 'max_frame_travel_px': max_step,
                'observed_pump_hz': round((len(playing)-1)*1e6/(playing[-1][0]-playing[0][0]), 1),
                'max_pending': max(v[2]-v[3] for v in playing), 'winner': finished[-1][12],
                'final_hp': finished[-1][9:11]})
        assert summary['peers'][0]['winner'] == summary['peers'][1]['winner']
        assert summary['peers'][0]['final_hp'] == summary['peers'][1]['final_hp']
        if args.expect_abort:
            for name in ('a', 'b'):
                assert (task/(name+'.history')).read_text().split()[2:6] == ['0', '0', '0', '0'], 'Abort incorrectly counted as competitive result'
        (task/'result.json').write_text(json.dumps(summary, indent=2))
        print('MOTION_LIVE_WINDOWS_OK ' + str(task), flush=True)
        print(json.dumps(summary), flush=True)
    finally:
        for p in reversed(owned):
            if p.poll() is None: p.terminate(); p.wait(timeout=5)
        stop.set()
        for thread in proxies: thread.join(timeout=2)
        for sock in sockets: sock.close()
        if args.ports and (task/'a.binding').is_file():
            # Only the fresh identity created by THIS test, never a saved user pair.
            from server_edge_test import control, recv_until
            fields = (task/'a.binding').read_text().split()
            assert fields[:2] == ['PLANE_PET_CLIENT', '1']
            device, binding, low, high = map(int, fields[2:6])
            assert device == identity
            if binding:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as cleanup:
                    cleanup.bind(('127.0.0.1', 0)); cleanup.connect(('127.0.0.1', args.ports[0])); cleanup.settimeout(.15)
                    for attempt in range(6):
                        cleanup.send(control(6, device, identity+200, binding=binding, low=low, high=high))
                        try:
                            recv_until(cleanup, lambda p: p[:4] == b'PB\x03\x04' and p[32] == 7)
                            print('MOTION_PUBLIC_SYNTHETIC_BINDING_REMOVED_OK', flush=True); break
                        except AssertionError:
                            if attempt == 5: raise


if __name__ == '__main__': main()
