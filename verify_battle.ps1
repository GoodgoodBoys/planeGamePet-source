param(
    [string]$Compiler = 'C:\Installation\Lib\mingw64\bin\g++.exe',
    [string]$Python = 'C:\Users\81228\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [switch]$SkipLive,
    [switch]$AllowPublicTest
)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskReport = Join-Path $taskRoot ('dist\battle-verification-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskReport | Out-Null
$taskFlags = @('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',"-B$(Split-Path -Parent $Compiler)\")
$taskLibraries = @('-lws2_32','-lgdi32','-lgdiplus','-lole32','-lshell32','-lcomdlg32','-lwinhttp','-lcrypt32','-lbcrypt')
function Assert-Exit([string]$Label) { if ($LASTEXITCODE -ne 0) { throw "$Label failed: $LASTEXITCODE" } }
Push-Location $taskRoot
try {
    foreach ($taskCore in @('motion','battle')) {
        & $Compiler @taskFlags "tests/pc_${taskCore}_test.cpp" -o "dist/PlanePetCore-$taskCore.exe"
        Assert-Exit "$taskCore compile"
        & ".\dist\PlanePetCore-$taskCore.exe"; Assert-Exit "$taskCore core"
    }
    & $Compiler @taskFlags server/main.cpp -o dist/PlanePetBattleServer.exe -lws2_32 -lbcrypt
    Assert-Exit 'battle server compile'
    & $Compiler @taskFlags tests/battle_render_test.cpp desktop/update_manager.cpp dist/PlanePetClientResources.o -o dist/PlanePetBattleRenderTest.exe @taskLibraries
    Assert-Exit 'actual renderer compile'
    & .\dist\PlanePetBattleRenderTest.exe $taskReport; Assert-Exit 'actual renderer'
    if (-not $SkipLive -or $AllowPublicTest) {
        & $Compiler @taskFlags -DPLANE_PET_MOTION_SELF_TEST desktop/main.cpp desktop/update_manager.cpp dist/PlanePetClientResources.o -o dist/PlanePetBattleLive.exe @taskLibraries
        Assert-Exit 'instrumented actual client compile'
    }
    if (-not $SkipLive) {
        & $Python tests/motion_live_test.py --battle --rtt 200 --loss 2; Assert-Exit 'impaired live match'
        & $Python tests/motion_live_test.py --battle --rtt 300 --loss 5 --expect-abort; Assert-Exit 'explicit sync abort'
        foreach ($taskPhase in @('playing', 'countdown')) {
            & $Python tests/motion_live_test.py --battle --rtt 100 --loss 2 --blackout $taskPhase
            Assert-Exit "$taskPhase complete blackout and restore"
        }
    }
    if ($AllowPublicTest) {
        & $Python tests/motion_public_test.py --allow-public-test --battle; Assert-Exit 'public live match'
        & $Python tests/motion_public_test.py --allow-public-test --battle --expect-abort; Assert-Exit 'public sync abort'
    }
    "BATTLE_VERIFICATION_OK report=$taskReport"
} finally { Pop-Location }
