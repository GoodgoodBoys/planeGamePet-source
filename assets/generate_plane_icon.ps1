param(
    [string]$Output = (Join-Path $PSScriptRoot "plane_pet.ico"),
    [string]$Preview = (Join-Path $PSScriptRoot "plane_pet_icon_preview.png")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function New-PlaneIconBitmap([int]$Size) {
    $bitmap = [Drawing.Bitmap]::new(
        $Size, $Size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality =
            [Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.ScaleTransform($Size / 256.0, $Size / 256.0)

        $badge = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(242, 18, 31, 50))
        $badgeEdge = [Drawing.Pen]::new([Drawing.Color]::FromArgb(190, 80, 205, 255), 5)
        try {
            $graphics.FillEllipse($badge, 10, 10, 236, 236)
            $graphics.DrawEllipse($badgeEdge, 12.5, 12.5, 231, 231)
        } finally {
            $badge.Dispose()
            $badgeEdge.Dispose()
        }

        $cx = 128.0
        $cy = 120.0
        $scale = 2.72
        $wings = [Drawing.PointF[]]@(
            [Drawing.PointF]::new($cx - 4 * $scale, $cy - 7 * $scale),
            [Drawing.PointF]::new($cx - 29 * $scale, $cy + 8 * $scale),
            [Drawing.PointF]::new($cx - 27 * $scale, $cy + 15 * $scale),
            [Drawing.PointF]::new($cx - 5 * $scale, $cy + 8 * $scale),
            [Drawing.PointF]::new($cx + 5 * $scale, $cy + 8 * $scale),
            [Drawing.PointF]::new($cx + 27 * $scale, $cy + 15 * $scale),
            [Drawing.PointF]::new($cx + 29 * $scale, $cy + 8 * $scale),
            [Drawing.PointF]::new($cx + 4 * $scale, $cy - 7 * $scale)
        )
        $tail = [Drawing.PointF[]]@(
            [Drawing.PointF]::new($cx - 4 * $scale, $cy + 14 * $scale),
            [Drawing.PointF]::new($cx - 14 * $scale, $cy + 23 * $scale),
            [Drawing.PointF]::new($cx - 12 * $scale, $cy + 27 * $scale),
            [Drawing.PointF]::new($cx + 12 * $scale, $cy + 27 * $scale),
            [Drawing.PointF]::new($cx + 14 * $scale, $cy + 23 * $scale),
            [Drawing.PointF]::new($cx + 4 * $scale, $cy + 14 * $scale)
        )
        $body = [Drawing.PointF[]]@(
            [Drawing.PointF]::new($cx, $cy - 31 * $scale),
            [Drawing.PointF]::new($cx + 5 * $scale, $cy - 17 * $scale),
            [Drawing.PointF]::new($cx + 7 * $scale, $cy + 15 * $scale),
            [Drawing.PointF]::new($cx + 4 * $scale, $cy + 27 * $scale),
            [Drawing.PointF]::new($cx - 4 * $scale, $cy + 27 * $scale),
            [Drawing.PointF]::new($cx - 7 * $scale, $cy + 15 * $scale),
            [Drawing.PointF]::new($cx - 5 * $scale, $cy - 17 * $scale)
        )

        $shadow = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(105, 0, 0, 0))
        try {
            $graphics.TranslateTransform(4, 6)
            $graphics.FillPolygon($shadow, $wings)
            $graphics.FillPolygon($shadow, $tail)
            $graphics.FillPolygon($shadow, $body)
            $graphics.ResetTransform()
            $graphics.ScaleTransform($Size / 256.0, $Size / 256.0)
        } finally {
            $shadow.Dispose()
        }

        $outerGlow = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(105, 255, 116, 28))
        $engine = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 255, 205, 42))
        try {
            $graphics.FillEllipse($outerGlow, $cx - 17, $cy + 67, 34, 50)
            $graphics.FillEllipse($engine, $cx - 10, $cy + 72, 20, 37)
        } finally {
            $outerGlow.Dispose()
            $engine.Dispose()
        }

        $outline = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 235, 250, 255), 6)
        $outline.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        $wingBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 0, 112, 184))
        $bodyBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 38, 186, 255))
        try {
            $graphics.FillPolygon($wingBrush, $wings)
            $graphics.DrawPolygon($outline, $wings)
            $graphics.FillPolygon($wingBrush, $tail)
            $graphics.DrawPolygon($outline, $tail)
            $graphics.FillPolygon($bodyBrush, $body)
            $graphics.DrawPolygon($outline, $body)
        } finally {
            $outline.Dispose()
            $wingBrush.Dispose()
            $bodyBrush.Dispose()
        }

        $canopy = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 155, 238, 255))
        $highlight = [Drawing.Pen]::new([Drawing.Color]::FromArgb(235, 230, 252, 255), 5)
        $highlight.StartCap = [Drawing.Drawing2D.LineCap]::Round
        $highlight.EndCap = [Drawing.Drawing2D.LineCap]::Round
        try {
            $graphics.FillEllipse($canopy, $cx - 11, $cy - 50, 22, 35)
            $graphics.DrawLine($highlight, $cx, $cy - 69, $cx, $cy + 47)
        } finally {
            $canopy.Dispose()
            $highlight.Dispose()
        }
        return $bitmap
    } finally {
        $graphics.Dispose()
    }
}

$outputDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($Output))
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$images = [Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    $bitmap = New-PlaneIconBitmap $size
    try {
        $memory = [IO.MemoryStream]::new()
        try {
            $bitmap.Save($memory, [Drawing.Imaging.ImageFormat]::Png)
            $images.Add($memory.ToArray())
        } finally {
            $memory.Dispose()
        }
        if ($size -eq 256) {
            $bitmap.Save($Preview, [Drawing.Imaging.ImageFormat]::Png)
        }
    } finally {
        $bitmap.Dispose()
    }
}

$stream = [IO.File]::Create($Output)
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    for ($index = 0; $index -lt $images.Count; ++$index) {
        $size = $sizes[$index]
        $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
        $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($image in $images) {
        $writer.Write($image)
    }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}

Write-Output "PLANE_PET_ICON_OK output=$Output sizes=$($sizes -join ',')"
