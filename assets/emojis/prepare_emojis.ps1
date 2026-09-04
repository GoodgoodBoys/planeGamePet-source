param(
    [string]$SheetPath = (Join-Path $PSScriptRoot "source\vector-faces-confirmed.png"),
    [string]$VictoryPath = (Join-Path $PSScriptRoot "source\victory-yellow-confirmed.png")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$drawingAssembly = [Drawing.Bitmap].Assembly.Location
$drawingPrimitivesAssembly = [Drawing.Rectangle].Assembly.Location
$gdiPlusAssembly = Join-Path (Split-Path -Parent $drawingAssembly) `
    "System.Private.Windows.GdiPlus.dll"
$windowsCoreAssembly = Join-Path (Split-Path -Parent $drawingAssembly) `
    "System.Private.Windows.Core.dll"
Add-Type -ReferencedAssemblies @(
    $drawingAssembly, $drawingPrimitivesAssembly, $gdiPlusAssembly,
    $windowsCoreAssembly) `
    -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class PlanePetEmojiPrep {
  private static Rectangle ContentBounds(Bitmap image, Rectangle region) {
    int[] rows = new int[region.Height];
    int[] columns = new int[region.Width];
    Rectangle whole = new Rectangle(0, 0, image.Width, image.Height);
    BitmapData data = image.LockBits(
        whole, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    try {
      int stride = Math.Abs(data.Stride);
      byte[] pixels = new byte[stride * image.Height];
      Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
      for (int y = 0; y < region.Height; ++y) {
        int sourceY = region.Top + y;
        int row = data.Stride >= 0 ? sourceY * stride
                                   : (image.Height - 1 - sourceY) * stride;
        for (int x = 0; x < region.Width; ++x) {
          int alpha = pixels[row + (region.Left + x) * 4 + 3];
          if (alpha >= 24) {
            ++rows[y];
            ++columns[x];
          }
        }
      }
    } finally {
      image.UnlockBits(data);
    }
    const int minimumRun = 8;
    int left = 0, right = region.Width - 1;
    int top = 0, bottom = region.Height - 1;
    while (left <= right && columns[left] < minimumRun) ++left;
    while (right >= left && columns[right] < minimumRun) --right;
    while (top <= bottom && rows[top] < minimumRun) ++top;
    while (bottom >= top && rows[bottom] < minimumRun) --bottom;
    if (left > right || top > bottom)
      throw new InvalidOperationException("No opaque emoji content found.");
    return new Rectangle(region.Left + left, region.Top + top,
                         right - left + 1, bottom - top + 1);
  }

  public static void Export(Bitmap source, Rectangle region, string path) {
    Rectangle content = ContentBounds(source, region);
    using (Bitmap output = new Bitmap(512, 512, PixelFormat.Format32bppArgb)) {
      output.SetResolution(96, 96);
      using (Graphics graphics = Graphics.FromImage(output)) {
        graphics.Clear(Color.Transparent);
        graphics.CompositingMode = CompositingMode.SourceCopy;
        graphics.CompositingQuality = CompositingQuality.HighQuality;
        graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
        graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        graphics.SmoothingMode = SmoothingMode.HighQuality;
        const int target = 466;
        double scale = Math.Min((double)target / content.Width,
                                (double)target / content.Height);
        int width = Math.Max(1, (int)Math.Round(content.Width * scale));
        int height = Math.Max(1, (int)Math.Round(content.Height * scale));
        Rectangle destination = new Rectangle((512 - width) / 2,
                                              (512 - height) / 2,
                                              width, height);
        graphics.DrawImage(source, destination, content, GraphicsUnit.Pixel);
      }
      output.Save(path, ImageFormat.Png);
    }
  }

  public static void Clear(string path, Rectangle region) {
    using (Bitmap image = new Bitmap(path)) {
      using (Bitmap output = new Bitmap(image.Width, image.Height,
                                        PixelFormat.Format32bppArgb)) {
        using (Graphics graphics = Graphics.FromImage(output)) {
          graphics.Clear(Color.Transparent);
          graphics.DrawImageUnscaled(image, 0, 0);
          graphics.CompositingMode = CompositingMode.SourceCopy;
          using (SolidBrush transparent = new SolidBrush(Color.Transparent))
            graphics.FillRectangle(transparent, region);
        }
        image.Dispose();
        output.Save(path, ImageFormat.Png);
      }
    }
  }
}
'@

if (-not (Test-Path -LiteralPath $SheetPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $VictoryPath -PathType Leaf)) {
    throw "Confirmed emoji source artwork is missing."
}

$output = $PSScriptRoot
$sheet = [Drawing.Bitmap]::new($SheetPath)
try {
    $halfWidth = [int]($sheet.Width / 2)
    $halfHeight = [int]($sheet.Height / 2)
    [PlanePetEmojiPrep]::Export(
        $sheet, [Drawing.Rectangle]::new(0, 0, $halfWidth, $halfHeight),
        (Join-Path $output "laugh.png"))
    [PlanePetEmojiPrep]::Export(
        $sheet, [Drawing.Rectangle]::new(
            $halfWidth, 0, $sheet.Width - $halfWidth, $halfHeight),
        (Join-Path $output "cry.png"))
    [PlanePetEmojiPrep]::Export(
        $sheet, [Drawing.Rectangle]::new(
            0, $halfHeight, $halfWidth, $sheet.Height - $halfHeight),
        (Join-Path $output "angry.png"))
}
finally {
    $sheet.Dispose()
}

$victory = [Drawing.Bitmap]::new($VictoryPath)
try {
    [PlanePetEmojiPrep]::Export(
        $victory,
        [Drawing.Rectangle]::new(0, 0, $victory.Width, $victory.Height),
        (Join-Path $output "victory.png"))
}
finally {
    $victory.Dispose()
}

# The accepted generation sheet contains three isolated paint flecks outside
# the artwork. Clear only those known transparent-margin regions so the
# approved emoji shapes themselves remain untouched.
[PlanePetEmojiPrep]::Clear(
    (Join-Path $output "laugh.png"), [Drawing.Rectangle]::new(480, 470, 32, 42))
[PlanePetEmojiPrep]::Clear(
    (Join-Path $output "cry.png"), [Drawing.Rectangle]::new(0, 470, 512, 42))
[PlanePetEmojiPrep]::Clear(
    (Join-Path $output "angry.png"), [Drawing.Rectangle]::new(0, 0, 112, 72))

Write-Output "PLANE_PET_EMOJI_ASSETS_OK size=512 count=4 alpha=1"
