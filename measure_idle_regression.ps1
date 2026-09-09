param([Parameter(Mandatory=$true)][string]$Baseline, [int]$Seconds = 12)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 5 -or $Seconds -gt 30) { throw 'Use a bounded 5–30 second sample per case' }
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$scope = Join-Path $root ('dist\idle-benchmark-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scope | Out-Null
$current = Join-Path $root 'dist\PlanePetClient.exe'
foreach ($binary in @($Baseline, $current)) { if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Missing benchmark executable' } }
$results = @()
foreach ($version in @('baseline','candidate')) {
    foreach ($hidden in @(0,1)) {
        $file = if ($version -eq 'baseline') { $Baseline } else { $current }
        $profile = Join-Path $scope "$version-$hidden.binding"
        $process = Start-Process -FilePath $file -WindowStyle Hidden -PassThru -ArgumentList @(
            '--code=0', '--server=127.0.0.1:9', '--telemetry=0', '--update-enabled=0',
            '--pet-x=60','--pet-y=520',"--hidden=$hidden", '--name=Benchmark', "--state=`"$profile`""
        )
        try {
            Start-Sleep -Seconds 2
            if ($process.HasExited) { throw 'Benchmark process exited during initialization' }
            $process.Refresh()
            $startCpu = $process.TotalProcessorTime.TotalSeconds
            $startHandles = $process.HandleCount
            $clock = [Diagnostics.Stopwatch]::StartNew()
            Start-Sleep -Seconds $Seconds
            $process.Refresh()
            $result = [pscustomobject]@{
                Version=$version; Scenario=if ($hidden) {'hidden-unbound'} else {'visible-unbound-pairing-card'}
                Seconds=[math]::Round($clock.Elapsed.TotalSeconds,2)
                CpuOneCorePercent=[math]::Round(100*($process.TotalProcessorTime.TotalSeconds-$startCpu)/$clock.Elapsed.TotalSeconds,2)
                MemoryMiB=[math]::Round($process.WorkingSet64/1MB,1)
                HandleDelta=$process.HandleCount-$startHandles
            }
            $results += $result
            $result | ConvertTo-Json -Compress
        } finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        }
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $scope 'results.json') -Encoding utf8
"IDLE_BENCHMARK_COMPLETED report=$scope\results.json"
