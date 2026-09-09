param([string]$Compiler = 'C:\Installation\Lib\mingw64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$auditRoot = Join-Path $root ('dist\release-regression-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $auditRoot | Out-Null
$profile = Join-Path $auditRoot '中文 用户路径'
New-Item -ItemType Directory -Path $profile | Out-Null
$exe = Join-Path $auditRoot 'ReleaseTest.exe'
$report = Join-Path $profile '检查结果.txt'
& "$root\release\Prepare-PlanePetNotices.ps1" -Compiler $Compiler
Push-Location $root
try {
  & (Join-Path (Split-Path -Parent $Compiler) 'windres.exe') 'desktop/resources.rc' -O coff -o "$auditRoot\Resources.o"
  if ($LASTEXITCODE -ne 0) { throw 'Release regression resource compilation failed' }
} finally { Pop-Location }
& $Compiler "-B$(Split-Path -Parent $Compiler)\" -std=c++17 -O2 -Wall -Wextra `
  -static -static-libgcc -static-libstdc++ -DPLANE_PET_RELEASE_SELF_TEST `
  "$root\desktop\main.cpp" "$root\desktop\update_manager.cpp" `
  "$auditRoot\Resources.o" -o $exe -mwindows -lws2_32 -lgdi32 `
  -lgdiplus -lole32 -lshell32 -lcomdlg32 -lwinhttp -lcrypt32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw 'Release regression compilation failed' }
$process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -ArgumentList @(
  '--name="测试 用户"', '--telemetry=0', '--code=0', '--server=127.0.0.1:9',
  "--state=`"$profile\绑定 存档.binding`"", "--test-report=`"$report`""
)
if (-not $process.WaitForExit(45000)) {
  Stop-Process -Id $process.Id -Force
  throw "Isolated release regression timed out: $auditRoot"
}
$process.WaitForExit()
Get-Content -LiteralPath $report
if ($process.ExitCode -ne 0) { throw "Release regression failed: $report" }
Write-Output "RELEASE_HARDENING_WINDOWS_OK report=$report"
