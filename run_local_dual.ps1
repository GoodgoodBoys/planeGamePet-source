param(
    [ValidateRange(1, 999999)]
    [int]$Code = 123456,
    [ValidateRange(5, 180)]
    [int]$MatchSeconds = 180,
    [ValidateRange(1, 65535)]
    [int]$Port = 32110,
    [switch]$StartSecondHidden,
    [switch]$ManualPairing,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$server = Join-Path $dist "PlanePetServer.exe"
$aliceClient = Join-Path $dist "PlanePetAlice.exe"
$bobClient = Join-Path $dist "PlanePetBob.exe"

if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1")
}
if (-not (Test-Path -LiteralPath $server) -or
    -not (Test-Path -LiteralPath $aliceClient) -or
    -not (Test-Path -LiteralPath $bobClient)) {
    throw "Build output is missing. Run build.ps1 first."
}
$codeText = $Code.ToString("000000")
$clientCode = if ($ManualPairing) { 0 } else { $Code }
$serverArgs = @("--port=$Port", "--seconds=$MatchSeconds")
$serverProcess = Start-Process -FilePath $server -ArgumentList $serverArgs `
    -WorkingDirectory $dist -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 350
$serverProcess.Refresh()
if ($serverProcess.HasExited) {
    throw "Local server failed to listen on UDP $Port. Choose another -Port or close the process using it."
}

$aliceArgs = @(
    "--server=127.0.0.1:$Port", "--code=$clientCode", "--slot=1",
    "--client=5101", "--name=Alice", "--peer=Bob",
    "--pet-x=120", "--pet-y=180"
)
$bobArgs = @(
    "--server=127.0.0.1:$Port", "--code=$clientCode", "--slot=2",
    "--client=5102", "--name=Bob", "--peer=Alice",
    "--pet-x=430", "--pet-y=350",
    ("--hidden=" + $(if ($StartSecondHidden) { "1" } else { "0" }))
)

$alice = Start-Process -FilePath $aliceClient -ArgumentList $aliceArgs `
    -WorkingDirectory $dist -PassThru
$bob = Start-Process -FilePath $bobClient -ArgumentList $bobArgs `
    -WorkingDirectory $dist -PassThru

Write-Host "PC_PET_LOCAL_TEST_STARTED"
Write-Host "Pairing code: $codeText"
Write-Host "Server PID: $($serverProcess.Id)"
Write-Host "Alice PID:  $($alice.Id)"
Write-Host "Bob PID:    $($bob.Id)"
if ($ManualPairing) {
    Write-Host "Manual pairing is waiting. Click each pet, enter $codeText, then press Enter."
} else {
    Write-Host "Right-click either visible plane to invite."
}
Write-Host "For the hidden wake-up test, run with -StartSecondHidden."
