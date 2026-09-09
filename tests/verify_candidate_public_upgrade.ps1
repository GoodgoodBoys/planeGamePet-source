param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$FromVersion = '1.0.2',
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$ToVersion = '1.0.3',
    [ValidateRange(0, 300)][int]$InspectionSeconds = 0
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$originalData = Join-Path $env:LOCALAPPDATA 'PlanePet'
$credential = Join-Path $originalData 'tunnel.credential'
$oldProgram = Join-Path $repo "dist\PlanePet-Public-Single-$FromVersion\PlanePet.exe"
$candidate = Join-Path $repo "dist\PlanePet-Public-Single-$ToVersion\PlanePet.exe"
foreach ($path in @($oldProgram, $candidate, $credential)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing test input: $path" }
}
foreach ($mutexName in @('PlanePetPublicLauncher', 'PlanePetUpdateTransaction')) {
    $existing = $null
    if ([Threading.Mutex]::TryOpenExisting($mutexName, [ref]$existing)) {
        $existing.Dispose()
        throw "Existing application/update must finish first: $mutexName"
    }
}

# Read embedded resources as data, never load executable code into the test host.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PlanePetCandidateResource {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
 [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr resource);
 [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
$testRoot = Join-Path $repo ('dist\candidate-upgrade-' + [guid]::NewGuid().ToString('N'))
$appData = Join-Path $testRoot '中文 用户数据'
$data = Join-Path $appData 'PlanePet'
$package = Join-Path $data "updates\$ToVersion\PlanePet.exe.download"
$target = Join-Path $testRoot 'PlanePet.exe'
$oldUpdater = Join-Path $testRoot "OriginalUpdater-$FromVersion.exe"
New-Item -ItemType Directory -Path (Split-Path -Parent $package) | Out-Null
Copy-Item -LiteralPath $oldProgram -Destination $target
Copy-Item -LiteralPath $candidate -Destination $package
$expectedHash = (Get-FileHash -LiteralPath $candidate).Hash
$module = [PlanePetCandidateResource]::LoadLibraryExW($oldProgram, [IntPtr]::Zero, 2)
if ($module -eq [IntPtr]::Zero) { throw 'Cannot inspect original release resources' }
try {
    $resource = [PlanePetCandidateResource]::FindResourceW($module, [IntPtr]203, [IntPtr]10)
    $length = [PlanePetCandidateResource]::SizeofResource($module, $resource)
    $address = [PlanePetCandidateResource]::LockResource([PlanePetCandidateResource]::LoadResource($module, $resource))
    if ($address -eq [IntPtr]::Zero -or $length -lt 1MB -or $length -gt 20MB) { throw 'Invalid original updater resource' }
    $bytes = [byte[]]::new($length)
    [Runtime.InteropServices.Marshal]::Copy($address, $bytes, 0, $length)
    [IO.File]::WriteAllBytes($oldUpdater, $bytes)
} finally { [void][PlanePetCandidateResource]::FreeLibrary($module) }

# Synthetic, unbound identity/history; the only copied secret is an existing
# encrypted tunnel credential, removed in finally. No original profile edits.
$fixtures = @{
    'public.binding' = "PLANE_PET_CLIENT 1 1729102102 0 0 0 0 1`r`n"
    'public.settings' = "PLANE_PET_SETTINGS 3 0 0 1 0 0`r`n"
    'public.history' = "PLANE_PET_HISTORY 2 3 1 1 1 0`r`n1720000000000 1 1 0`r`n1710000000000 0 1 -1`r`n1700000000000 1 0 1`r`n"
}
$originalHashes = @{}
foreach ($name in @('public.binding','public.settings','public.history','tunnel.credential')) {
    $path = Join-Path $originalData $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { $originalHashes[$name] = (Get-FileHash -LiteralPath $path).Hash }
}
$fixtureHashes = @{}
foreach ($name in $fixtures.Keys) {
    $path = Join-Path $data $name
    [IO.File]::WriteAllText($path, $fixtures[$name], [Text.UTF8Encoding]::new($false))
    $fixtureHashes[$name] = (Get-FileHash -LiteralPath $path).Hash
}
$socket = [Net.Sockets.UdpClient]::new(0)
$testPort = $socket.Client.LocalEndPoint.Port
$socket.Dispose()
$credentialCopy = Join-Path $data 'tunnel.credential'
$updaterProcess = $null
$parent = $null
try {
    Copy-Item -LiteralPath $credential -Destination $credentialCopy
    $parent = Start-Process -FilePath (Join-Path $repo 'dist\PlanePetUpdateHealthStub.exe') -WindowStyle Hidden -PassThru
    $info = [Diagnostics.ProcessStartInfo]::new($oldUpdater)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.Environment['LOCALAPPDATA'] = $appData
    $info.Environment['PLANE_PET_LOCAL_PORT'] = "$testPort"
    # Match 1.0.2's own launcher: its legacy parser expects quotes after '='.
    # ArgumentList quotes the entire option and is not compatible with it.
    $info.Arguments = "--parent-pid=$($parent.Id) --target=`"$target`" --package=`"$package`" --sha256=$($expectedHash.ToLowerInvariant()) --version=$ToVersion --data=`"$data`" --trace=1"
    $updaterProcess = [Diagnostics.Process]::Start($info)
    if (-not $updaterProcess.WaitForExit(55000)) { throw 'Original updater timed out' }
    if ($updaterProcess.ExitCode -ne 0) { throw "Original updater failed: $($updaterProcess.ExitCode); evidence=$testRoot" }
    if ((Get-FileHash -LiteralPath $target).Hash -ne $expectedHash -or
        (Test-Path -LiteralPath "$target.old") -or
        (Test-Path -LiteralPath (Join-Path $data 'update.pending')) -or
        (Test-Path -LiteralPath (Join-Path $data 'update.failure'))) { throw 'Real candidate installation did not commit' }
    $trace = Get-Content -Raw -LiteralPath (Join-Path $data 'update.trace')
    if ($trace -notmatch 'health_confirmed') { throw 'Real launcher did not acknowledge initialized client' }
    $status = ''
    for ($attempt = 0; $attempt -lt 40; ++$attempt) {
        $statusPath = Join-Path $data 'tunnel.status'
        if (Test-Path -LiteralPath $statusPath) { $status = Get-Content -Raw -LiteralPath $statusPath }
        if ($status -match '^PPNET1 [0-9]+ 2 0') { break }
        Start-Sleep -Milliseconds 500
    }
    if ($status -notmatch '^PPNET1 [0-9]+ 2 0') { throw "Candidate failed public TLS connection: $status" }
    foreach ($name in $fixtureHashes.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $data $name)).Hash -ne $fixtureHashes[$name]) { throw "Synthetic profile changed: $name" }
    }
    foreach ($name in @('PlanePetClient.exe','PlanePetTunnel.exe','PlanePetUpdater.exe','PRIVACY.txt','THIRD-PARTY-NOTICES.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $data "runtime-public-$ToVersion\$name"))) { throw "Missing candidate component: $name" }
    }
    if (Test-Path -LiteralPath (Join-Path $data 'public.events.csv')) { throw 'Telemetry-disabled test created events' }
    "REAL_CANDIDATE_UPGRADE_OK from=$FromVersion to=$ToVersion original_embedded_updater=1 real_launcher=1 real_client=1 health=1 tls=1 synthetic_profile_preserved=1"
    "TEST_SCOPE=$testRoot"
    $trace
    for ($second = 0; $second -lt $InspectionSeconds; ++$second) { Start-Sleep -Seconds 1 }
} finally {
    if ($updaterProcess -and -not $updaterProcess.HasExited) { $updaterProcess.Kill(); $updaterProcess.WaitForExit(5000) | Out-Null }
    if ($parent -and -not $parent.HasExited) { $parent.Kill() }
    $prefix = [IO.Path]::GetFullPath($testRoot).TrimEnd('\') + '\'
    foreach ($process in @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
    })) { Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue }
    $resolvedCopy = [IO.Path]::GetFullPath($credentialCopy)
    if (-not $resolvedCopy.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Credential cleanup escaped isolated tree' }
    Remove-Item -LiteralPath $resolvedCopy -Force -ErrorAction SilentlyContinue
    foreach ($name in $originalHashes.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $originalData $name)).Hash -ne $originalHashes[$name]) { throw "Original user data changed unexpectedly: $name" }
    }
    'ORIGINAL_PROFILE_UNCHANGED_TEMP_CREDENTIAL_REMOVED'
}
