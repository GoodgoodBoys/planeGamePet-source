param(
    [ValidateRange(1, 999999)]
    [int]$Code = 735190,
    [ValidateRange(0, 65535)]
    [int]$Port = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-timeout-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
if ($Port -eq 0) {
    $probe = [Net.Sockets.UdpClient]::new(0)
    try { $Port = ([Net.IPEndPoint]$probe.Client.LocalEndPoint).Port }
    finally { $probe.Dispose() }
}
$server = $null
$alice = $null
$bob = $null
try {
    $aliceEvents = Join-Path $testRoot "alice.events.csv"
    $server = Start-Process -FilePath (Join-Path $dist "PlanePetServer.exe") `
        -ArgumentList @("--port=$Port", "--invite-seconds=1",
            "--store=$(Join-Path $testRoot 'bindings.db')") `
        -WorkingDirectory $dist -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 350
    $server.Refresh()
    if ($server.HasExited) { throw "Server did not become ready on UDP $Port." }
    $bob = Start-Process -FilePath (Join-Path $dist "PlanePetClient.exe") `
        -ArgumentList @("--server=127.0.0.1:$Port", "--code=$Code",
            "--state=$(Join-Path $testRoot 'bob.binding')", "--client=29102",
            "--telemetry=1") -WorkingDirectory $dist -PassThru
    $alice = Start-Process -FilePath (Join-Path $dist "PlanePetClient.exe") `
        -ArgumentList @("--server=127.0.0.1:$Port", "--code=$Code",
            "--state=$(Join-Path $testRoot 'alice.binding')",
            "--events=$aliceEvents", "--client=29101", "--auto-invite=1",
            "--telemetry=1") -WorkingDirectory $dist -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    $timedOut = $false
    do {
        Start-Sleep -Milliseconds 100
        if (Test-Path -LiteralPath $aliceEvents) {
            $timedOut = (Get-Content -Raw -LiteralPath $aliceEvents) -match
                ',invite_timed_out,'
        }
    } while (-not $timedOut -and [DateTime]::UtcNow -lt $deadline)
    if (-not $timedOut) { throw "Invite timeout was not reported to sender." }
    $alice.Refresh()
    $bob.Refresh()
    if ($alice.MainWindowTitle -notlike "Plane Pet - *" -or
        $bob.MainWindowTitle -notlike "Plane Pet - *") {
        throw "Clients did not return to the pet after invite timeout."
    }
    if ((Test-Path (Join-Path $testRoot "alice.history")) -or
        (Test-Path (Join-Path $testRoot "bob.history"))) {
        throw "An unanswered invite incorrectly created match history."
    }
    Write-Host "PC_PET_INVITE_TIMEOUT_OK"
    Write-Host "sender_feedback=1 returned_idle=1 history_created=0"
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
