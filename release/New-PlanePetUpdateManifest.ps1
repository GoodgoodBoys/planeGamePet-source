param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$MinimumSupportedVersion = '1.0.0',
    [string]$Executable = '', [string]$OutputRoot = '', [string]$PrivateKeyPath = '',
    [string[]]$Summary = @('正式发布 1.0.0：配对桌宠、快捷表情与双人对战',
        '保留绑定、设置和战绩，独立正式版更新通道', '受击反馈、关于页面及文档完善'),
    [switch]$AllowUnsigned
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\Release-Preflight.ps1"
if (-not $Executable) { $Executable = Join-Path $repoRoot 'dist\PlanePetPublic.exe' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot 'release_out' }
if (-not $PrivateKeyPath) { $PrivateKeyPath = Join-Path $env:LOCALAPPDATA 'PlanePetRelease\update-signing-private.csp' }
$artifactInfo = Test-PlanePetReleaseArtifact $Executable $Version $MinimumSupportedVersion -AllowUnsigned:$AllowUnsigned
if ($Summary.Count -lt 1 -or $Summary.Count -gt 3 -or
    @($Summary | Where-Object { -not $_ -or $_.Length -gt 120 -or $_ -match '[\r\n]' }).Count) {
    throw 'Supply 1..3 nonempty summary lines, at most 120 characters each'
}
$releaseRoot = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) 'plane-pet\windows\release'
$versionRoot = Join-Path $releaseRoot "versions\$Version"
$artifact = Join-Path $versionRoot 'PlanePet.exe'
Assert-PlanePetImmutableArtifact $artifact $artifactInfo.Sha256
$immutableManifest = Join-Path $versionRoot 'release.json'
$published = [DateTimeOffset]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
if (Test-Path -LiteralPath $immutableManifest) {
    $published = (Get-Content -Raw -LiteralPath $immutableManifest | ConvertFrom-Json).published_at
}
$manifest = [ordered]@{
    schema=1; product_id='plane-pet-windows'; channel='release'; release_epoch=1;
    version=$Version; minimum_supported_version=$MinimumSupportedVersion; published_at=$published;
    size=[uint64]$artifactInfo.Size; sha256=$artifactInfo.Sha256;
    url="https://egg-ota-test.oss-cn-beijing.aliyuncs.com/plane-pet/windows/release/versions/$Version/PlanePet.exe";
    authenticode=$artifactInfo.Authenticode;
    summary_1=$Summary[0]; summary_2=$(if ($Summary.Count -ge 2) { $Summary[1] } else { '' });
    summary_3=$(if ($Summary.Count -ge 3) { $Summary[2] } else { '' })
}
$json = $manifest | ConvertTo-Json -Depth 3
$utf8 = [Text.UTF8Encoding]::new($false)
$manifestBytes = $utf8.GetBytes($json)
if ((Test-Path -LiteralPath $immutableManifest) -and (Get-Content -Raw -LiteralPath $immutableManifest) -cne $json) {
    throw 'This version already has different release metadata; issue a new version'
}
$rsa = [Security.Cryptography.RSACryptoServiceProvider]::new()
try {
    $rsa.ImportCspBlob([IO.File]::ReadAllBytes($PrivateKeyPath))
    $source = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'desktop\update_manager.cpp')
    $block = [regex]::Match($source, 'kPublicModulusBase64\[\]\s*=([\s\S]*?);').Groups[1].Value
    $expected = ([regex]::Matches($block, '"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }) -join ''
    if (-not $expected -or [Convert]::ToBase64String($rsa.ExportParameters($false).Modulus) -cne $expected) {
        throw 'Manifest signing key does not match embedded trust key'
    }
    $signature = $rsa.SignData($manifestBytes, [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    if (-not $rsa.VerifyData($manifestBytes, $signature, [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)) { throw 'Manifest signature self-check failed' }
} finally { $rsa.Dispose() }
# Nothing is written until preflight and signature checks pass.
New-Item -ItemType Directory -Path $versionRoot -Force | Out-Null
$publishLock = [IO.File]::Open((Join-Path $releaseRoot '.publish.lock'),
    [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    # Recheck inside a channel-wide lock so concurrent publishers cannot race.
    Assert-PlanePetImmutableArtifact $artifact $artifactInfo.Sha256
    if ((Test-Path -LiteralPath $immutableManifest) -and
        (Get-Content -Raw -LiteralPath $immutableManifest) -cne $json) {
        throw 'Concurrent publication changed this immutable version'
    }
    $signatureText = [Convert]::ToBase64String($signature)
    if ((Test-Path -LiteralPath "$immutableManifest.sig") -and
        (Get-Content -Raw -LiteralPath "$immutableManifest.sig").Trim() -cne $signatureText) {
        throw 'Existing immutable signature differs; do not overwrite it'
    }
    if (-not (Test-Path -LiteralPath $artifact)) {
        $inputFile = [IO.File]::OpenRead($artifactInfo.Path)
        try {
            $outputFile = [IO.File]::Open($artifact, [IO.FileMode]::CreateNew,
                [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) }
            finally { $outputFile.Dispose() }
        } finally { $inputFile.Dispose() }
    }
    Assert-PlanePetImmutableArtifact $artifact $artifactInfo.Sha256
    if (-not (Test-Path -LiteralPath $immutableManifest)) {
        [IO.File]::WriteAllBytes($immutableManifest, $manifestBytes)
    }
    if (-not (Test-Path -LiteralPath "$immutableManifest.sig")) {
        [IO.File]::WriteAllText("$immutableManifest.sig", $signatureText, [Text.Encoding]::ASCII)
    }
    [IO.File]::WriteAllText((Join-Path $releaseRoot 'latest.json.sig'), $signatureText, [Text.Encoding]::ASCII)
    [IO.File]::WriteAllBytes((Join-Path $releaseRoot 'latest.json'), $manifestBytes)
} finally {
    $publishLock.Dispose()
}
[pscustomobject]@{ Version=$Version; ReleaseEpoch=1; Channel='release'; Artifact=$artifact;
    Sha256=$artifactInfo.Sha256; Authenticode=$artifactInfo.Authenticode;
    Manifest=(Join-Path $releaseRoot 'latest.json'); OssPrefix='plane-pet/windows/release/' }
