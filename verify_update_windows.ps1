param(
    [string]$Compiler = "C:\Installation\Lib\mingw64\bin\g++.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$resource = Join-Path $dist "PlanePetClientResources.o"
if (-not (Test-Path -LiteralPath $resource)) {
    throw "Run build.ps1 first to compile the desktop resources."
}
$common = @("-std=c++17", "-O2", "-Wall", "-Wextra",
    "-B$(Split-Path -Parent $Compiler)\", "-static", "-static-libgcc",
    "-static-libstdc++", "-DPLANE_PET_UPDATE_SELF_TEST")
$stateExe = Join-Path $dist "PlanePetUpdateStateTest.exe"
& $Compiler @common (Join-Path $root "tests\update_state_test.cpp") `
    (Join-Path $root "desktop\update_manager.cpp") -o $stateExe `
    -lwinhttp -lcrypt32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "Update state regression build failed." }
& $stateExe
if ($LASTEXITCODE -ne 0) { throw "Update state regressions failed." }

$windowExe = Join-Path $dist "PlanePetUpdateWindowTest.exe"
& $Compiler @common -DPLANE_PET_UPDATE_WINDOW_SELF_TEST `
    (Join-Path $root "desktop\main.cpp") `
    (Join-Path $root "desktop\update_manager.cpp") $resource -o $windowExe `
    -mwindows -lws2_32 -lgdi32 -lgdiplus -lole32 -lshell32 -lcomdlg32 `
    -lwinhttp -lcrypt32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw "Update window regression build failed." }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("plane-pet-update-window-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$report = Join-Path $testRoot "window-test.txt"
$state = Join-Path $testRoot "client.binding"
$process = Start-Process -FilePath $windowExe -ArgumentList @(
    "--code=0", "--telemetry=0", "--state=`"$state`"",
    "--update-enabled=1", "--update-manifest=invalid",
    "--test-report=`"$report`""
) -PassThru -WindowStyle Hidden
if (-not $process.WaitForExit(20000)) {
    Stop-Process -Id $process.Id -Force
    throw "Update window regression timed out. Report: $report"
}
$process.WaitForExit()
Get-Content -LiteralPath $report
if ($process.ExitCode -ne 0) { throw "Update window regressions failed. Report: $report" }
Write-Host "PLANE_PET_UPDATE_WINDOWS_OK report=$report"
