param(
    [ValidateRange(1, 999999)]
    [int]$Code = 246810,
    [ValidateRange(0, 65535)]
    [int]$Port = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$serverPath = Join-Path $dist "PlanePetServer.exe"
$alicePath = Join-Path $dist "PlanePetAlice.exe"
$bobPath = Join-Path $dist "PlanePetBob.exe"

foreach ($path in @($serverPath, $alicePath, $bobPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing build output: $path"
    }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-gui-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$store = Join-Path $testRoot "bindings.db"
$aliceState = Join-Path $testRoot "alice.binding"
$bobState = Join-Path $testRoot "bob.binding"

if ($Port -eq 0) {
    $probe = [Net.Sockets.UdpClient]::new(0)
    try { $Port = ([Net.IPEndPoint]$probe.Client.LocalEndPoint).Port }
    finally { $probe.Dispose() }
}

function Invoke-GuiStage {
    param(
        [int]$StartupCode,
        [string]$Label
    )
    $server = $null
    $alice = $null
    $bob = $null
    try {
        $server = Start-Process -FilePath $serverPath -ArgumentList @(
            "--port=$Port", "--seconds=10", "--store=$store"
        ) -WorkingDirectory $dist -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 300
        $server.Refresh()
        if ($server.HasExited) { throw "$Label server did not become ready on UDP $Port." }
        $bob = Start-Process -FilePath $bobPath -ArgumentList @(
            "--server=127.0.0.1:$Port", "--code=$StartupCode", "--hidden=1",
            "--state=$bobState", "--client=15102", "--auto-accept=1",
            "--telemetry=1"
        ) -WorkingDirectory $dist -PassThru
        $alice = Start-Process -FilePath $alicePath -ArgumentList @(
            "--server=127.0.0.1:$Port", "--code=$StartupCode",
            "--state=$aliceState", "--client=15101", "--auto-invite=1",
            "--telemetry=1"
        ) -WorkingDirectory $dist -PassThru

        $deadline = [DateTime]::UtcNow.AddSeconds(8)
        $passed = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $alice.Refresh()
            $bob.Refresh()
            if ($alice.MainWindowTitle -like "Plane Pet Game*" -and
                $bob.MainWindowTitle -like "Plane Pet Game*") {
                $passed = $true
                break
            }
        }
        if (-not $passed) {
            throw "$Label failed. Alice='$($alice.MainWindowTitle)' Bob='$($bob.MainWindowTitle)'"
        }
        Write-Host "$Label OK"
        Write-Host "Alice: $($alice.MainWindowTitle)"
        Write-Host "Bob:   $($bob.MainWindowTitle)"
    }
    finally {
        foreach ($process in @($alice, $bob, $server)) {
            if ($null -ne $process) {
                $process.Refresh()
                if (-not $process.HasExited) {
                    Stop-Process -Id $process.Id -Force
                }
            }
        }
        Start-Sleep -Milliseconds 200
    }
}

try {
    Invoke-GuiStage -StartupCode $Code -Label "PC_PET_GUI_FIRST_PAIR"
    if (-not (Test-Path -LiteralPath $store) -or
        -not (Test-Path -LiteralPath $aliceState) -or
        -not (Test-Path -LiteralPath $bobState)) {
        throw "Pairing persistence files were not created."
    }
    Invoke-GuiStage -StartupCode 0 -Label "PC_PET_GUI_AUTO_RESUME"
    Write-Host "PC_PET_GUI_FLOW_OK"
    Write-Host "First pairing, hidden-peer wake-up, and restart auto-resume passed."
}
finally {
    $resolved = (Resolve-Path -LiteralPath $testRoot).Path
    if ($resolved.StartsWith([IO.Path]::GetTempPath(),
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
