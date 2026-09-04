param(
    [ValidateRange(1, 999999)]
    [int]$Code = 864209,
    [ValidateRange(0, 65535)]
    [int]$Port = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-restart-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
if ($Port -eq 0) {
    $probe = [Net.Sockets.UdpClient]::new(0)
    try { $Port = ([Net.IPEndPoint]$probe.Client.LocalEndPoint).Port }
    finally { $probe.Dispose() }
}

$server = $null
$alice = $null
$bob = $null

function Start-Alice([bool]$autoInvite) {
    $arguments = @(
        "--server=127.0.0.1:$Port", "--code=$(if ($autoInvite) {$Code} else {0})",
        "--state=$(Join-Path $testRoot 'alice.binding')",
        "--events=$(Join-Path $testRoot 'alice.events.csv')",
        "--client=28101", "--telemetry=1"
    )
    if ($autoInvite) { $arguments += "--auto-invite=1" }
    return Start-Process -FilePath (Join-Path $dist "PlanePetClient.exe") `
        -ArgumentList $arguments -WorkingDirectory $dist -PassThru
}

function Event-Count([string]$path, [string]$event) {
    if (-not (Test-Path -LiteralPath $path)) { return 0 }
    return ([regex]::Matches((Get-Content -Raw -LiteralPath $path),
        ",$([regex]::Escape($event)),")).Count
}

function History-Total([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return -1 }
    $header = (Get-Content -LiteralPath $path -TotalCount 1) -split ' '
    if ($header.Count -lt 7 -or $header[0] -ne 'PLANE_PET_HISTORY' -or
        [int]$header[1] -ne 2) { return -2 }
    return [int]$header[2]
}

try {
    $store = Join-Path $testRoot "bindings.db"
    $aliceEvents = Join-Path $testRoot "alice.events.csv"
    $bobEvents = Join-Path $testRoot "bob.events.csv"
    $aliceHistory = Join-Path $testRoot "alice.history"
    $bobHistory = Join-Path $testRoot "bob.history"
    $server = Start-Process -FilePath (Join-Path $dist "PlanePetServer.exe") `
        -ArgumentList @("--port=$Port", "--seconds=20", "--store=$store") `
        -WorkingDirectory $dist -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 350
    $server.Refresh()
    if ($server.HasExited) { throw "Server did not become ready on UDP $Port." }
    $bob = Start-Process -FilePath (Join-Path $dist "PlanePetClient.exe") `
        -ArgumentList @("--server=127.0.0.1:$Port", "--code=$Code",
            "--state=$(Join-Path $testRoot 'bob.binding')", "--events=$bobEvents",
            "--client=28102", "--auto-accept=1", "--telemetry=1") `
        -WorkingDirectory $dist -PassThru
    $alice = Start-Alice $true

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
    } while ((Event-Count $aliceEvents "game_started") -lt 1 -and
             [DateTime]::UtcNow -lt $deadline)
    $startedBefore = Event-Count $aliceEvents "game_started"
    if ($startedBefore -ne 1) { throw "Initial game did not start exactly once." }

    # Simulate a process crash: no Goodbye and no local shutdown history write.
    Stop-Process -Id $alice.Id -Force
    $alice.WaitForExit(3000) | Out-Null
    $alice = Start-Alice $false

    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 100
    } while (((History-Total $aliceHistory) -ne 1 -or
              (History-Total $bobHistory) -ne 1) -and
             [DateTime]::UtcNow -lt $deadline)
    if ((History-Total $aliceHistory) -ne 1 -or
        (History-Total $bobHistory) -ne 1) {
        throw "Restarted match did not settle to one history record per side."
    }
    if ((Event-Count $aliceEvents "game_started") -ne $startedBefore) {
        throw "Restart incorrectly restored the old match as Playing."
    }
    if ((Event-Count $bobEvents "game_finished") -ne 1) {
        throw "The remaining peer did not receive a single match result."
    }

    # Restart again while the server still holds Finished.  The round ID stored
    # in history must make this replay idempotent.
    Stop-Process -Id $alice.Id -Force
    $alice.WaitForExit(3000) | Out-Null
    $alice = Start-Alice $false
    Start-Sleep -Seconds 2
    if ((History-Total $aliceHistory) -ne 1 -or
        (Event-Count $aliceEvents "game_started") -ne $startedBefore) {
        throw "Finished snapshot replay duplicated history or game_started."
    }
    Write-Host "PC_PET_RESTART_REGRESSIONS_OK"
    Write-Host "old_playing_restored=0 duplicate_history=0 peer_received_reason=1"
}
finally {
    foreach ($process in @($alice, $bob, $server)) {
        if ($null -ne $process) {
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        }
    }
    if (Test-Path -LiteralPath $testRoot) {
        $resolved = (Resolve-Path -LiteralPath $testRoot).Path
        if ($resolved.StartsWith([IO.Path]::GetTempPath(),
                [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
