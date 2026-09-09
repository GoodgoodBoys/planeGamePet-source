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
    $Archive = Join-Path $root "dist\PlanePet-$packageFamily-DualLocal-$ExpectedVersion.zip"
}
$Archive = [IO.Path]::GetFullPath($Archive)
if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
    throw "Missing dual release archive: $Archive"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-dual-static-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $names = @($zip.Entries | Where-Object {
            -not $_.FullName.EndsWith('/')
        } | ForEach-Object FullName | Sort-Object)
        $expected = @("PlanePet.exe", "使用说明.txt") | Sort-Object
        if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'1.0.16') {
            $expected += 'HELP.md'
            $helpEntry = $zip.Entries | Where-Object FullName -eq 'HELP.md'
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
            throw "Unexpected dual archive contents: $($names -join ', ')"
        }
    } finally {
        $zip.Dispose()
    }
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive, $testRoot)
    $exe = Join-Path $testRoot "PlanePet.exe"
    $bytes = [IO.File]::ReadAllBytes($exe)
    if ($bytes.Length -lt 1MB -or $bytes[0] -ne 0x4D -or
        $bytes[1] -ne 0x5A) {
        throw "Dual launcher is not a valid-sized MZ executable."
    }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([Text.Encoding]::ASCII.GetString($bytes, $pe, 4) -ne "PE`0`0" -or
        [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664 -or
        [BitConverter]::ToUInt16($bytes, $pe + 24 + 0x44) -ne 2) {
        throw "Dual launcher is not a Windows x64 GUI PE."
    }
    $unicode = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($marker in @(
            $(if ($FormalRelease) { "runtime-release-dual-" } else { "runtime-public-dual-" }), "PlanePetPublicDualLauncher",
            "PlanePetTunnel.exe", "PlanePetClient.exe")) {
        if (-not $unicode.Contains($marker)) {
            throw "Missing embedded dual-launcher marker: $marker"
        }
    }
    if (-not $unicode.Contains($ExpectedVersion)) {
        throw "Missing embedded application version: $ExpectedVersion"
    }
    $instructions = Get-Content -Raw -LiteralPath `
        (Join-Path $testRoot "使用说明.txt")
    foreach ($required in @(
            "单机双端公网测试版 v$ExpectedVersion", "TLS", "四个表情",
            "在线状态", "显示/隐藏", "显示绑定电脑框", "勿扰状态", "发送/接收",
            "绑定存储", "联网鉴权", "来源网络信息",
            "不可逆哈希", "退出")) {
        if (-not $instructions.Contains($required)) {
            throw "Dual instructions are missing required text: $required"
        }
    }
    if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'3.0.8') {
        foreach ($required in @('通过 TLS 加密连接传输用于鉴权', '而非原始令牌', '关于…', '开发者 Ding', '离线阅读隐私说明和完整第三方许可')) {
            if (-not $instructions.Contains($required)) { throw "Missing About/privacy dual text: $required" }
        }
        if ($instructions.Contains('原始安装凭据不会上传')) { throw 'Obsolete credential transmission claim' }
    }
    $built = Join-Path $root "dist\PlanePetPublicDual.exe"
    if ($FormalRelease -or [version]$ExpectedVersion -ge [version]'1.0.3') {
        foreach ($label in @('设置与隐私', '帮助与更新', '匿名使用统计', '检查更新仍置灰')) {
            if (-not $instructions.Contains($label)) { throw "Missing grouped dual menu documentation: $label" }
        }
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $built).Hash -ne
        (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash) {
        throw "Packaged dual launcher does not match the fresh build."
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Archive).Hash.ToLower()
    Write-Output "PUBLIC_DUAL_STATIC_OK"
    Write-Output "archive=$Archive"
    Write-Output "sha256=$hash"
    Write-Output "pe=x64-gui embedded=two-clients+two-tunnels entries=$($names.Count)"
}
finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($temporaryRoot,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
