param([string]$Compiler = 'C:\Installation\Lib\mingw64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskDist = Join-Path $taskRoot 'dist'
$taskLicenseRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $Compiler)) 'licenses'
$taskLegal = @((Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $taskRoot 'THIRD-PARTY-NOTICES.md')))
foreach ($taskComponent in @('gcc', 'mingw-w64', 'winpthreads')) {
    $taskFolder = Join-Path $taskLicenseRoot $taskComponent
    if (-not (Test-Path -LiteralPath $taskFolder -PathType Container)) {
        throw "Missing toolchain license texts: $taskFolder"
    }
    $taskFiles = @(Get-ChildItem -LiteralPath $taskFolder -File | Sort-Object Name)
    if (-not $taskFiles.Count) { throw "Empty toolchain license folder: $taskFolder" }
    foreach ($taskFile in $taskFiles) {
        $taskLegal += "`n=== $taskComponent/$($taskFile.Name) ===`n"
        $taskLegal += Get-Content -Raw -Encoding utf8 -LiteralPath $taskFile.FullName
    }
}
New-Item -ItemType Directory -Force -Path $taskDist | Out-Null
# Generated build output, not a manually maintained second copy of the licenses.
Set-Content -LiteralPath (Join-Path $taskDist 'PlanePetRuntimeNotices.txt') -Encoding utf8 -Value $taskLegal
