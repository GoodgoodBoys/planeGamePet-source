param([string]$Version = '')
$ErrorActionPreference = 'Stop'
if ($Version -and $Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version' }
$repo = Split-Path -Parent $PSScriptRoot
$releaseHeader = Get-Content -Raw -LiteralPath (Join-Path $repo 'common\app_version.h')
$releaseVersion = if ($releaseHeader -match 'kString\[\] = "(\d+\.\d+\.\d+)"') { $Matches[1] } else { throw 'Missing release version' }
if (-not $Version) { $Version = $releaseVersion }
$FormalRelease = ($releaseHeader -match 'kReleaseEpoch = 1;') -and (-not $Version -or $Version -eq $releaseVersion)
$packageFamily = if ($FormalRelease) { 'Release' } else { 'Public' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PlanePetEmbeddedRead {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
 [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr resource);
 [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr module, IntPtr resource);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
function Assert-PlanePetEmbeddedFile([IntPtr]$Module, [int]$Id, [string]$Source, [string]$Label) {
    $resource = [PlanePetEmbeddedRead]::FindResourceW($Module, [IntPtr]$Id, [IntPtr]10)
    $length = [PlanePetEmbeddedRead]::SizeofResource($Module, $resource)
    $pointer = [PlanePetEmbeddedRead]::LockResource([PlanePetEmbeddedRead]::LoadResource($Module, $resource))
    if (-not $length -or $pointer -eq [IntPtr]::Zero) { throw "Missing embedded document: $Label" }
    $bytes = [byte[]]::new($length)
    [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $length)
    $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
    if ($actual -ne (Get-FileHash -LiteralPath $Source).Hash) { throw "Stale embedded document: $Label" }
    "EMBEDDED_ABOUT_DOCUMENT_OK target=$Label bytes=$length"
}
foreach ($kind in @('Single', 'DualLocal')) {
    $exe = Join-Path $repo "dist\PlanePet-$packageFamily-$kind-$Version\PlanePet.exe"
    # DATAFILE | IMAGE_RESOURCE: inspect bytes, never execute the launcher.
    $module = [PlanePetEmbeddedRead]::LoadLibraryExW($exe, [IntPtr]::Zero, 0x22)
    if ($module -eq [IntPtr]::Zero) { throw "Cannot read launcher: $kind" }
    try {
        if ($FormalRelease -or [version]$Version -ge [version]'3.0.8') {
            Assert-PlanePetEmbeddedFile $module 204 (Join-Path $repo 'dist\PlanePetRuntimeNotices.txt') "$kind/licenses"
            Assert-PlanePetEmbeddedFile $module 205 (Join-Path $repo 'PRIVACY.md') "$kind/privacy"
        }
        $components = @{201='PlanePetTunnel.exe'; 202='PlanePetClient.exe'}
        if ($kind -eq 'Single') { $components[203] = 'PlanePetUpdater.exe' }
        foreach ($id in ($components.Keys | Sort-Object)) {
            $resource = [PlanePetEmbeddedRead]::FindResourceW($module, [IntPtr]$id, [IntPtr]10)
            $length = [PlanePetEmbeddedRead]::SizeofResource($module, $resource)
            $loaded = [PlanePetEmbeddedRead]::LoadResource($module, $resource)
            $pointer = [PlanePetEmbeddedRead]::LockResource($loaded)
            if ($length -eq 0 -or $pointer -eq [IntPtr]::Zero) { throw 'Missing embedded component' }
            $bytes = [byte[]]::new($length)
            [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $length)
            $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
            $expected = (Get-FileHash -LiteralPath (Join-Path $repo ('dist\' + $components[$id]))).Hash
            if ($actual -ne $expected) { throw "Stale embedded component: $kind $($components[$id])" }
            "EMBEDDED_COMPONENT_OK package=$kind component=$($components[$id]) sha256=$($actual.ToLower())"
        }
    } finally { [void][PlanePetEmbeddedRead]::FreeLibrary($module) }
}

if ($FormalRelease -or [version]$Version -ge [version]'3.0.8') {
    $client = Join-Path $repo 'dist\PlanePetClient.exe'
    $module = [PlanePetEmbeddedRead]::LoadLibraryExW($client, [IntPtr]::Zero, 0x22)
    if ($module -eq [IntPtr]::Zero) { throw 'Cannot inspect About reader resources' }
    try {
        Assert-PlanePetEmbeddedFile $module 191 (Join-Path $repo 'PRIVACY.md') 'client/privacy'
        Assert-PlanePetEmbeddedFile $module 192 (Join-Path $repo 'dist\PlanePetRuntimeNotices.txt') 'client/licenses'
    } finally { [void][PlanePetEmbeddedRead]::FreeLibrary($module) }
}

if ($FormalRelease -or [version]$Version -ge [version]'1.0.8') {
    $atlas = Join-Path $repo 'assets\hearts\history-classic-a.png'
    $selected = Join-Path $repo 'assets\heart-concepts\2026-09-06\A-classic.png'
    $expected = (Get-FileHash -LiteralPath $atlas).Hash
    if ($expected -ne (Get-FileHash -LiteralPath $selected).Hash) {
        throw 'History atlas differs from the user-selected first group'
    }
    $module = [PlanePetEmbeddedRead]::LoadLibraryExW((Join-Path $repo 'dist\PlanePetClient.exe'), [IntPtr]::Zero, 0x22)
    if ($module -eq [IntPtr]::Zero) { throw 'Cannot read client heart resource' }
    try {
        $resource = [PlanePetEmbeddedRead]::FindResourceW($module, [IntPtr]161, [IntPtr]10)
        $length = [PlanePetEmbeddedRead]::SizeofResource($module, $resource)
        $loaded = [PlanePetEmbeddedRead]::LoadResource($module, $resource)
        $pointer = [PlanePetEmbeddedRead]::LockResource($loaded)
        if ($length -eq 0 -or $pointer -eq [IntPtr]::Zero) { throw 'Missing embedded history atlas' }
        $bytes = [byte[]]::new($length)
        [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $length)
        $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
        if ($actual -ne $expected) { throw 'Stale embedded history atlas' }
        "EMBEDDED_HISTORY_HEART_A_OK sha256=$($actual.ToLower())"
    } finally { [void][PlanePetEmbeddedRead]::FreeLibrary($module) }
}

if ($FormalRelease -or [version]$Version -ge [version]'1.0.12') {
    $asset = Join-Path $repo 'assets\challenge\fist-c.png'
    $selected = Join-Path $repo 'assets\challenge-concepts\c-attack-v1\c-fist-master.png'
    $expected = (Get-FileHash -LiteralPath $asset).Hash
    if ($expected -ne (Get-FileHash -LiteralPath $selected).Hash) {
        throw 'Challenge fist differs from the user-selected static C master'
    }
    $module = [PlanePetEmbeddedRead]::LoadLibraryExW((Join-Path $repo 'dist\PlanePetClient.exe'), [IntPtr]::Zero, 0x22)
    if ($module -eq [IntPtr]::Zero) { throw 'Cannot read client challenge resource' }
    try {
        $resource = [PlanePetEmbeddedRead]::FindResourceW($module, [IntPtr]162, [IntPtr]10)
        $length = [PlanePetEmbeddedRead]::SizeofResource($module, $resource)
        $loaded = [PlanePetEmbeddedRead]::LoadResource($module, $resource)
        $pointer = [PlanePetEmbeddedRead]::LockResource($loaded)
        if ($length -eq 0 -or $pointer -eq [IntPtr]::Zero) { throw 'Missing embedded C fist' }
        $bytes = [byte[]]::new($length)
        [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $length)
        $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
        if ($actual -ne $expected) { throw 'Stale embedded C fist' }
        "EMBEDDED_STATIC_FIST_C_OK sha256=$($actual.ToLower())"
    } finally { [void][PlanePetEmbeddedRead]::FreeLibrary($module) }
}

if ($FormalRelease -or [version]$Version -ge [version]'3.0.5') {
    $feedback = @{
        174='hit-word-c.png'; 175='plane-blue-hp2.png'; 176='plane-blue-hp1.png'
        177='plane-red-hp2.png'; 178='plane-red-hp1.png'
    }
    if ($FormalRelease -or [version]$Version -ge [version]'3.0.7') {
        $feedback[179]='win-w1-120.png'; $feedback[180]='lost-l1.png'
    }
    $module = [PlanePetEmbeddedRead]::LoadLibraryExW((Join-Path $repo 'dist\PlanePetClient.exe'), [IntPtr]::Zero, 0x22)
    if ($module -eq [IntPtr]::Zero) { throw 'Cannot read client combat feedback resources' }
    try {
        foreach ($id in ($feedback.Keys | Sort-Object)) {
            $resource = [PlanePetEmbeddedRead]::FindResourceW($module, [IntPtr]$id, [IntPtr]10)
            $length = [PlanePetEmbeddedRead]::SizeofResource($module, $resource)
            $loaded = [PlanePetEmbeddedRead]::LoadResource($module, $resource)
            $pointer = [PlanePetEmbeddedRead]::LockResource($loaded)
            if ($length -eq 0 -or $pointer -eq [IntPtr]::Zero) { throw "Missing combat feedback resource: $id" }
            $bytes = [byte[]]::new($length)
            [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $length)
            $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
            $expected = (Get-FileHash -LiteralPath (Join-Path $repo ('assets\effects\' + $feedback[$id]))).Hash
            if ($actual -ne $expected) { throw "Stale combat feedback resource: $id" }
            "EMBEDDED_COMBAT_FEEDBACK_OK resource=$id asset=$($feedback[$id]) sha256=$($actual.ToLower())"
        }
    } finally { [void][PlanePetEmbeddedRead]::FreeLibrary($module) }
}
