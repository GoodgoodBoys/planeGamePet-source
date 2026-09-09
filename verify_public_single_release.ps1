param(
    [string]$Archive = "",
    [string]$ExpectedVersion = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$releaseHeader = Get-Content -Raw -LiteralPath (Join-Path $root 'common\app_version.h')
$releaseVersion = if ($releaseHeader -match 'kString\[\] = "(\d+\.\d+\.\d+)"') { $Matches[1] } else { throw 'Missing release version' }
$FormalRelease = ($releaseHeader -match 'kReleaseEpoch = 1;') -and (-not $ExpectedVersion -or $ExpectedVersion -eq $releaseVersion)
$packageFamily = if ($FormalRelease) { 'Release' } else { 'Public' }
if ([string]::IsNullOrWhiteSpace($ExpectedVersion)) {
    $taskVersionHeader = Get-Content -Raw -LiteralPath (Join-Path $root 'common\app_version.h')
    if ($taskVersionHeader -notmatch 'kString\[\] = "(\d+\.\d+\.\d+)"') { throw 'Missing app version' }
    $ExpectedVersion = $Matches[1]
}
if ([string]::IsNullOrWhiteSpace($Archive)) {
    $Archive = Join-Path $root "dist\PlanePet-$packageFamily-Single-$ExpectedVersion.zip"
}
$Archive = [IO.Path]::GetFullPath($Archive)
if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
    throw "Missing release archive: $Archive"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PlanePetReleaseResources {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(
        string fileName, IntPtr file, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadImageW(
        IntPtr instance, IntPtr name, uint type, int width, int height,
        uint flags);
    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr icon);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
}
'@
$zip = [IO.Compression.ZipFile]::OpenRead($Archive)
try {
    $entries = @($zip.Entries | Where-Object { -not $_.FullName.EndsWith('/') })
    $names = @($entries.FullName | Sort-Object)
    $expected = @("PlanePet.exe", "使用说明.txt") | Sort-Object
    if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'1.0.16') {
        $expected += 'HELP.md'
        $helpEntry = $entries | Where-Object FullName -eq 'HELP.md'
        if (-not $helpEntry) { throw 'Missing help document' }
        $helpReader = [IO.StreamReader]::new($helpEntry.Open(), [Text.Encoding]::UTF8)
        try { $helpText = $helpReader.ReadToEnd() } finally { $helpReader.Dispose() }
        if ($helpText -cne (Get-Content -Raw -LiteralPath (Join-Path $root 'HELP.md'))) {
            throw 'Packaged help differs from the approved help document'
        }
    }
    if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'1.0.3') {
        $expected += @('PRIVACY.md', 'THIRD-PARTY-NOTICES.md')
        $expected += @($names | Where-Object { $_ -match '^licenses/(gcc|mingw-w64|winpthreads)/[A-Za-z0-9._-]+$' })
        foreach ($requiredLicense in @('licenses/gcc/COPYING.RUNTIME','licenses/mingw-w64/COPYING','licenses/winpthreads/COPYING')) {
            if ($requiredLicense -notin $names) { throw "Missing license: $requiredLicense" }
        }
        $expected = $expected | Sort-Object
    }
    if (($names -join "|") -ne ($expected -join "|")) {
        throw "Unexpected archive contents: $($names -join ', ')"
    }
    $exeEntry = $entries | Where-Object FullName -eq "PlanePet.exe"
    $textEntry = $entries | Where-Object FullName -eq "使用说明.txt"
    if ($exeEntry.Length -lt 1MB -or $exeEntry.Length -gt 20MB) {
        throw "Unexpected executable size: $($exeEntry.Length)"
    }
    if ($textEntry.Length -lt 200 -or $textEntry.Length -gt 100KB) {
        throw "Unexpected instructions size: $($textEntry.Length)"
    }
}
finally {
    $zip.Dispose()
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-single-static-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive, $testRoot)
    $exe = Join-Path $testRoot "PlanePet.exe"
    $instructionsPath = Join-Path $testRoot "使用说明.txt"
    $bytes = [IO.File]::ReadAllBytes($exe)
    if ($bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Executable is missing the DOS MZ header."
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0x40 -or $peOffset + 0x60 -ge $bytes.Length -or
        [Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4) -ne "PE`0`0") {
        throw "Executable is missing a valid PE header."
    }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x8664) {
        throw ("Expected Windows x64 PE, found machine 0x{0:x4}." -f $machine)
    }
    $optionalHeader = $peOffset + 24
    if ([BitConverter]::ToUInt16($bytes, $optionalHeader) -ne 0x20B) {
        throw "Expected a PE32+ optional header."
    }
    $subsystem = [BitConverter]::ToUInt16($bytes, $optionalHeader + 0x44)
    if ($subsystem -ne 2) {
        throw "Expected a Windows GUI subsystem executable."
    }
    $module = [PlanePetReleaseResources]::LoadLibraryExW($exe, [IntPtr]::Zero, 2)
    if ($module -eq [IntPtr]::Zero) {
        throw "Could not load the packaged executable resources."
    }
    try {
        $icon = [PlanePetReleaseResources]::LoadImageW(
            $module, [IntPtr]1, 1, 256, 256, 0)
        if ($icon -eq [IntPtr]::Zero) {
            throw "The game-aircraft application icon resource is missing."
        }
        [void][PlanePetReleaseResources]::DestroyIcon($icon)
    } finally {
        [void][PlanePetReleaseResources]::FreeLibrary($module)
    }
    $unicode = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($marker in @(
            $(if ($FormalRelease) { "runtime-release-" } else { "runtime-public-" }), "PlanePetPublicLauncher",
            "PlanePetTunnel.exe", "PlanePetClient.exe")) {
        if (-not $unicode.Contains($marker)) {
            throw "Missing embedded launcher marker: $marker"
        }
    }
    if (-not $unicode.Contains($ExpectedVersion)) {
        throw "Missing embedded application version: $ExpectedVersion"
    }
    foreach ($marker in @(
            "PlanePetUpdater.exe", "update-enabled",
            "egg-ota-test.oss-cn-beijing.aliyuncs.com")) {
        if (-not $unicode.Contains($marker)) {
            throw "Missing embedded update marker: $marker"
        }
    }
    $instructions = Get-Content -Raw -LiteralPath $instructionsPath
    foreach ($required in @(
            $(if ($FormalRelease) { "单端公网正式版 v$ExpectedVersion" } else { "单端公网最终测试版 v$ExpectedVersion" }), "两位测试者",
            "匿名测试统计", "六位匹配码", "TLS", "90 天", "在线状态",
            "显示/隐藏", "显示绑定电脑框", "勿扰状态", "发送/接收", "绑定存储",
            "联网鉴权", "来源网络信息", "不可逆哈希", "退出")) {
        if (-not $instructions.Contains($required)) {
            throw "Instructions are missing required text: $required"
        }
    }
    if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'3.0.8') {
        foreach ($required in @('通过 TLS 加密连接传输用于鉴权', '而非原始令牌', '关于…', '开发者 Ding', '离线阅读隐私说明和完整第三方许可')) {
            if (-not $instructions.Contains($required)) { throw "Missing About/privacy release text: $required" }
        }
        if ($instructions.Contains('原始安装凭据不会上传')) { throw 'Obsolete credential transmission claim' }
    }
    if (-not $instructions.Contains("检查软件更新")) {
        throw "Instructions are missing the update workflow."
    }
    if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'1.0.3') {
        foreach ($label in @('设置与隐私', '帮助与更新', '匿名使用统计')) {
            if (-not $instructions.Contains($label)) { throw "Missing grouped menu documentation: $label" }
        }
    }
    $builtExe = Join-Path $root "dist\PlanePetPublic.exe"
    if (-not (Test-Path -LiteralPath $builtExe) -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $builtExe).Hash -ne
        (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash) {
        throw "Packaged executable does not match the freshly built launcher."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $exe
    $archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Archive).Hash.ToLower()
    Write-Output "PUBLIC_SINGLE_STATIC_OK"
    Write-Output "archive=$Archive"
    Write-Output "sha256=$archiveHash"
    Write-Output "pe=x64-gui icon=game-aircraft embedded=client+tunnel+updater entries=$($names.Count)"
    Write-Output "signature=$($signature.Status)"
}
finally {
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}
