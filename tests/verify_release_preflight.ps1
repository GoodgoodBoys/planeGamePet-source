$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$repo\release\Release-Preflight.ps1"
$exe = Join-Path $repo 'dist\PlanePetPublic.exe'
$header = Get-Content -Raw -LiteralPath (Join-Path $repo 'common\app_version.h')
if ($header -notmatch 'kString\[\] = "(\d+\.\d+\.\d+)"') { throw 'Missing current version' }
$version = $Matches[1]
function Expect-Rejected([string]$Name, [scriptblock]$Check) {
    $rejected = $false
    try { & $Check | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Expected rejection: $Name" }
    "RELEASE_GATE_PASS $Name"
}
foreach ($invalid in @('01.0.0','1.0','256.0.0','1.2.3.4','-1.0.0','1.0.0/../../')) {
    Expect-Rejected "invalid_version_$invalid" { ConvertTo-PlanePetVersion $invalid }
}
Expect-Rejected 'version_mismatch' { Test-PlanePetReleaseArtifact $exe '9.0.0' $version -AllowUnsigned }
Expect-Rejected 'minimum_above_target' { Test-PlanePetReleaseArtifact $exe $version '255.255.255' -AllowUnsigned }
Expect-Rejected 'unsigned_requires_explicit_consent' { Test-PlanePetReleaseArtifact $exe $version $version }
Expect-Rejected 'dual_must_not_enter_single_channel' {
    Test-PlanePetReleaseArtifact "$repo\dist\PlanePetPublicDual.exe" $version $version -AllowUnsigned
}
Expect-Rejected 'same_version_different_bytes' { Assert-PlanePetImmutableArtifact $exe ('0' * 64) }
$valid = Test-PlanePetReleaseArtifact $exe $version $version -AllowUnsigned
Assert-PlanePetImmutableArtifact $exe $valid.Sha256
'RELEASE_PREFLIGHT_OK'
