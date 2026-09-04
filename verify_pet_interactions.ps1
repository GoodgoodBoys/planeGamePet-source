param(
    [ValidateRange(1, 999999)]
    [int]$Code = 357159,
    [ValidateRange(0, 65535)]
    [int]$Port = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-interaction-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
if ($Port -eq 0) {
    $probe = [Net.Sockets.UdpClient]::new(0)
    try { $Port = ([Net.IPEndPoint]$probe.Client.LocalEndPoint).Port }
    finally { $probe.Dispose() }
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PetInteractionNative {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int index);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hWnd, uint command);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@

# Read and interact with the per-monitor-aware clients in physical pixels.
[PetInteractionNative]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null

function Get-WindowRect($process) {
    $process.Refresh()
    $rect = New-Object PetInteractionNative+RECT
    [PetInteractionNative]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    return $rect
}

function Assert-CompactGameWindow($process, $petRect, [string]$label) {
    $process.Refresh()
    $client = New-Object PetInteractionNative+RECT
    [PetInteractionNative]::GetClientRect($process.MainWindowHandle, [ref]$client) | Out-Null
    if (($client.Right - $client.Left) -ne 240 -or
        ($client.Bottom - $client.Top) -ne 372) {
        throw "$label game client is not 240x372."
    }
    $exStyle = [PetInteractionNative]::GetWindowLongPtr(
        $process.MainWindowHandle, -20).ToInt64()
    if (($exStyle -band 0x80) -eq 0 -or ($exStyle -band 0x40000) -ne 0) {
        throw "$label game window is not using the discreet tool-window style."
    }
    $window = Get-WindowRect $process
    if ([Math]::Abs($window.Left - $petRect.Left) -gt 1 -or
        [Math]::Abs($window.Top - $petRect.Top) -gt 1) {
        throw "$label game window did not open beside the pet position."
    }
}

function Wait-Widths($alice, $bob, [int]$width, [int]$seconds = 5) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Start-Sleep -Milliseconds 100
        $aliceRect = Get-WindowRect $alice
        $bobRect = Get-WindowRect $bob
        if (($aliceRect.Right - $aliceRect.Left) -eq $width -and
            ($bobRect.Right - $bobRect.Left) -eq $width) {
            return
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Window width did not become $width. Alice=$($aliceRect.Right-$aliceRect.Left) Bob=$($bobRect.Right-$bobRect.Left)"
}

function Wait-Width($process, [int]$width, [int]$seconds = 5) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Start-Sleep -Milliseconds 100
        $rect = Get-WindowRect $process
        if (($rect.Right - $rect.Left) -eq $width) { return }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Window width did not become $width. Actual=$($rect.Right-$rect.Left)"
}

function Drag-ClientWindowFromPoint($process, [int]$x, [int]$y,
        [int]$deltaX = 36, [int]$deltaY = 24) {
    if ((Get-HitTest $process $x $y) -ne 1) {
        throw "Requested drag point ($x,$y) is not interactive."
    }
    $before = Get-WindowRect $process
    $startX = $before.Left + $x
    $startY = $before.Top + $y
    [PetInteractionNative]::SetCursorPos($startX, $startY) | Out-Null
    $downParam = [IntPtr](($y -shl 16) -bor ($x -band 0xffff))
    $moveX = $x + $deltaX
    $moveY = $y + $deltaY
    $moveParam = [IntPtr](($moveY -shl 16) -bor ($moveX -band 0xffff))
    try {
        [PetInteractionNative]::SendMessage($process.MainWindowHandle,
            0x0201, [IntPtr]1, $downParam) | Out-Null
        Start-Sleep -Milliseconds 120
        [PetInteractionNative]::SetCursorPos(
            $startX + $deltaX, $startY + $deltaY) | Out-Null
        [PetInteractionNative]::SendMessage($process.MainWindowHandle,
            0x0200, [IntPtr]1, $moveParam) | Out-Null
        Start-Sleep -Milliseconds 180
    }
    finally {
        [PetInteractionNative]::SendMessage($process.MainWindowHandle,
            0x0202, [IntPtr]::Zero, $moveParam) | Out-Null
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 80
        $after = Get-WindowRect $process
        if ([Math]::Abs($after.Left - $before.Left) -ge 10 -and
            [Math]::Abs($after.Top - $before.Top) -ge 10) {
            return $after
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Dragging the pairing input did not move the pet window."
}

function Click-Client($process, [int]$x, [int]$y) {
    $lParam = [IntPtr](($y -shl 16) -bor ($x -band 0xffff))
    [PetInteractionNative]::PostMessage($process.MainWindowHandle, 0x0201,
        [IntPtr]1, $lParam) | Out-Null
}

function Get-HitTest($process, [int]$x, [int]$y) {
    $rect = Get-WindowRect $process
    $screenX = $rect.Left + $x
    $screenY = $rect.Top + $y
    $lParam = [IntPtr](($screenY -shl 16) -bor ($screenX -band 0xffff))
    return [PetInteractionNative]::SendMessage($process.MainWindowHandle,
        0x0084, [IntPtr]::Zero, $lParam).ToInt64()
}

function Test-AnimatedPlaneHit($process) {
    for ($y = 12; $y -le 138; $y += 6) {
        for ($x = 12; $x -le 268; $x += 6) {
            if ((Get-HitTest $process $x $y) -eq 1) { return $true }
        }
    }
    return $false
}

function Wait-Visibility([IntPtr]$handle, [bool]$visible, [int]$seconds = 3) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Start-Sleep -Milliseconds 80
        if ([PetInteractionNative]::IsWindowVisible($handle) -eq $visible) {
            return
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Window visibility did not become $visible."
}

function Find-OwnedInfoWindow($process) {
    $process.Refresh()
    $candidate = [PetInteractionNative]::GetWindow(
        $process.MainWindowHandle, 6)
    if ($candidate -eq $process.MainWindowHandle) { return [IntPtr]::Zero }
    return $candidate
}

function Wait-InfoWindow($process, [string]$label, [int]$clientWidth,
        [int]$clientHeight, [int]$seconds = 3) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Start-Sleep -Milliseconds 80
        $handle = Find-OwnedInfoWindow $process
        if ($handle -ne [IntPtr]::Zero -and
            [PetInteractionNative]::IsWindowVisible($handle)) {
            $client = New-Object PetInteractionNative+RECT
            [PetInteractionNative]::GetClientRect($handle, [ref]$client) | Out-Null
            $dpi = [PetInteractionNative]::GetDpiForWindow($handle)
            $expectedWidth = [Math]::Round($clientWidth * $dpi / 96)
            $expectedHeight = [Math]::Round($clientHeight * $dpi / 96)
            if (($client.Right - $client.Left) -ne $expectedWidth -or
                ($client.Bottom - $client.Top) -ne $expectedHeight) {
                throw "$label client size is $($client.Right-$client.Left)x$($client.Bottom-$client.Top), expected ${expectedWidth}x${expectedHeight} at ${dpi} DPI."
            }
            $exStyle = [PetInteractionNative]::GetWindowLongPtr(
                $handle, -20).ToInt64()
            if (($exStyle -band 0x80) -eq 0 -or
                ($exStyle -band 0x40000) -ne 0) {
                throw "$label is not a discreet tool window."
            }
            return $handle
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$label did not open."
}

function Close-InfoWindow($process, [IntPtr]$handle, [string]$label) {
    [PetInteractionNative]::PostMessage($handle, 0x0010,
        [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 80
        if ((Find-OwnedInfoWindow $process) -eq [IntPtr]::Zero) { return }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$label did not close."
}

function Wait-HistoryFiles([string[]]$paths, [int]$seconds = 12) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Start-Sleep -Milliseconds 100
        $ready = $true
        foreach ($path in $paths) {
            if (-not (Test-Path -LiteralPath $path)) {
                $ready = $false
                break
            }
            $header = (Get-Content -LiteralPath $path -TotalCount 1)
            if ($header -notmatch '^PLANE_PET_HISTORY (1|2) 1 ') {
                $ready = $false
                break
            }
        }
        if ($ready) { return }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Completed match history was not persisted for both clients."
}

function Assert-HistoryFile([string]$path, [string]$label) {
    $lines = @(Get-Content -LiteralPath $path)
    if ($lines.Count -ne 2) { throw "$label history did not contain one recent match." }
    $header = $lines[0] -split ' '
    if ($header.Count -notin @(6, 7) -or $header[0] -ne 'PLANE_PET_HISTORY' -or
        [int]$header[1] -notin @(1, 2) -or [int]$header[2] -ne 1 -or
        ([int]$header[3] + [int]$header[4] + [int]$header[5]) -ne 1) {
        throw "$label history totals are invalid."
    }
    $record = $lines[1] -split ' '
    if ($record.Count -ne 4 -or [uint64]$record[0] -eq 0 -or
        [int]$record[1] -lt 0 -or [int]$record[1] -gt 3 -or
        [int]$record[2] -lt 0 -or [int]$record[2] -gt 3 -or
        [int]$record[3] -lt -1 -or [int]$record[3] -gt 1) {
        throw "$label recent-match health/result record is invalid."
    }
}

function Wait-PetTitles($alice, $bob, [string]$alicePattern,
        [string]$bobPattern, [int]$seconds = 5) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        Start-Sleep -Milliseconds 100
        $alice.Refresh()
        $bob.Refresh()
        if ($alice.MainWindowTitle -like $alicePattern -and
            $bob.MainWindowTitle -like $bobPattern) { return }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Titles did not reach expected state. Alice='$($alice.MainWindowTitle)' Bob='$($bob.MainWindowTitle)'"
}

function Assert-SamePetRect($process, $expected, [string]$label) {
    $actual = Get-WindowRect $process
    if ($actual.Left -ne $expected.Left -or $actual.Top -ne $expected.Top -or
        $actual.Right -ne $expected.Right -or $actual.Bottom -ne $expected.Bottom) {
        throw ("$label changed the fixed pet rectangle. " +
            "expected=$($expected.Left),$($expected.Top),$($expected.Right),$($expected.Bottom) " +
            "actual=$($actual.Left),$($actual.Top),$($actual.Right),$($actual.Bottom)")
    }
}

$server = $null
$pairingClient = $null
$alice = $null
$bob = $null
try {
    $store = Join-Path $testRoot "bindings.db"
    $aliceState = Join-Path $testRoot "alice.binding"
    $bobState = Join-Path $testRoot "bob.binding"
    $aliceEvents = Join-Path $testRoot "alice.events.csv"
    $bobEvents = Join-Path $testRoot "bob.events.csv"
    $aliceHistory = Join-Path $testRoot "alice.history"
    $bobHistory = Join-Path $testRoot "bob.history"
    $server = Start-Process -FilePath (Join-Path $dist "PlanePetServer.exe") `
        -ArgumentList @("--port=$Port", "--seconds=5", "--store=$store") `
        -WorkingDirectory $dist -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 300
    $server.Refresh()
    if ($server.HasExited) { throw "Server did not become ready on UDP $Port." }

    # The pairing card covers the animated plane, so its custom input area must
    # remain both focusable for digits and draggable as a window handle.
    $pairingArgs = @("--server=127.0.0.1:$Port", "--code=0",
        "--state=$(Join-Path $testRoot 'pairing-drag.binding')",
        "--events=$(Join-Path $testRoot 'pairing-drag.events.csv')",
        "--client=17103", "--pet-x=760", "--pet-y=180", "--telemetry=0")
    $pairingClient = Start-Process -FilePath (Join-Path $dist "PlanePetAlice.exe") `
        -ArgumentList $pairingArgs `
        -WorkingDirectory $dist -PassThru
    Wait-Width $pairingClient 280
    if ((Get-HitTest $pairingClient 270 140) -ne 1) {
        throw "The pairing panel was not visible by default."
    }
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0111, [IntPtr]2013, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Get-HitTest $pairingClient 270 140) -ne -1 -or
        (Get-HitTest $pairingClient 82 145) -ne -1 -or
        -not (Test-AnimatedPlaneHit $pairingClient)) {
        throw "Hiding the pairing panel did not expose the flying pet."
    }
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0111, [IntPtr]2003, [IntPtr]::Zero) | Out-Null
    if (-not $pairingClient.WaitForExit(3000)) {
        throw "Pairing panel persistence client did not exit cleanly."
    }
    $pairingClient = Start-Process -FilePath (Join-Path $dist "PlanePetAlice.exe") `
        -ArgumentList $pairingArgs `
        -WorkingDirectory $dist -PassThru
    Wait-Width $pairingClient 280
    if ((Get-HitTest $pairingClient 270 140) -ne -1 -or
        (Get-HitTest $pairingClient 82 145) -ne -1 -or
        -not (Test-AnimatedPlaneHit $pairingClient)) {
        throw "The hidden pairing-panel preference was not restored."
    }
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0111, [IntPtr]2013, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Get-HitTest $pairingClient 270 140) -ne 1) {
        throw "Re-enabling the pairing panel did not restore its hit area."
    }
    $draggedPairingRect = Drag-ClientWindowFromPoint $pairingClient 150 82
    foreach ($character in "864209".ToCharArray()) {
        [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
            0x0102, [IntPtr][int]$character, [IntPtr]::Zero) | Out-Null
    }
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0102, [IntPtr]13, [IntPtr]::Zero) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 80
        $pairingClient.Refresh()
    } while ($pairingClient.MainWindowTitle -notlike "Plane Pet Pairing*" -and
             [DateTime]::UtcNow -lt $deadline)
    if ($pairingClient.MainWindowTitle -notlike "Plane Pet Pairing*") {
        throw "Pairing input stopped accepting digits after the drag."
    }
    Assert-SamePetRect $pairingClient $draggedPairingRect "Pairing submit"
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0111, [IntPtr]2013, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 150
    $pairingClient.Refresh()
    if ($pairingClient.MainWindowTitle -notlike "Plane Pet Pairing*" -or
        (Get-HitTest $pairingClient 270 140) -ne -1 -or
        -not (Test-AnimatedPlaneHit $pairingClient)) {
        throw "Hiding the active search panel interrupted pairing or the pet animation."
    }
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0111, [IntPtr]2013, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Get-HitTest $pairingClient 270 140) -ne 1) {
        throw "The active search panel could not be restored."
    }
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0100, [IntPtr]27, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 150
    [PetInteractionNative]::PostMessage($pairingClient.MainWindowHandle,
        0x0111, [IntPtr]2003, [IntPtr]::Zero) | Out-Null
    if (-not $pairingClient.WaitForExit(3000)) {
        throw "Pairing drag test client did not exit cleanly."
    }
    $pairingClient = $null

    $bob = Start-Process -FilePath (Join-Path $dist "PlanePetBob.exe") `
        -ArgumentList @("--server=127.0.0.1:$Port", "--code=$Code",
            "--state=$bobState", "--events=$bobEvents", "--client=17102",
            "--pet-x=430", "--pet-y=350", "--telemetry=1") `
        -WorkingDirectory $dist -PassThru
    $alice = Start-Process -FilePath (Join-Path $dist "PlanePetAlice.exe") `
        -ArgumentList @("--server=127.0.0.1:$Port", "--code=$Code",
            "--state=$aliceState", "--events=$aliceEvents", "--client=17101",
            "--pet-x=120", "--pet-y=180", "--telemetry=1") `
        -WorkingDirectory $dist -PassThru

    Wait-Widths $alice $bob 280
    Start-Sleep -Milliseconds 500
    $aliceIdleRect = Get-WindowRect $alice
    $bobIdleRect = Get-WindowRect $bob
    if (-not (Test-AnimatedPlaneHit $alice)) {
        throw "Animated plane did not remain interactive."
    }
    if ((Get-HitTest $alice 270 140) -ne -1) {
        throw "Transparent pet area did not pass pointer input through."
    }
    foreach ($x in @(82, 120, 158, 196)) {
        if ((Get-HitTest $alice $x 136) -ne 1) {
            throw "Quick-emote button at x=$x is not interactive."
        }
        Click-Client $alice $x 136
        Start-Sleep -Milliseconds 800
    }
    Click-Client $bob 92 136
    Start-Sleep -Milliseconds 800
    $aliceEmotes = Get-Content -Raw -LiteralPath $aliceEvents
    $bobEmotes = Get-Content -Raw -LiteralPath $bobEvents
    foreach ($value in 1..4) {
        if ($aliceEmotes -notmatch ",quick_emote_sent,$value") {
            throw "Alice did not send quick emote $value."
        }
        if ($bobEmotes -notmatch ",quick_emote_received,$value") {
            throw "Bob did not receive quick emote $value."
        }
    }
    if ($bobEmotes -notmatch ',quick_emote_sent,1' -or
        $aliceEmotes -notmatch ',quick_emote_received,1') {
        throw "Reverse quick-emote delivery failed."
    }

    # Exercise the History and Help entries without resizing or moving the pet.
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2006, [IntPtr]::Zero) | Out-Null
    $historyWindow = Wait-InfoWindow $alice "History" 560 600
    Assert-SamePetRect $alice $aliceIdleRect "History window"
    Close-InfoWindow $alice $historyWindow "History"
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2007, [IntPtr]::Zero) | Out-Null
    $helpWindow = Wait-InfoWindow $alice "Help" 700 740
    Assert-SamePetRect $alice $aliceIdleRect "Help window"
    Close-InfoWindow $alice $helpWindow "Help"

    # Exercise the same WM_COMMAND emitted by the right-click Invite menu item.
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2004, [IntPtr]::Zero) | Out-Null
    Wait-PetTitles $alice $bob "Plane Pet Waiting*" "Plane Pet Invited*"
    Assert-SamePetRect $alice $aliceIdleRect "Outgoing invite"
    Assert-SamePetRect $bob $bobIdleRect "Incoming invite"

    # Outgoing cancel must clear both sides.
    Click-Client $alice 224 105
    Wait-PetTitles $alice $bob "Plane Pet - *" "Plane Pet - *"
    Assert-SamePetRect $alice $aliceIdleRect "Cancel"
    Assert-SamePetRect $bob $bobIdleRect "Peer cancel"

    # Incoming reject must clear both sides.
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2004, [IntPtr]::Zero) | Out-Null
    Wait-PetTitles $alice $bob "Plane Pet Waiting*" "Plane Pet Invited*"
    Assert-SamePetRect $alice $aliceIdleRect "Second invite"
    Assert-SamePetRect $bob $bobIdleRect "Second incoming invite"
    Click-Client $bob 179 108
    Wait-PetTitles $alice $bob "Plane Pet - *" "Plane Pet - *"
    Assert-SamePetRect $alice $aliceIdleRect "Reject peer"
    Assert-SamePetRect $bob $bobIdleRect "Reject"

    # Do-not-disturb must reject silently without waking or resizing Bob.
    [PetInteractionNative]::PostMessage($bob.MainWindowHandle, 0x0111,
        [IntPtr]2009, [IntPtr]::Zero) | Out-Null
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2004, [IntPtr]::Zero) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $dndRejected = $false
    do {
        Start-Sleep -Milliseconds 100
        $alice.Refresh()
        $bob.Refresh()
        if ((Test-Path -LiteralPath $bobEvents) -and
            (Get-Content -Raw -LiteralPath $bobEvents) -match ',invite_auto_rejected_dnd,') {
            $dndRejected = $true
            break
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $dndRejected) { throw "Do-not-disturb did not reject the invite." }
    Wait-PetTitles $alice $bob "Plane Pet - *" "Plane Pet - *"
    Assert-SamePetRect $bob $bobIdleRect "Do-not-disturb"
    [PetInteractionNative]::PostMessage($bob.MainWindowHandle, 0x0111,
        [IntPtr]2009, [IntPtr]::Zero) | Out-Null

    # Incoming accept must transition both clients into the game window.
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2004, [IntPtr]::Zero) | Out-Null
    Wait-PetTitles $alice $bob "Plane Pet Waiting*" "Plane Pet Invited*"
    Assert-SamePetRect $alice $aliceIdleRect "Third invite"
    Assert-SamePetRect $bob $bobIdleRect "Third incoming invite"
    Click-Client $bob 101 108
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 120
        $alice.Refresh()
        $bob.Refresh()
    } while (($alice.MainWindowTitle -notlike "Plane Pet Game*" -or
              $bob.MainWindowTitle -notlike "Plane Pet Game*") -and
             [DateTime]::UtcNow -lt $deadline)
    if ($alice.MainWindowTitle -notlike "Plane Pet Game*" -or
        $bob.MainWindowTitle -notlike "Plane Pet Game*") {
        throw "Accept did not open both game windows."
    }
    Assert-CompactGameWindow $alice $aliceIdleRect "Alice"
    Assert-CompactGameWindow $bob $bobIdleRect "Bob"
    $aliceHandle = $alice.MainWindowHandle
    $bobHandle = $bob.MainWindowHandle
    [PetInteractionNative]::PostMessage($aliceHandle, 0x0312,
        [IntPtr]1, [IntPtr]::Zero) | Out-Null
    Wait-Visibility $aliceHandle $false
    Wait-Visibility $bobHandle $false
    [PetInteractionNative]::PostMessage($aliceHandle, 0x0111,
        [IntPtr]2001, [IntPtr]::Zero) | Out-Null
    [PetInteractionNative]::PostMessage($bobHandle, 0x0111,
        [IntPtr]2001, [IntPtr]::Zero) | Out-Null
    Wait-Visibility $aliceHandle $true
    Wait-Visibility $bobHandle $true
    $alice.Refresh()
    if ($alice.MainWindowTitle -notlike "Plane Pet Game*") {
        throw "Tray restore did not return to the active game."
    }

    Wait-HistoryFiles @($aliceHistory, $bobHistory)
    Assert-HistoryFile $aliceHistory "Alice"
    Assert-HistoryFile $bobHistory "Bob"

    # Closing a finished game must return to the pet, not hide/reopen the result.
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0010,
        [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Wait-Widths $alice $bob 280
    Wait-PetTitles $alice $bob "Plane Pet - *" "Plane Pet - *"
    Wait-Visibility $alice.MainWindowHandle $true
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2006, [IntPtr]::Zero) | Out-Null
    $historyWindow = Wait-InfoWindow $alice "History" 560 600
    Close-InfoWindow $alice $historyWindow "History"

    # A fresh process must load the same local history rather than resetting it.
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2003, [IntPtr]::Zero) | Out-Null
    if (-not $alice.WaitForExit(3000)) { throw "Alice did not exit cleanly." }
    $alice = Start-Process -FilePath (Join-Path $dist "PlanePetAlice.exe") `
        -ArgumentList @("--server=127.0.0.1:$Port", "--code=0",
            "--state=$aliceState", "--events=$aliceEvents", "--client=17101",
            "--pet-x=120", "--pet-y=180", "--telemetry=1") `
        -WorkingDirectory $dist -PassThru
    Wait-Widths $alice $bob 280
    [PetInteractionNative]::PostMessage($alice.MainWindowHandle, 0x0111,
        [IntPtr]2006, [IntPtr]::Zero) | Out-Null
    $historyWindow = Wait-InfoWindow $alice "History" 560 600
    Close-InfoWindow $alice $historyWindow "History"
    Assert-HistoryFile $aliceHistory "Restarted Alice"

    Start-Sleep -Milliseconds 150
    $aliceLog = Get-Content -Raw -LiteralPath $aliceEvents
    $bobLog = Get-Content -Raw -LiteralPath $bobEvents
    foreach ($event in @("invite_sent", "invite_canceled", "invite_rejected_by_peer", "invite_accepted_by_peer", "game_hidden", "game_restored")) {
        if ($aliceLog -notmatch ",$event,") { throw "Alice telemetry is missing $event." }
    }
    foreach ($event in @("invite_received", "invite_rejected", "invite_accepted", "invite_auto_rejected_dnd")) {
        if ($bobLog -notmatch ",$event,") { throw "Bob telemetry is missing $event." }
    }
    Write-Host "PC_PET_INTERACTIONS_OK"
    Write-Host "pairing_panel_default=1 pairing_panel_hide=1 pairing_panel_persist=1 pairing_panel_restore=1 pairing_search_hidden=1 pairing_input_drag=1 pairing_input_after_drag=1 fixed_rect=1 animated_plane_hit=1 hit_through=1 quick_emote_buttons=4 quick_emote_bidirectional=1 history_window=1 help_window=1 menu_invite=1 cancel_sync=1 reject_sync=1 dnd_auto_reject=1 accept_game=1 compact_game=1 nearby_open=1 no_taskbar_button=1 emergency_hide=1 tray_restore=1 finished_close_returns_pet=1 history_persisted=1 history_reloaded=1 local_events=1"
}
finally {
    foreach ($process in @($pairingClient, $alice, $bob, $server)) {
        if ($null -ne $process) {
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        }
    }
    $resolved = (Resolve-Path -LiteralPath $testRoot).Path
    if ($resolved.StartsWith([IO.Path]::GetTempPath(),
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
