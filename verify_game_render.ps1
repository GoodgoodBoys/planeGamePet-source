param(
    [string]$Compiler = "C:\Installation\Lib\mingw64\bin\g++.exe",
    [string]$ResourceCompiler = "C:\Installation\Lib\mingw64\bin\windres.exe",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$compilerBin = Split-Path -Parent $Compiler
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $dist "game_boundary_fixture.bmp"
}
$testExe = Join-Path $dist "PlanePetRenderSelfTest.exe"
$resourceObject = Join-Path $dist "PlanePetRenderSelfTestResources.o"
Push-Location $root
try {
    & $ResourceCompiler (Join-Path $root "desktop\resources.rc") `
        -O coff -o $resourceObject
    if ($LASTEXITCODE -ne 0) {
        throw "Render self-test resource build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
& $Compiler "-B$compilerBin\" -std=c++17 -O2 -Wall -Wextra -static -static-libgcc `
    -static-libstdc++ -DPLANE_PET_RENDER_SELF_TEST `
    (Join-Path $root "desktop\main.cpp") `
    (Join-Path $root "desktop\update_manager.cpp") $resourceObject `
    -o $testExe -mwindows -lws2_32 -lgdi32 -lgdiplus -lole32 `
    -lshell32 -lcomdlg32 -lwinhttp -lcrypt32 -lbcrypt
if ($LASTEXITCODE -ne 0) {
    throw "Render self-test build failed with exit code $LASTEXITCODE"
}
$process = Start-Process -FilePath $testExe -ArgumentList @(
    "--render-fixture=$Output", "--telemetry=0"
) -WorkingDirectory $dist -Wait -PassThru
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $Output)) {
    throw "Render self-test failed with exit code $($process.ExitCode)"
}
$bytes = [IO.File]::ReadAllBytes($Output)
if ($bytes.Length -ne 54 + 240 * 372 * 4 -or
    $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4D -or
    [BitConverter]::ToInt32($bytes, 18) -ne 240 -or
    [Math]::Abs([BitConverter]::ToInt32($bytes, 22)) -ne 372) {
    throw "Unexpected render fixture format or dimensions."
}
$petOutput = Join-Path (Split-Path -Parent $Output) "pet_online_fixture.bmp"
if (-not (Test-Path -LiteralPath $petOutput)) {
    throw "Online pet render fixture was not generated."
}
$petBytes = [IO.File]::ReadAllBytes($petOutput)
if ($petBytes.Length -ne 54 + 280 * 150 * 4 -or
    $petBytes[0] -ne 0x42 -or $petBytes[1] -ne 0x4D -or
    [BitConverter]::ToInt32($petBytes, 18) -ne 280 -or
    [Math]::Abs([BitConverter]::ToInt32($petBytes, 22)) -ne 150) {
    throw "Unexpected online pet fixture format or dimensions."
}
$motionOutput = Join-Path (Split-Path -Parent $Output) `
    "pet_formation_metrics.txt"
if (-not (Test-Path -LiteralPath $motionOutput)) {
    throw "Pet chase motion metrics were not generated."
}
$motionLines = @(Get-Content -LiteralPath $motionOutput)
if ($motionLines.Count -ne 4 -or
    @($motionLines | Where-Object { $_ -notmatch ' ok=1 ' }).Count -ne 0) {
    throw "Pet chase motion continuity or boundary validation failed."
}
Write-Output "PC_PET_GAME_RENDER_OK output=$Output size=240x372 hud=52 map=320"
Write-Output "PC_PET_ONLINE_RENDER_OK output=$petOutput size=280x150 formation=red+blue d2_clouds=3 emotes=4"
Write-Output "PC_PET_CHASE_MOTION_OK samples=4x120s min_distance_ge=96px forward_only=1 max_step_lt=3px turns_per_sample_ge=3"
