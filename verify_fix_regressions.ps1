param(
    [string]$Compiler = "C:\Installation\Lib\mingw64\bin\g++.exe",
    [string]$Python = "C:\Users\81228\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root "dist"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("plane-pet-fixes-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $testRoot | Out-Null
$server = $null
$probe = [Net.Sockets.UdpClient]::new(0)
$port = ([Net.IPEndPoint]$probe.Client.LocalEndPoint).Port
$probe.Dispose()
function Check-Exit([string]$Name) {
    if ($LASTEXITCODE -ne 0) { throw "$Name failed: $LASTEXITCODE" }
}
Push-Location $root
try {
    & $Compiler "-B$(Split-Path -Parent $Compiler)\" -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ tests/client_edge_test.cpp desktop/update_manager.cpp -o dist/PlanePetClientEdgeTest.exe -lws2_32 -lgdi32 -lgdiplus -lole32 -lshell32 -lcomdlg32 -lwinhttp -lcrypt32 -lbcrypt
    Check-Exit "client edge compile"
    & .\dist\PlanePetClientEdgeTest.exe $testRoot
    Check-Exit "client edges"
    & $Compiler "-B$(Split-Path -Parent $Compiler)\" -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ tests/storage_regression_test.cpp desktop/update_manager.cpp -o dist/PlanePetStorageTest.exe -lws2_32 -lgdi32 -lgdiplus -lole32 -lshell32 -lcomdlg32 -lwinhttp -lcrypt32 -lbcrypt
    Check-Exit "storage compile"
    & .\dist\PlanePetStorageTest.exe (Join-Path $testRoot 'storage')
    Check-Exit "storage regressions"
    & $Compiler "-B$(Split-Path -Parent $Compiler)\" -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ tests/server_state_test.cpp -o dist/PlanePetServerStateTest.exe -lws2_32 -lbcrypt
    Check-Exit "server state compile"
    & .\dist\PlanePetServerStateTest.exe $testRoot
    Check-Exit "server provisional expiry"
    & $Python -m unittest discover -s tests -p 'test_gateway*.py' -v
    Check-Exit "gateway tests"
    & $Python tests/server_edge_test.py
    Check-Exit "server edges"
    & .\dist\PlanePetGameLayoutTest.exe
    Check-Exit "game layout"
    & .\dist\PlanePetGameSymmetryTest.exe
    Check-Exit "game symmetry"
    $server = Start-Process -FilePath (Join-Path $dist "PlanePetServer.exe") -WindowStyle Hidden -PassThru -ArgumentList @("--port=$port", "--seconds=2", "--store=$testRoot\server.db")
    Start-Sleep -Milliseconds 300
    & .\dist\PlanePetIntegrationTest.exe $port 481953
    Check-Exit "legacy full integration"
    & .\dist\PlanePetPairingLifecycleTest.exe $port lifecycle "$testRoot\credentials" 483956
    Check-Exit "pairing lifecycle"
    & .\dist\PlanePetPairingLifecycleTest.exe $port resume "$testRoot\credentials" 483956
    Check-Exit "pairing resume"
    & .\dist\PlanePetPairingLifecycleTest.exe $port unbind "$testRoot\credentials" 483956
    Check-Exit "pairing unbind"
    & .\dist\PlanePetPairingLifecycleTest.exe $port version_compatibility "$testRoot\versions" 483957
    Check-Exit "major/minor compatibility"
    .\tests\verify_updater.ps1
    .\verify_public_single_release.ps1
    .\verify_public_dual_release.ps1
    Write-Output "PLANE_PET_CONFIRMED_FIX_REGRESSIONS_OK"
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Pop-Location
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if ($resolved.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved).StartsWith("plane-pet-fixes-")) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
