param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$MinimumSupportedVersion = "1.0.0",

    [string]$Executable = "",
    [string]$OutputRoot = "",
    [string]$PrivateKeyPath = "",
    [string[]]$Summary = @(
        "新增安全的软件更新功能",
        "增加双方版本兼容检查",
        "保留旧版并支持启动失败自动回滚"
    )
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $repoRoot "dist\PlanePetPublic.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "release_out"
}
if ([string]::IsNullOrWhiteSpace($PrivateKeyPath)) {
    $PrivateKeyPath = Join-Path $env:LOCALAPPDATA `
        "PlanePetRelease\update-signing-private.csp"
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Release executable not found: $Executable"
}
if (-not (Test-Path -LiteralPath $PrivateKeyPath -PathType Leaf)) {
    throw "Signing key not found: $PrivateKeyPath"
}
if ($Summary.Count -gt 3) {
    throw "At most three summary lines are supported."
}

$stableRoot = Join-Path $OutputRoot "plane-pet\windows\stable"
$versionRoot = Join-Path $stableRoot "versions\$Version"
New-Item -ItemType Directory -Force -Path $versionRoot | Out-Null
$artifact = Join-Path $versionRoot "PlanePet.exe"
Copy-Item -LiteralPath $Executable -Destination $artifact -Force
$artifactInfo = Get-Item -LiteralPath $artifact
$artifactHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLower()
$artifactUrl = "https://egg-ota-test.oss-cn-beijing.aliyuncs.com/" +
    "plane-pet/windows/stable/versions/$Version/PlanePet.exe"

$manifest = [ordered]@{
    schema = 1
    product_id = "plane-pet-windows"
    channel = "stable"
    version = $Version
    minimum_supported_version = $MinimumSupportedVersion
    published_at = [DateTimeOffset]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    size = [uint64]$artifactInfo.Length
    sha256 = $artifactHash
    url = $artifactUrl
    summary_1 = if ($Summary.Count -ge 1) { $Summary[0] } else { "" }
    summary_2 = if ($Summary.Count -ge 2) { $Summary[1] } else { "" }
    summary_3 = if ($Summary.Count -ge 3) { $Summary[2] } else { "" }
}
$json = $manifest | ConvertTo-Json -Depth 3
$utf8 = New-Object System.Text.UTF8Encoding($false)
$manifestPath = Join-Path $stableRoot "latest.json"
[IO.File]::WriteAllText($manifestPath, $json, $utf8)
$manifestBytes = [IO.File]::ReadAllBytes($manifestPath)

$rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
$rsa.ImportCspBlob([IO.File]::ReadAllBytes($PrivateKeyPath))
$signature = $rsa.SignData(
    $manifestBytes,
    [System.Security.Cryptography.HashAlgorithmName]::SHA256,
    [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
$signaturePath = "$manifestPath.sig"
[IO.File]::WriteAllText(
    $signaturePath, [Convert]::ToBase64String($signature),
    [Text.Encoding]::ASCII)

[PSCustomObject]@{
    Version = $Version
    MinimumSupportedVersion = $MinimumSupportedVersion
    Artifact = $artifact
    ArtifactSize = $artifactInfo.Length
    Sha256 = $artifactHash
    Manifest = $manifestPath
    Signature = $signaturePath
    OssPrefix = "plane-pet/windows/stable/"
}
