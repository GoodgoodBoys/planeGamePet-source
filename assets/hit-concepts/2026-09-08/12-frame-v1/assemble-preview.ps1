param(
    [string]$Source = 'C:\Users\81228\.codex\generated_images\01a05a85-ae82-78f0-97a3-5d5467bce706\exec-387dcdc7-0bc1-4c26-a7dc-2e47d3d2f3da.png',
    [string]$FFmpeg = 'C:\Installation\Lib\ffmpeg-8.1-full_build\ffmpeg-8.1-full_build\bin\ffmpeg.exe'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$taskSheet = [Drawing.Bitmap]::new($Source)
# Registration measured on the generated artwork, not changing bounds-based
# recentering (which would make the origin jump as sparks separate).
$taskOrigins = @(@(189,201),@(535,199),@(887,205),@(1226,201),
    @(179,516),@(530,514),@(893,515),@(1237,523),
    @(196,850),@(544,850),@(887,850),@(1260,850))
$taskFrames = Join-Path $PSScriptRoot 'frames'
New-Item -ItemType Directory -Force -Path $taskFrames | Out-Null
try {
    if ($taskSheet.Width -ne 1448 -or $taskSheet.Height -ne 1086) { throw 'Unexpected source dimensions' }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $PSScriptRoot 'generated-12-frame-sheet.png')
    $taskAtlas = [Drawing.Bitmap]::new(1280, 960)
    $taskGraphics = [Drawing.Graphics]::FromImage($taskAtlas)
    try {
        for ($taskIndex = 0; $taskIndex -lt 12; ++$taskIndex) {
            $taskX = $taskOrigins[$taskIndex][0] - 160
            $taskY = $taskOrigins[$taskIndex][1] - 160
            $taskRect = [Drawing.Rectangle]::new($taskX, $taskY, 320, 320)
            $taskFrame = $taskSheet.Clone($taskRect, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
            try {
                $taskFrame.Save((Join-Path $taskFrames ('hit-{0:00}.png' -f ($taskIndex + 1))), [Drawing.Imaging.ImageFormat]::Png)
                $taskGraphics.DrawImageUnscaled($taskFrame, ($taskIndex % 4) * 320, [Math]::Floor($taskIndex / 4) * 320)
            } finally { $taskFrame.Dispose() }
        }
        $taskAtlas.Save((Join-Path $PSScriptRoot 'hit-12-registered.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $taskGraphics.Dispose(); $taskAtlas.Dispose() }
    # A blank pause is for the looping preview only, not a thirteenth hit frame.
    # Identical RGB format avoids FFmpeg reinitializing its filter graph when
    # the pause follows the twelve RGB frames (which would drop earlier frames).
    $taskPause = [Drawing.Bitmap]::new(320, 320, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $taskGraphics = [Drawing.Graphics]::FromImage($taskPause)
    try {
        $taskGraphics.Clear($taskSheet.GetPixel(320,320))
        $taskPause.Save((Join-Path $taskFrames 'pause.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $taskGraphics.Dispose(); $taskPause.Dispose() }
} finally { $taskSheet.Dispose() }
& $FFmpeg -hide_banner -loglevel error -f concat -safe 0 -i (Join-Path $PSScriptRoot 'timing.txt') `
    -filter_complex '[0:v]split[a][b];[a]palettegen=max_colors=64:reserve_transparent=0[p];[b][p]paletteuse=dither=none' `
    -fps_mode vfr -loop 0 -y (Join-Path $PSScriptRoot 'hit-12-preview.gif')
if ($LASTEXITCODE -ne 0) { throw 'GIF encoding failed' }
"HIT_PREVIEW_OK action_frames=12 frame_ms=40 pause_ms=640 output=$PSScriptRoot"
