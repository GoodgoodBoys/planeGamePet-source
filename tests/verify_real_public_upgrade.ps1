param(
    [string]$FromVersion = '1.0.1',
    [string]$ToVersion = '1.0.2'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$validation = Join-Path $repo "release_out\upgrade-validation-$FromVersion-to-$ToVersion"
$oldProgram = Join-Path $repo "dist\PlanePet-Public-Single-$FromVersion\PlanePet.exe"
$oldUpdater = Join-Path $validation "PlanePetUpdater-$FromVersion.exe"
$publicProgram = Join-Path $validation "public-PlanePet-$ToVersion.exe"
$manifest = Get-Content -Raw -LiteralPath (Join-Path $repo "release_out\v$ToVersion\plane-pet\windows\stable\latest.json") | ConvertFrom-Json
if ($manifest.version -ne $ToVersion -or
    (Get-FileHash -LiteralPath $publicProgram).Hash.ToLowerInvariant() -ne $manifest.sha256) {
    throw 'Downloaded public package does not match release manifest'
}
foreach ($inputFile in @($oldProgram, $oldUpdater)) {
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) { throw "Missing $inputFile" }
}
# Do not overlap the user's single-instance launcher or an update transaction.
foreach ($mutexName in @('PlanePetPublicLauncher', 'PlanePetUpdateTransaction')) {
    $existing = $null
    if ([Threading.Mutex]::TryOpenExisting($mutexName, [ref]$existing)) {
        $existing.Dispose()
        throw "User's application/update is already running: $mutexName"
    }
}
$testRoot = Join-Path $repo ('release_out\real-upgrade-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$appData = Join-Path $testRoot 'appdata'
$data = Join-Path $appData 'PlanePet'
$package = Join-Path $data "updates\$ToVersion\PlanePet.exe.download"
$target = Join-Path $testRoot 'PlanePet.exe'
New-Item -ItemType Directory -Path (Split-Path -Parent $package) | Out-Null
Copy-Item -LiteralPath $oldProgram -Destination $target
Copy-Item -LiteralPath $publicProgram -Destination $package
# Synthetic, unbound data only. No real identities, binding tokens, or telemetry.
$fixtures = @{
    'public.binding' = "PLANE_PET_CLIENT 1 1729102102 0 0 0 0 1`n"
    'public.settings' = "PLANE_PET_SETTINGS 3 0 0 1 0 0`n"
    'public.history' = "PLANE_PET_HISTORY 2 3 1 1 1 0`n1720000000000 1 1 0`n1710000000000 0 1 -1`n1700000000000 1 0 1`n"
}
$fixtureHashes = @{}
foreach ($name in $fixtures.Keys) {
    $path = Join-Path $data $name
    # Match the CRLF text files emitted by the Windows client's ofstream.
    $fixtureText = $fixtures[$name].Replace("`n", [Environment]::NewLine)
    [IO.File]::WriteAllText($path, $fixtureText, [Text.UTF8Encoding]::new($false))
    $fixtureHashes[$name] = (Get-FileHash -LiteralPath $path).Hash
}
$socket = [Net.Sockets.UdpClient]::new(0)
$testPort = $socket.Client.LocalEndPoint.Port
$socket.Dispose()
$updaterProcess = $null
try {
    $parent = Start-Process -FilePath (Join-Path $repo 'dist\PlanePetUpdateHealthStub.exe') -WindowStyle Hidden -PassThru
    $info = [Diagnostics.ProcessStartInfo]::new($oldUpdater)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.Environment['LOCALAPPDATA'] = $appData
    $info.Environment['PLANE_PET_LOCAL_PORT'] = "$testPort"
    $info.Arguments = "--parent-pid=$($parent.Id) --target=`"$target`" --package=`"$package`" --sha256=$($manifest.sha256) --version=$ToVersion --data=`"$data`" --trace=1"
    $updaterProcess = [Diagnostics.Process]::Start($info)
    if (-not $updaterProcess.WaitForExit(55000)) { throw 'Real updater timed out' }
    if ($updaterProcess.ExitCode -ne 0) { throw "Real updater failed: $($updaterProcess.ExitCode)" }
    if ((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -ne $manifest.sha256 -or
        (Test-Path -LiteralPath "$target.old") -or
        (Test-Path -LiteralPath (Join-Path $data 'update.pending')) -or
        (Test-Path -LiteralPath (Join-Path $data 'update.failure'))) {
        throw 'Install did not commit cleanly'
    }
    $trace = Get-Content -Raw -LiteralPath (Join-Path $data 'update.trace')
    if ($trace -notmatch 'health_confirmed') { throw 'New real launcher did not confirm startup' }
    foreach ($name in $fixtureHashes.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $data $name)).Hash -ne $fixtureHashes[$name]) {
            throw "Synthetic profile changed: $name"
        }
    }
    $runtime = Join-Path $data "runtime-public-$ToVersion"
    foreach ($binary in @('PlanePetClient.exe', 'PlanePetTunnel.exe', 'PlanePetUpdater.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $runtime $binary))) { throw "Missing extracted $binary" }
    }
    Write-Output "REAL_PUBLIC_UPGRADE_OK from=$FromVersion to=$ToVersion old_updater=1 public_artifact=1 health=1 profile_preserved=1"
    Write-Output "Evidence: $testRoot"
    Write-Output $trace
}
finally {
    if ($updaterProcess -and -not $updaterProcess.HasExited) { $updaterProcess.Kill() }
    # Only terminate processes whose executable resides in this unique test tree.
    $testPrefix = [IO.Path]::GetFullPath($testRoot).TrimEnd('\') + '\'
    $testProcesses = Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)
    }
    foreach ($process in $testProcesses) {
        Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
    }
}
