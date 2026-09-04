param(
    [ValidatePattern('^[0-9A-Fa-f]{32}$')]
    [Parameter(Mandatory = $true)]
    [string]$NetworkKey,
    [string]$BindAddress = "0.0.0.0",
    [ValidateRange(1, 65535)]
    [int]$Port = 32110,
    [ValidateRange(5, 180)]
    [int]$MatchSeconds = 180,
    [string]$Store = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$server = Join-Path $dist "PlanePetServer.exe"
if (-not $SkipBuild) { & (Join-Path $root "build.ps1") }
if (-not (Test-Path -LiteralPath $server)) { throw "Missing $server" }
if ([string]::IsNullOrWhiteSpace($Store)) {
    $Store = Join-Path $env:LOCALAPPDATA "PlanePet\network_server_bindings.db"
}
$arguments = @(
    "--bind=$BindAddress", "--port=$Port", "--seconds=$MatchSeconds",
    "--invite-seconds=300", "--network-key=$NetworkKey", "--store=$Store"
)
$process = Start-Process -FilePath $server -ArgumentList $arguments `
    -WorkingDirectory $dist -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 350
$process.Refresh()
if ($process.HasExited) { throw "Server failed to listen on $BindAddress`:$Port." }
Write-Host "PLANE_PET_NETWORK_SERVER_STARTED"
Write-Host "Listen: $BindAddress`:$Port"
Write-Host "PID:    $($process.Id)"
Write-Host "Keep the 32-hex network key private and use the same key on both clients."
