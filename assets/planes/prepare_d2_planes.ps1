param(
    [string]$SourcePath = (Join-Path $PSScriptRoot "source\d2-sheet.png")
)

$ErrorActionPreference = "Stop"

# System.Drawing's native image interfaces are not referenceable by Add-Type
# under the bundled PowerShell Core runtime. Re-enter through Windows
# PowerShell so the extractor stays reproducible on the supported OS.
if ($PSVersionTable.PSEdition -ne "Desktop") {
    $windowsPowerShell = Join-Path $env:WINDIR `
        "System32\WindowsPowerShell\v1.0\powershell.exe"
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass `
        -File $PSCommandPath -SourcePath $SourcePath
    if ($LASTEXITCODE -ne 0) {
        throw "D2 plane extraction failed with exit code $LASTEXITCODE"
    }
    return
}

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path -LiteralPath $SourcePath)) {
    throw "D2 source sheet not found: $SourcePath"
}

if (-not ("D2PlaneExtractor" -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

public static class D2PlaneExtractor {
  private static bool IsConnectedBackground(byte b, byte g, byte r) {
    // The approved D2 sheet uses a continuous, very dark navy backdrop.
    // Flood filling only from the crop boundary preserves dark cockpit/body
    // pixels that are enclosed by the aircraft's light outline.
    return r <= 44 && g <= 68 && b <= 112;
  }

  public static void Extract(string sourcePath, Rectangle crop,
                             string outputPath) {
    using (var source = new Bitmap(sourcePath))
    using (var working = new Bitmap(crop.Width, crop.Height,
                                    PixelFormat.Format32bppArgb)) {
      using (var graphics = Graphics.FromImage(working)) {
        graphics.CompositingMode = CompositingMode.SourceCopy;
        graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
        graphics.PixelOffsetMode = PixelOffsetMode.Half;
        graphics.DrawImage(source, new Rectangle(0, 0, crop.Width, crop.Height),
                           crop, GraphicsUnit.Pixel);
      }

      var area = new Rectangle(0, 0, working.Width, working.Height);
      var data = working.LockBits(area, ImageLockMode.ReadWrite,
                                  PixelFormat.Format32bppArgb);
      int stride = Math.Abs(data.Stride);
      int byteCount = stride * working.Height;
      byte[] bytes = new byte[byteCount];
      System.Runtime.InteropServices.Marshal.Copy(data.Scan0, bytes, 0,
                                                  byteCount);
      bool[] cleared = new bool[working.Width * working.Height];
      var queue = new Queue<int>();

      Action<int, int> enqueue = (x, y) => {
        int logical = y * working.Width + x;
        if (cleared[logical]) return;
        int offset = y * stride + x * 4;
        if (!IsConnectedBackground(bytes[offset], bytes[offset + 1],
                                   bytes[offset + 2])) return;
        cleared[logical] = true;
        queue.Enqueue(logical);
      };

      for (int x = 0; x < working.Width; ++x) {
        enqueue(x, 0);
        enqueue(x, working.Height - 1);
      }
      for (int y = 1; y + 1 < working.Height; ++y) {
        enqueue(0, y);
        enqueue(working.Width - 1, y);
      }
      while (queue.Count != 0) {
        int logical = queue.Dequeue();
        int x = logical % working.Width;
        int y = logical / working.Width;
        if (x > 0) enqueue(x - 1, y);
        if (x + 1 < working.Width) enqueue(x + 1, y);
        if (y > 0) enqueue(x, y - 1);
        if (y + 1 < working.Height) enqueue(x, y + 1);
      }

      int minX = working.Width, minY = working.Height, maxX = -1, maxY = -1;
      for (int y = 0; y < working.Height; ++y) {
        for (int x = 0; x < working.Width; ++x) {
          int logical = y * working.Width + x;
          int offset = y * stride + x * 4;
          if (cleared[logical]) {
            bytes[offset + 3] = 0;
          } else {
            bytes[offset + 3] = 255;
            minX = Math.Min(minX, x);
            minY = Math.Min(minY, y);
            maxX = Math.Max(maxX, x);
            maxY = Math.Max(maxY, y);
          }
        }
      }
      System.Runtime.InteropServices.Marshal.Copy(bytes, 0, data.Scan0,
                                                  byteCount);
      working.UnlockBits(data);
      if (maxX < minX || maxY < minY) throw new InvalidOperationException(
          "No aircraft pixels survived background extraction.");

      var bounds = Rectangle.FromLTRB(minX, minY, maxX + 1, maxY + 1);
      using (var output = new Bitmap(512, 512, PixelFormat.Format32bppArgb)) {
        using (var graphics = Graphics.FromImage(output)) {
          graphics.Clear(Color.Transparent);
          graphics.CompositingMode = CompositingMode.SourceCopy;
          graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
          graphics.PixelOffsetMode = PixelOffsetMode.Half;
          const int margin = 18;
          double scale = Math.Min((512.0 - margin * 2.0) / bounds.Width,
                                  (512.0 - margin * 2.0) / bounds.Height);
          int width = Math.Max(1, (int)Math.Round(bounds.Width * scale));
          int height = Math.Max(1, (int)Math.Round(bounds.Height * scale));
          int left = (512 - width) / 2;
          int top = (512 - height) / 2;
          graphics.DrawImage(working, new Rectangle(left, top, width, height),
                             bounds, GraphicsUnit.Pixel);
        }
        output.Save(outputPath, ImageFormat.Png);
      }
    }
  }
}
'@
}

$source = [Drawing.Bitmap]::FromFile($SourcePath)
try {
    if ($source.Width -ne 1254 -or $source.Height -ne 1254) {
        throw "Unexpected D2 sheet size: $($source.Width)x$($source.Height)"
    }
}
finally {
    $source.Dispose()
}

[D2PlaneExtractor]::Extract(
    $SourcePath, [Drawing.Rectangle]::new(245, 170, 360, 610),
    (Join-Path $PSScriptRoot "pet-red-d2.png"))
[D2PlaneExtractor]::Extract(
    $SourcePath, [Drawing.Rectangle]::new(660, 170, 360, 610),
    (Join-Path $PSScriptRoot "pet-blue-d2.png"))
[D2PlaneExtractor]::Extract(
    $SourcePath, [Drawing.Rectangle]::new(88, 790, 340, 205),
    (Join-Path $PSScriptRoot "pet-cloud-d2-1.png"))
[D2PlaneExtractor]::Extract(
    $SourcePath, [Drawing.Rectangle]::new(480, 805, 360, 245),
    (Join-Path $PSScriptRoot "pet-cloud-d2-2.png"))
[D2PlaneExtractor]::Extract(
    $SourcePath, [Drawing.Rectangle]::new(915, 885, 270, 165),
    (Join-Path $PSScriptRoot "pet-cloud-d2-3.png"))

Write-Host "Prepared D2 red/blue aircraft and three pixel-cloud sprites."
