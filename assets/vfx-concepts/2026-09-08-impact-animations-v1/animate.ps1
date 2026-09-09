$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$drawingAssembly = [Drawing.Bitmap].Assembly.Location
$references = @($drawingAssembly, [Drawing.Rectangle].Assembly.Location, [Console].Assembly.Location)
foreach ($assemblyName in @('System.Private.Windows.GdiPlus.dll', 'System.Private.Windows.Core.dll')) {
    $assemblyPath = Join-Path (Split-Path -Parent $drawingAssembly) $assemblyName
    if (Test-Path -LiteralPath $assemblyPath) { $references += $assemblyPath }
}
Add-Type -ReferencedAssemblies $references -TypeDefinition (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PreviewAnimation.cs') -Raw)
[ImpactAnimationPreview]::Export($PSScriptRoot)
$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
foreach ($effectName in @('heart-break', 'hit-orange')) {
    & $ffmpeg -hide_banner -loglevel error -y -framerate 50 -i (Join-Path $PSScriptRoot ($effectName + '-frames/%03d.png')) `
        -filter_complex '[0:v]split[a][b];[a]palettegen=stats_mode=full:reserve_transparent=0[p];[b][p]paletteuse=dither=none' `
        -loop 0 (Join-Path $PSScriptRoot ($effectName + '-preview.gif'))
    if ($LASTEXITCODE -ne 0) { throw "GIF encoding failed: $effectName" }
}
'ENCODING_PASS: 2 previews, 100 frames each, 50 fps, 2 second loops'
