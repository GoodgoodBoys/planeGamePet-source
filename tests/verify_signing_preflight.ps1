$ErrorActionPreference = 'Stop'
$signingScriptPath = Join-Path $PSScriptRoot '../release/Sign-PlanePetBinary.ps1'
$signingTokens = $null
$signingParseErrors = $null
$signingAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $signingScriptPath, [ref]$signingTokens, [ref]$signingParseErrors)
if ($signingParseErrors.Count -ne 0) { throw 'Signing script contains syntax errors' }

# Run the production certificate metadata guard with provider-shaped fixtures.
# No certificate/key is generated, imported, trusted, or used to sign a file.
$guard = $signingAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text.Contains('$certificate.HasPrivateKey')
}, $true)
if ($null -eq $guard) { throw 'Missing production certificate guard' }
$rejectCertificate = [scriptblock]::Create($guard.Clauses[0].Item1.Extent.Text)
$signingTestNow = Get-Date
$signingCases = @(
    @{Name='valid_code_signing_string_oid'; Private=$true; Before=-1; After=1; Oids=@('1.3.6.1.5.5.7.3.3'); Reject=$false},
    @{Name='multiple_eku_including_code_signing'; Private=$true; Before=-1; After=1; Oids=@('1.3.6.1.5.5.7.3.1','1.3.6.1.5.5.7.3.3'); Reject=$false},
    @{Name='missing_private_key'; Private=$false; Before=-1; After=1; Oids=@('1.3.6.1.5.5.7.3.3'); Reject=$true},
    @{Name='expired'; Private=$true; Before=-2; After=-1; Oids=@('1.3.6.1.5.5.7.3.3'); Reject=$true},
    @{Name='not_yet_valid'; Private=$true; Before=1; After=2; Oids=@('1.3.6.1.5.5.7.3.3'); Reject=$true},
    @{Name='tls_certificate_rejected'; Private=$true; Before=-1; After=1; Oids=@('1.3.6.1.5.5.7.3.1'); Reject=$true},
    @{Name='no_explicit_code_signing_usage'; Private=$true; Before=-1; After=1; Oids=@(); Reject=$true}
)
foreach ($signingCase in $signingCases) {
    # PowerShell's Certificate provider exposes ObjectId as a string, not Oid.
    $certificate = [pscustomobject]@{
        HasPrivateKey=$signingCase.Private
        NotBefore=$signingTestNow.AddDays($signingCase.Before)
        NotAfter=$signingTestNow.AddDays($signingCase.After)
        EnhancedKeyUsageList=@($signingCase.Oids | ForEach-Object {
            [pscustomobject]@{ObjectId=$_}
        })
    }
    $rejected = & $rejectCertificate
    if ([bool]$rejected -ne $signingCase.Reject) { throw "Certificate guard failed: $($signingCase.Name)" }
    "SIGNING_PREFLIGHT_PASS $($signingCase.Name)"
}
'SIGNING_PREFLIGHT_OK metadata_only=1 real_signature_not_tested=1 certificate_store_unchanged=1'
