"""Drive only the two isolated test HWNDs; read redacted test-build state."""
import ctypes
from ctypes import wintypes
import time


def run_dnd_ui(task, processes, restart_b):
    user = ctypes.windll.user32
    # Cross-process mouse-message coordinates otherwise get DPI-virtualized
    # from Python's unaware context into the client's PMv2 physical pixels.
    user.SetThreadDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    user.SetThreadDpiAwarenessContext.restype = ctypes.c_void_p
    user.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4))
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.SendMessageW.restype = wintypes.LPARAM
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    keys = 'phase self known peer hidden notice game own_emote peer_emote online synced rtt can_invite width height'.split()
    def state(i):
        try:
            values = list(map(int, (task / (('alice', 'bob')[i] + '.dnd.txt')).read_text().split()))
            return dict(zip(keys, values)) if len(values) == len(keys) else {}
        except (OSError, ValueError): return {}
    def await_state(predicate, label, timeout=12):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            a, b = state(0), state(1)
            if a and b and predicate(a, b): return
            time.sleep(.05)
        raise AssertionError(label + ' ' + str([state(0), state(1)]))
    def window(i):
        found = []
        @callback_type
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == processes[i].pid:
                title = ctypes.create_unicode_buffer(256)
                user.GetWindowTextW(hwnd, title, 256)
                if title.value.startswith('Plane Pet '): found.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        assert len(found) == 1, ('Isolated client HWND not unique', i, len(found))
        return found[0]
    def command(i, menu): user.SendMessageW(window(i), 0x111, menu, 0)
    def click(i, x, y):
        hwnd = window(i); point = x | (y << 16)
        user.SendMessageW(hwnd, 0x201, 1, point); user.SendMessageW(hwnd, 0x202, 0, point)
    # Previous base-suite round has finished; both return to pet with real close logic.
    for i in range(2): user.SendMessageW(window(i), 0x10, 0, 0)
    await_state(lambda a,b: a['phase'] == b['phase'] == 0 and a['known'] and b['known'], 'Return to idle')
    command(0, 2001); command(1, 2002); command(1, 2009)
    await_state(lambda a,b: a['peer'] and b['self'] and b['synced'] and b['hidden'], 'Hidden DND sync')
    processes[1] = restart_b()
    await_state(lambda a,b: a['known'] and a['peer'] and b['self'] and b['synced'] and b['hidden'] and
                a['can_invite'] and b['can_invite'], 'DND restart persistence')
    command(0, 2004)
    await_state(lambda a,b: a['notice'] and a['phase'] == b['phase'] == 0 and b['hidden'], 'Blocked notice, recipient not woken')
    click(0, 82, 130); click(1, 193, 130)
    await_state(lambda a,b: a['peer_emote'] == 4 and b['peer_emote'] == 1 and a['notice'], 'Emotes while blocked')
    click(0, 220, 82)
    await_state(lambda a,b: not a['notice'] and a['phase'] == b['phase'] == 0, 'Local cancel')
    command(0, 2004)
    await_state(lambda a,b: a['notice'], 'Second blocked notice')
    start = time.monotonic()
    await_state(lambda a,b: not a['notice'] and b['hidden'], 'Three-second auto close', 5)
    assert 2 <= time.monotonic() - start <= 4.5
    command(1, 2009)
    await_state(lambda a,b: not a['peer'] and not b['self'] and b['synced'], 'DND off sync')
    command(0, 2004)
    await_state(lambda a,b: a['phase'] == b['phase'] == 1 and not b['hidden'], 'Real invite wakes hidden receiver')
    command(1, 2009)
    await_state(lambda a,b: a['notice'] and a['phase'] == b['phase'] == 0 and b['hidden'], 'DND interrupts received invite')
    command(0, 2009)
    await_state(lambda a,b: b['peer'] and a['self'] and a['synced'], 'Both DND')
    command(1, 2001); command(1, 2004)
    await_state(lambda a,b: b['notice'] and a['phase'] == b['phase'] == 0, 'Both-DND invite blocked')
    click(1, 220, 82); command(0, 2009)
    await_state(lambda a,b: not b['peer'] and a['synced'], 'Normal receiver restored')
    command(1, 2004)
    await_state(lambda a,b: a['phase'] == b['phase'] == 1, 'DND person may initiate')
    click(0, 60, 115)
    await_state(lambda a,b: a['phase'] == b['phase'] == 2, 'Acceptance starts countdown')
    command(0, 2009)
    await_state(lambda a,b: a['phase'] == b['phase'] == 3 and a['game'] and b['game'], 'DND does not interrupt committed game')
    print('PUBLIC_REAL_GUI_DND_SYNC_RESTART_BLOCK_CANCEL_TIMEOUT_EMOTES_HIDE_RACE_BOTH_INITIATE_GAME_OK', flush=True)
