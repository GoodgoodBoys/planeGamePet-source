param([string]$Credential = '', [string]$Url = 'wss://8.166.124.212:32112/v1/tunnel')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Credential) { $Credential = Join-Path $env:LOCALAPPDATA 'PlanePet\tunnel.credential' }
if (-not (Test-Path -LiteralPath $Credential -PathType Leaf)) {
    throw 'An existing encrypted test credential is required; this smoke test never enrolls a new identity.'
}
$scope = Join-Path $root ('dist\network-smoke-' + [guid]::NewGuid().ToString('N'))
$profile = Join-Path $scope 'PlanePet'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$copy = Join-Path $profile 'tunnel-qa-net.credential'
Copy-Item -LiteralPath $Credential -Destination $copy
$portProbe = [Net.Sockets.UdpClient]::new(0)
$port = ([Net.IPEndPoint]$portProbe.Client.LocalEndPoint).Port
$portProbe.Dispose()
$process = $null
try {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $root 'dist\PlanePetTunnel.exe'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.Environment['LOCALAPPDATA'] = $scope
    foreach ($argument in @('--instance=qa-net', '--enroll=0', "--local-port=$port", "--url=$Url")) {
        [void]$start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    $statusFile = Join-Path $profile 'tunnel-qa-net.status'
    $connected = $false
    for ($attempt = 0; $attempt -lt 60; ++$attempt) {
        if ($process.HasExited) { throw "Isolated tunnel exited: $($process.ExitCode)" }
        if (Test-Path -LiteralPath $statusFile) {
            $status = Get-Content -Raw -LiteralPath $statusFile
            if ($status -match '^PPNET1 [0-9]+ 2 0') { $connected = $true; break }
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $connected) { throw "Tunnel did not connect. Redacted state: $status" }
    "PUBLIC_TLS_PROXY_SMOKE_OK endpoint=$Url existing_credential=1 telemetry=0 gameplay=0"
} finally {
    if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    $validated = [IO.Path]::GetFullPath($copy)
    if ($validated.StartsWith([IO.Path]::GetFullPath($scope) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $copy -Force -ErrorAction SilentlyContinue }
}
