param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$TimestampUrl,
    [string]$SignTool = 'signtool.exe'
)
$ErrorActionPreference = 'Stop'
if ($TimestampUrl -notmatch '^https?://[^\s]+$') { throw 'A trusted RFC3161 timestamp service URL is required' }
$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or
    $certificate.NotBefore -gt (Get-Date) -or
    -not ($certificate.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3')) {
    throw 'A currently valid code-signing certificate with its private key is required'
}
$tool = (Get-Command $SignTool -ErrorAction Stop).Source
& $tool sign /sha1 $CertificateThumbprint /s My /fd SHA256 /tr $TimestampUrl /td SHA256 $Executable
if ($LASTEXITCODE -ne 0) { throw "Signing failed: $Executable" }
& $tool verify /pa /all $Executable
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $Executable" }
$signature = Get-AuthenticodeSignature -LiteralPath $Executable
if ($signature.Status -ne 'Valid' -or $null -eq $signature.TimeStamperCertificate) {
    throw "A valid timestamped signature is required: $Executable"
}
