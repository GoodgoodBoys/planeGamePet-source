param(
    [string]$Server = "127.0.0.1:32110",
    [ValidatePattern('^$|^[0-9A-Fa-f]{32}$')]
    [string]$NetworkKey = "",
    [ValidateRange(1, 4294967295)]
    [uint32]$ClientId = 0,
    [string]$Name = "Me",
    [string]$PeerName = "Friend",
    [string]$State = "",
    [switch]$Hidden,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$client = Join-Path $dist "PlanePetClient.exe"
if (-not $SkipBuild) { & (Join-Path $root "build.ps1") }
if (-not (Test-Path -LiteralPath $client)) { throw "Missing $client" }
if ([string]::IsNullOrWhiteSpace($State)) {
    $safeServer = $Server -replace '[^A-Za-z0-9_.-]', '_'
    $State = Join-Path $env:LOCALAPPDATA "PlanePet\client_$safeServer.binding"
}
$arguments = @(
    "--server=$Server", "--network-key=$NetworkKey",
    "--name=$Name", "--peer=$PeerName", "--state=$State", "--code=0",
    ("--hidden=" + $(if ($Hidden) { "1" } else { "0" }))
)
if ($ClientId -ne 0) { $arguments += "--client=$ClientId" }
$process = Start-Process -FilePath $client -ArgumentList $arguments `
    -WorkingDirectory $dist -PassThru
Write-Host "PLANE_PET_CLIENT_STARTED"
Write-Host "Server: $Server"
Write-Host "PID:    $($process.Id)"
