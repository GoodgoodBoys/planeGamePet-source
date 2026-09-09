$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($PSVersionTable.PSEdition -ne "Desktop") {
    $windowsPowerShell = Join-Path $env:WINDIR `
        "System32\WindowsPowerShell\v1.0\powershell.exe"
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
    if ($LASTEXITCODE -ne 0) {
        throw "Pet visual asset verification failed with exit code $LASTEXITCODE"
    }
    return
}

Add-Type -AssemblyName System.Drawing

$beckonPath = Join-Path $root "assets\emojis\beckon.gif"
$redPath = Join-Path $root "assets\planes\pet-red-d2.png"
$bluePath = Join-Path $root "assets\planes\pet-blue-d2.png"
$cloudPaths = @(1..3 | ForEach-Object {
    Join-Path $root "assets\planes\pet-cloud-d2-$_.png"
})
$resourcePath = Join-Path $root "desktop\resources.rc"
$sourcePath = Join-Path $root "desktop\main.cpp"

foreach ($path in @($beckonPath, $redPath, $bluePath) + $cloudPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing embedded visual asset: $path"
    }
}

$beckon = [Drawing.Image]::FromFile($beckonPath)
try {
    if ($beckon.Width -ne 512 -or $beckon.Height -ne 512) {
        throw "Beckon animation must be 512x512."
    }
    $frameCount = $beckon.GetFrameCount([Drawing.Imaging.FrameDimension]::Time)
    if ($frameCount -ne 43) {
        throw "Beckon animation must contain 43 frames; actual=$frameCount"
    }
    $delays = $beckon.GetPropertyItem(0x5100).Value
    $durationCentiseconds = 0
    for ($index = 0; $index -lt $delays.Length; $index += 4) {
        $delay = [BitConverter]::ToInt32($delays, $index)
        if ($delay -lt 4 -or $delay -gt 6) {
            throw "Unexpected beckon frame delay at frame $($index / 4): $delay cs"
        }
        $durationCentiseconds += $delay
    }
    if ($durationCentiseconds -lt 200 -or $durationCentiseconds -gt 230) {
        throw "Unexpected beckon animation duration: $durationCentiseconds cs"
    }
}
finally {
    $beckon.Dispose()
}

function Test-PlaneAsset([string]$path, [string]$label) {
    $image = [Drawing.Bitmap]::FromFile($path)
    try {
        if ($image.Width -ne 512 -or $image.Height -ne 512) {
            throw "$label D2 sprite must be 512x512."
        }
        $visible = 0
        $minX = 512
        $minY = 512
        $maxX = -1
        $maxY = -1
        for ($y = 0; $y -lt 512; $y += 4) {
            for ($x = 0; $x -lt 512; $x += 4) {
                if ($image.GetPixel($x, $y).A -gt 16) {
                    ++$visible
                    $minX = [Math]::Min($minX, $x)
                    $minY = [Math]::Min($minY, $y)
                    $maxX = [Math]::Max($maxX, $x)
                    $maxY = [Math]::Max($maxY, $y)
                }
            }
        }
        if ($visible -lt 2500 -or $visible -gt 7500) {
            throw "$label D2 sprite has implausible alpha coverage: $visible samples"
        }
        if ($minX -gt 150 -or $minY -gt 40 -or $maxX -lt 360 -or $maxY -lt 460) {
            throw "$label D2 sprite is not using the expected transparent canvas."
        }
        return [PSCustomObject]@{
            Visible = $visible
            Bounds = "$minX,$minY-$maxX,$maxY"
        }
    }
    finally {
        $image.Dispose()
    }
}

$red = Test-PlaneAsset $redPath "Red"
$blue = Test-PlaneAsset $bluePath "Blue"
if ([Math]::Abs($red.Visible - $blue.Visible) -gt 250) {
    throw "The D2 red and blue aircraft silhouettes are not sufficiently aligned."
}

foreach ($cloudPath in $cloudPaths) {
    $cloud = [Drawing.Bitmap]::FromFile($cloudPath)
    try {
        if ($cloud.Width -ne 512 -or $cloud.Height -ne 512) {
            throw "D2 cloud sprite must be 512x512: $cloudPath"
        }
        $visible = 0
        for ($y = 0; $y -lt 512; $y += 4) {
            for ($x = 0; $x -lt 512; $x += 4) {
                if ($cloud.GetPixel($x, $y).A -gt 16) { ++$visible }
            }
        }
        if ($visible -lt 1200 -or $visible -gt 8500) {
            throw "D2 cloud has implausible alpha coverage: $visible samples"
        }
    }
    finally {
        $cloud.Dispose()
    }
}

$resources = Get-Content -LiteralPath $resourcePath -Raw
$source = Get-Content -LiteralPath $sourcePath -Raw
$requiredResourceLines = @(
    '104 RCDATA "assets/emojis/beckon.gif"',
    '151 RCDATA "assets/planes/pet-red-d2.png"',
    '152 RCDATA "assets/planes/pet-blue-d2.png"',
    '153 RCDATA "assets/planes/pet-cloud-d2-1.png"',
    '154 RCDATA "assets/planes/pet-cloud-d2-2.png"',
    '155 RCDATA "assets/planes/pet-cloud-d2-3.png"'
)
foreach ($line in $requiredResourceLines) {
    if (-not $resources.Contains($line)) {
        throw "Missing resource declaration: $line"
    }
}
foreach ($fragment in @(
        'kBeckonFrameMilliseconds = 50',
        'const int bounce = value == 4 ? 0',
        'value == 4))',
        'constexpr int kGameSpriteSize = 36',
        'constexpr int kPetSpriteSize = 48',
        'DrawPixelProjectile(',
        'QuickEmoteBubbleRect(',
        'badge.center = {bubble.right + (moon ? 2 : -3)',
        'place(formation.red, false, PeerDndKnown() && peerDnd_, false)',
        'RGB(0, 235, 255)',
        'RGB(255, 48, 193)',
        'pose.angle - 1.57079632679489661923',
        'lateralCrossings < 3')) {
    if (-not $source.Contains($fragment)) {
        throw "Missing visual behavior guard: $fragment"
    }
}
if ($source.Contains('case 4: return L"✌"')) {
    throw "The retired victory-hand fallback is still active."
}

Write-Output (("PC_PET_VISUAL_ASSETS_OK beckon=43frames/{0}ms " +
    "red={1}/{2} blue={3}/{4} clouds=3") -f ($durationCentiseconds * 10),
    $red.Visible, $red.Bounds, $blue.Visible, $blue.Bounds)
