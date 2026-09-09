param(
    [string]$Dist = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($Dist)) {
    $Dist = Join-Path $repoRoot "dist"
}
$updater = Join-Path $Dist "PlanePetUpdater.exe"
$stub = Join-Path $Dist "PlanePetUpdateHealthStub.exe"
$unhealthyStub = Join-Path $Dist "PlanePetUpdateUnhealthyStub.exe"
if (-not (Test-Path -LiteralPath $updater -PathType Leaf) -or
    -not (Test-Path -LiteralPath $stub -PathType Leaf) -or
    -not (Test-Path -LiteralPath $unhealthyStub -PathType Leaf)) {
    throw "Updater test binaries are missing. Run build.ps1 -BuildPublic first."
}

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = Join-Path $tempRoot ("plane-pet-updater-test-" + [Guid]::NewGuid())
$data = Join-Path $testRoot "data"
$updates = Join-Path $data "updates\9.9.9"
$target = Join-Path $testRoot "PlanePet.exe"
$package = Join-Path $updates "PlanePet.exe.download"
New-Item -ItemType Directory -Force -Path $updates | Out-Null

function Invoke-Updater([string]$ExpectedHash, [bool]$Recover = $false) {
    $shortLived = Start-Process -FilePath $env:ComSpec `
        -ArgumentList "/c ping 127.0.0.1 -n 2 >nul" -PassThru `
        -WindowStyle Hidden
    $arguments = @(
        "--parent-pid=$($shortLived.Id)",
        "--target=`"$target`"",
        "--package=`"$package`"",
        "--sha256=$ExpectedHash",
        "--version=9.9.9",
        "--data=`"$data`"",
        "--trace=1"
    )
    if ($Recover) { $arguments += "--recover=1" }
    $process = Start-Process -FilePath $updater -ArgumentList $arguments `
        -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(90000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $tracePath = Join-Path $data "update.trace"
        $trace = if (Test-Path -LiteralPath $tracePath) {
            Get-Content -Raw -LiteralPath $tracePath
        } else { "no trace" }
        throw "Updater test timed out: $trace"
    }
    $shortLived.WaitForExit()
    return $process.ExitCode
}

try {
    Copy-Item -LiteralPath $stub -Destination $target -Force
    Copy-Item -LiteralPath $stub -Destination $package -Force
    $successHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $package).Hash.ToLower()
    $successCode = Invoke-Updater $successHash
    if ($successCode -ne 0 -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLower() -ne $successHash -or
        (Test-Path -LiteralPath "$target.old") -or
        -not (Test-Path -LiteralPath "$target.last-good") -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath "$target.last-good").Hash.ToLower() -ne $successHash -or
        (Test-Path -LiteralPath (Join-Path $data "update.pending"))) {
        $failurePath = Join-Path $data "update.failure"
        $failureText = if (Test-Path -LiteralPath $failurePath) {
            Get-Content -Raw -Encoding UTF8 -LiteralPath $failurePath
        } else { "no failure record" }
        throw "Successful install path failed (exit $successCode): $failureText"
    }
    Start-Sleep -Milliseconds 900

    Copy-Item -LiteralPath $stub -Destination $target -Force
    $originalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLower()
    Copy-Item -LiteralPath $unhealthyStub -Destination $package -Force
    $badHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $package).Hash.ToLower()
    $rollbackCode = Invoke-Updater $badHash
    if ($rollbackCode -ne 8 -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLower() -ne $originalHash -or
        -not (Test-Path -LiteralPath (Join-Path $data "update.failure"))) {
        throw "Rollback path failed (exit $rollbackCode)."
    }
    Write-Host "PLANE_PET_UPDATER_INSTALL_OK"
    Write-Host "PLANE_PET_UPDATER_ROLLBACK_OK"
    Start-Sleep -Milliseconds 250
    $restartMarker = Join-Path $data "old-version-restarted"
    if (-not (Test-Path -LiteralPath $restartMarker)) { throw "Old version was not restarted" }
    Remove-Item -LiteralPath $restartMarker -Force
    $hashFailure = Invoke-Updater ('0' * 64)
    Start-Sleep -Milliseconds 250
    if ($hashFailure -ne 3 -or -not (Test-Path -LiteralPath $restartMarker) -or
        (Get-FileHash -LiteralPath $target).Hash.ToLower() -ne $originalHash) {
        throw "Hash rejection did not restart the untouched old version"
    }
    Write-Host "PLANE_PET_UPDATER_BAD_HASH_RESTART_OK"
    # Deny just the pending journal write, without changing OS permissions.
    $blockedJournal = Join-Path $data "update.pending.tmp"
    New-Item -ItemType Directory -Path $blockedJournal | Out-Null
    $journalFailure = Invoke-Updater $badHash
    Remove-Item -LiteralPath $blockedJournal -Force
    if ($journalFailure -ne 6 -or
        (Get-FileHash -LiteralPath $target).Hash.ToLower() -ne $originalHash) {
        throw "Journal failure changed the installed binary"
    }
    Write-Host "PLANE_PET_UPDATER_JOURNAL_FAILURE_OK"
    Start-Sleep -Milliseconds 250
    # Simulate a power loss after replacement, before the health marker.
    Copy-Item -LiteralPath $target -Destination "$target.old"
    Copy-Item -LiteralPath $unhealthyStub -Destination $target -Force
    [IO.File]::WriteAllText((Join-Path $data "update.pending"),
        "1234-5678`n$target.old`n", [Text.UTF8Encoding]::new($false))
    Remove-Item -LiteralPath (Join-Path $data "update.health") -Force -ErrorAction SilentlyContinue
    $recovery = Invoke-Updater $badHash $true
    if ($recovery -ne 0 -or
        (Get-FileHash -LiteralPath $target).Hash.ToLower() -ne $originalHash -or
        (Test-Path -LiteralPath "$target.old") -or
        (Test-Path -LiteralPath (Join-Path $data "update.pending"))) {
        throw "Interrupted install did not recover"
    }
    Write-Host "PLANE_PET_UPDATER_INTERRUPTED_RECOVERY_OK"
    Start-Sleep -Milliseconds 250
}
finally {
    $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
    if ($resolvedRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -like "*plane-pet-updater-test-*") {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
