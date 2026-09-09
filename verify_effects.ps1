param(
    [string]$Compiler = 'C:\Installation\Lib\mingw64\bin\g++.exe',
    [string]$ResourceCompiler = 'C:\Installation\Lib\mingw64\bin\windres.exe',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Output) {
    $fxVersion = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'common\app_version.h')
    if ($fxVersion -notmatch 'kString\[\] = "(\d+\.\d+\.\d+)"') { throw 'Missing app version' }
    $Output = Join-Path $PSScriptRoot ("dist\vfx-verification-" + $Matches[1])
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$fxFlags = @('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',"-B$(Split-Path -Parent $Compiler)\")
Push-Location $PSScriptRoot
try {
    & $ResourceCompiler desktop/resources.rc -O coff -o dist/PlanePetEffectsResources.o
    if ($LASTEXITCODE -ne 0) { throw 'Effect resource compile failed.' }
    & $Compiler @fxFlags tests/game_effects_test.cpp -o dist/PlanePetGameEffectsTest.exe
    if ($LASTEXITCODE -ne 0) { throw 'Effect unit-test compile failed.' }
    & .\dist\PlanePetGameEffectsTest.exe
    if ($LASTEXITCODE -ne 0) { throw "Effect unit test failed: $LASTEXITCODE" }
    & $Compiler @fxFlags tests/combat_feedback_test.cpp -o dist/PlanePetCombatFeedbackTest.exe
    if ($LASTEXITCODE -ne 0) { throw 'Combat feedback unit-test compile failed.' }
    & .\dist\PlanePetCombatFeedbackTest.exe
    if ($LASTEXITCODE -ne 0) { throw "Combat feedback unit test failed: $LASTEXITCODE" }
    & $Compiler @fxFlags tests/game_effects_render_test.cpp desktop/update_manager.cpp dist/PlanePetEffectsResources.o -o dist/PlanePetGameEffectsRenderTest.exe -lws2_32 -lgdi32 -lgdiplus -lole32 -lshell32 -lcomdlg32 -lwinhttp -lcrypt32 -lbcrypt
    if ($LASTEXITCODE -ne 0) { throw 'Actual renderer test compile failed.' }
    & .\dist\PlanePetGameEffectsRenderTest.exe $Output
    if ($LASTEXITCODE -ne 0) { throw "Actual renderer test failed: $LASTEXITCODE" }
    "EFFECTS_VERIFICATION_OK output=$Output"
} finally { Pop-Location }
