$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$drawingAssembly = [Drawing.Bitmap].Assembly.Location
$references = @($drawingAssembly, [Drawing.Rectangle].Assembly.Location, [Console].Assembly.Location)
foreach ($assemblyName in @('System.Private.Windows.GdiPlus.dll', 'System.Private.Windows.Core.dll')) {
    $assemblyPath = Join-Path (Split-Path -Parent $drawingAssembly) $assemblyName
    if (Test-Path -LiteralPath $assemblyPath) { $references += $assemblyPath }
}
Add-Type -ReferencedAssemblies $references -TypeDefinition (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'WinAnimation.cs') -Raw)
$previousHit = Join-Path $PSScriptRoot 'hit-width-reference.png'
[WinAnimation]::Export($PSScriptRoot, $previousHit)
$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
& $ffmpeg -hide_banner -loglevel error -y -framerate 50 -i (Join-Path $PSScriptRoot 'preview-frames/%03d.png') `
    -filter_complex '[0:v]split[a][b];[a]palettegen=stats_mode=full:reserve_transparent=0[p];[b][p]paletteuse=dither=none' `
    -loop 0 (Join-Path $PSScriptRoot 'win-w1-preview.gif')
if ($LASTEXITCODE -ne 0) { throw 'GIF encoding failed' }
& $ffmpeg -hide_banner -loglevel error -y -framerate 50 -i (Join-Path $PSScriptRoot 'rgba-frames/%03d.png') `
    -plays 0 -f apng (Join-Path $PSScriptRoot 'win-w1-transparent.apng')
if ($LASTEXITCODE -ne 0) { throw 'Transparent APNG encoding failed' }
'WIN_W1_ENCODING_OK gif+apng 120frames/2400ms'
