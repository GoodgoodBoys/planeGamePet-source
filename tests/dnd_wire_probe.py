"""PC DND protocol integration, usable with isolated UDP or two authorized WSS tunnels.

Creates only a fresh synthetic binding and removes it in finally. No telemetry.
"""
import binascii
import secrets
import socket
import struct
import time
from server_edge_test import control, game, recv_until


def snapshot(data):
    if data[:4] != b'PL\x05\x0d' or len(data) < 89:
        return None
    return dict(revision=struct.unpack_from('<I', data, 22)[0], known=data[26], enabled=data[27],
                capable=data[28], interrupted=data[29], operation=struct.unpack_from('<I', data, 30)[0],
                reason=data[34], context=data[35:51] + data[59:60], phase=data[59],
                inviter=data[60], end=data[62], online=data[63], sequence=struct.unpack_from('<I', data, 8)[0])


def run_dnd_wire(ports):
    sockets, credentials, sessions, sequences, states = [], [], [0, 0], [0, 0], [None, None]
    base = secrets.randbelow(0x1fffffff) + 0x60000000
    code = secrets.randbelow(900000) + 100000
    def send_control(index, kind, capable=True, request=None):
        binding, low, high = credentials[index] if index < len(credentials) else (0, 0, 0)
        packet = bytearray(control(kind, base + index, request or base + index + 10,
                                   code, binding, low, high))
        packet[37] = 64 if capable else 0
        struct.pack_into('<H', packet, 38, binascii.crc_hqx(packet[:38], 0xffff))
        sockets[index].send(packet)
    def send(index, kind, payload):
        sequences[index] += 1
        sockets[index].send(game(kind, sessions[index], sequences[index], payload))
    def state(index, predicate=lambda s: True):
        data = recv_until(sockets[index], lambda p: snapshot(p) is not None and predicate(snapshot(p)), 4)
        states[index] = snapshot(data)
        return states[index]
    def preference(index, revision, enabled):
        send(index, 12, struct.pack('<IB', revision, enabled))
        return state(index, lambda s: s['revision'] == revision)
    def action(index, operation, value, context=None):
        send(index, 10, struct.pack('<IB', operation, value) + (context or states[index]['context']))
    try:
        for port in ports:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(('127.0.0.1', 0)); sock.connect(('127.0.0.1', port)); sock.settimeout(.1)
            sockets.append(sock)
        send_control(0, 1)
        recv_until(sockets[0], lambda d: d[:2] == b'PB' and d[32] == 1, 4)
        send_control(1, 1)
        for i in range(2):
            matched = recv_until(sockets[i], lambda d: d[:2] == b'PB' and d[32] == 2, 4)
            credentials.append(struct.unpack_from('<III', matched, 16))
            send_control(i, 3)
            welcome = recv_until(sockets[i], lambda d: d[:4] == b'PL\x05\x02', 4)
            sessions[i] = struct.unpack_from('<I', welcome, 4)[0]
        preference(0, 1, 0)
        state(0, lambda s: s['online'] == 3)
        action(0, 100, 1)
        assert state(0, lambda s: s['operation'] == 100)['reason'] == 2
        preference(1, 1, 1)
        state(0, lambda s: s['enabled'] == 2)
        action(0, 101, 1)
        blocked = state(0, lambda s: s['operation'] == 101)
        assert blocked['phase'] == 0 and blocked['reason'] == 1
        assert state(1)['phase'] == 0
        # Emotes pass in both directions while receiver is DND.
        for i, value in ((0, 4), (1, 7)):
            send(i, 8, bytes([value]))
            recv_until(sockets[1-i], lambda d: d[:4] == b'PL\x05\x08' and d[22] == value, 4)
        preference(1, 2, 0)
        state(0, lambda s: s['enabled'] == 0)
        action(0, 102, 1)
        waiting = state(1, lambda s: s['phase'] == 1)
        preference(1, 3, 1)
        ended = state(0, lambda s: s['interrupted'] == 1)
        assert ended['phase'] == 0 and ended['end'] == 5 and ended['context'][8:16] == waiting['context'][8:16]
        # A late accept retains the old context and cannot revive this invitation.
        action(1, 103, 2, waiting['context'])
        assert state(1, lambda s: s['phase'] == 0)['interrupted'] == 1
        action(1, 104, 1)
        state(0, lambda s: s['phase'] == 1 and s['inviter'] == 2)
        action(0, 105, 2)
        state(1, lambda s: s['phase'] == 2)
        preference(0, 2, 1)
        assert state(0)['phase'] == 2
        state(0, lambda s: s['phase'] == 3)
        preference(1, 4, 0)
        assert state(1)['phase'] == 3
        print('DND_WIRE_UNKNOWN_BLOCK_EMOTES_INTERRUPT_STALE_ACCEPT_SELF_INVITE_COUNTDOWN_PLAY_OK', flush=True)
        # A legacy process reconnects: receives only the original Snapshot/Meta.
        send_control(0, 3, capable=False, request=base + 100)
        welcome = recv_until(sockets[0], lambda d: d[:4] == b'PL\x05\x02', 4)
        sessions[0] = struct.unpack_from('<I', welcome, 4)[0]; sequences[0] = 0
        legacy = recv_until(sockets[0], lambda d: d[:4] == b'PL\x05\x04', 4)
        assert legacy[30] == 4 and legacy[33] == 3  # restart ended game, disconnected
        send(0, 8, b'\x03')
        state(1, lambda s: s['phase'] == 0)
        preference(1, 5, 1)
        send(0, 8, b'\x01')
        recv_until(sockets[0], lambda d: d[:4] == b'PL\x05\x04' and d[30] == 0 and d[33] == 5, 4)
        assert state(1)['phase'] == 0
        print('DND_WIRE_MIXED_LEGACY_FALLBACK_NO_RECIPIENT_INVITATION_OK', flush=True)
    finally:
        try:
            if credentials:
                send_control(0, 6)
                recv_until(sockets[0], lambda d: d[:2] == b'PB' and d[32] == 7, 4)
                print('DND_WIRE_SYNTHETIC_BINDING_REMOVED_OK', flush=True)
        finally:
            for sock in sockets: sock.close()


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int)
    parser.add_argument('--peer-port', type=int)
    args = parser.parse_args()
    run_dnd_wire((args.port, args.peer_port or args.port))
