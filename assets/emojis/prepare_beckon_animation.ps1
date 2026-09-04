param(
    [string]$SourcePath = (Join-Path $PSScriptRoot "source\beckon-h3-inbetween-generated.png"),
    [string]$OutputSheetPath = (Join-Path $PSScriptRoot "source\beckon-h3-spritesheet.png"),
    [string]$OutputFramesDirectory = (Join-Path $PSScriptRoot "source\beckon-h3-frames"),
    [switch]$Smooth,
    [switch]$SeamlessLoop
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
using System.IO;

public static class PlanePetBeckonPrep {
  private const int FrameCount = 8;
  private const int CellSize = 256;
  private const int ContentWidth = 224;
  private const int ContentHeight = 184;
  private const int Baseline = 224;

  private static bool IsGeneratedBackground(Color pixel, bool smooth) {
    int maximum = Math.Max(pixel.R, Math.Max(pixel.G, pixel.B));
    int minimum = Math.Min(pixel.R, Math.Min(pixel.G, pixel.B));
    return smooth ? minimum >= 200 && maximum - minimum <= 80
                  : minimum >= 232 && maximum - minimum <= 20;
  }

  private static void QueueBackground(Bitmap source, int x, int y,
                                      bool smooth, bool[] erase,
                                      int[] queue, ref int tail) {
    if (x < 0 || y < 0 || x >= source.Width || y >= source.Height) return;
    int offset = y * source.Width + x;
    if (erase[offset] || !IsGeneratedBackground(source.GetPixel(x, y), smooth))
      return;
    erase[offset] = true;
    queue[tail++] = offset;
  }

  private static Bitmap MakeTransparent(Bitmap source, bool smooth) {
    Bitmap output = new Bitmap(source.Width, source.Height,
                               PixelFormat.Format32bppArgb);
    bool[] erase = null;
    if (smooth) {
      erase = new bool[source.Width * source.Height];
      int[] queue = new int[erase.Length];
      int head = 0;
      int tail = 0;
      for (int x = 0; x < source.Width; ++x) {
        QueueBackground(source, x, 0, smooth, erase, queue, ref tail);
        QueueBackground(source, x, source.Height - 1, smooth, erase, queue,
                        ref tail);
      }
      for (int y = 0; y < source.Height; ++y) {
        QueueBackground(source, 0, y, smooth, erase, queue, ref tail);
        QueueBackground(source, source.Width - 1, y, smooth, erase, queue,
                        ref tail);
      }
      while (head < tail) {
        int offset = queue[head++];
        int x = offset % source.Width;
        int y = offset / source.Width;
        QueueBackground(source, x - 1, y, smooth, erase, queue, ref tail);
        QueueBackground(source, x + 1, y, smooth, erase, queue, ref tail);
        QueueBackground(source, x, y - 1, smooth, erase, queue, ref tail);
        QueueBackground(source, x, y + 1, smooth, erase, queue, ref tail);
      }
    }
    for (int y = 0; y < source.Height; ++y) {
      for (int x = 0; x < source.Width; ++x) {
        Color pixel = source.GetPixel(x, y);
        bool remove = smooth ? erase[y * source.Width + x]
                             : IsGeneratedBackground(pixel, smooth);
        output.SetPixel(x, y, remove
                                  ? Color.FromArgb(0, 0, 0, 0)
                                  : Color.FromArgb(255, pixel.R, pixel.G,
                                                   pixel.B));
      }
    }
    return output;
  }

  private static Rectangle ContentBounds(Bitmap image, Rectangle region) {
    int left = region.Right;
    int right = region.Left - 1;
    int top = region.Bottom;
    int bottom = region.Top - 1;
    for (int y = region.Top; y < region.Bottom; ++y) {
      for (int x = region.Left; x < region.Right; ++x) {
        if (image.GetPixel(x, y).A == 0) continue;
        left = Math.Min(left, x);
        right = Math.Max(right, x);
        top = Math.Min(top, y);
        bottom = Math.Max(bottom, y);
      }
    }
    if (right < left || bottom < top)
      throw new InvalidOperationException("No sprite pixels found in frame.");
    return Rectangle.FromLTRB(left, top, right + 1, bottom + 1);
  }

  private static Rectangle[] FrameRegions(Bitmap image) {
    Rectangle[] regions = new Rectangle[FrameCount];
    int count = 0;
    int start = -1;
    for (int x = 0; x < image.Width; ++x) {
      bool occupied = false;
      for (int y = 0; y < image.Height; ++y) {
        if (image.GetPixel(x, y).A != 0) {
          occupied = true;
          break;
        }
      }
      if (occupied && start < 0) start = x;
      if (!occupied && start >= 0) {
        if (x - start >= 20) {
          if (count >= FrameCount)
            throw new InvalidOperationException("More than 8 sprites found.");
          regions[count++] = new Rectangle(start, 0, x - start, image.Height);
        }
        start = -1;
      }
    }
    if (start >= 0 && image.Width - start >= 20) {
      if (count >= FrameCount)
        throw new InvalidOperationException("More than 8 sprites found.");
      regions[count++] = new Rectangle(start, 0, image.Width - start,
                                       image.Height);
    }
    if (count != FrameCount)
      throw new InvalidOperationException(
          String.Format("Expected 8 sprites, found {0}.", count));
    return regions;
  }

  private static Bitmap RenderFrame(Bitmap transparent, Rectangle source,
                                    double scale, bool smooth) {
    Bitmap frame = new Bitmap(CellSize, CellSize, PixelFormat.Format32bppArgb);
    using (Graphics graphics = Graphics.FromImage(frame)) {
      graphics.Clear(Color.FromArgb(0, 0, 0, 0));
      graphics.CompositingMode = CompositingMode.SourceCopy;
      graphics.CompositingQuality = smooth ? CompositingQuality.HighQuality
                                           : CompositingQuality.HighSpeed;
      graphics.InterpolationMode = smooth ? InterpolationMode.HighQualityBicubic
                                          : InterpolationMode.NearestNeighbor;
      graphics.PixelOffsetMode = smooth ? PixelOffsetMode.HighQuality
                                        : PixelOffsetMode.Half;
      graphics.SmoothingMode = smooth ? SmoothingMode.HighQuality
                                      : SmoothingMode.None;

      int width = Math.Max(1, (int)Math.Round(source.Width * scale));
      int height = Math.Max(1, (int)Math.Round(source.Height * scale));
      // Anchor the fist at one fixed left edge. Centering the whole pose would
      // make the fist slide sideways as the animated finger changes width.
      int x = (CellSize - ContentWidth) / 2;
      int y = Baseline - height;
      graphics.DrawImage(transparent, new Rectangle(x, y, width, height),
                         source, GraphicsUnit.Pixel);
    }
    return frame;
  }

  public static void Export(string sourcePath, string sheetPath,
                            string framesDirectory, bool smooth,
                            bool seamlessLoop) {
    using (Bitmap source = new Bitmap(sourcePath))
    using (Bitmap transparent = MakeTransparent(source, smooth))
    using (Bitmap sheet = new Bitmap(CellSize * FrameCount, CellSize,
                                     PixelFormat.Format32bppArgb)) {
      Directory.CreateDirectory(framesDirectory);
      Rectangle[] frameRegions = FrameRegions(transparent);
      Rectangle[] contentRegions = new Rectangle[FrameCount];
      int maximumWidth = 1;
      int maximumHeight = 1;
      for (int index = 0; index < FrameCount; ++index) {
        contentRegions[index] = ContentBounds(transparent, frameRegions[index]);
        maximumWidth = Math.Max(maximumWidth, contentRegions[index].Width);
        maximumHeight = Math.Max(maximumHeight, contentRegions[index].Height);
      }
      double scale = Math.Min((double)ContentWidth / maximumWidth,
                              (double)ContentHeight / maximumHeight);
      using (Graphics sheetGraphics = Graphics.FromImage(sheet)) {
        sheetGraphics.Clear(Color.FromArgb(0, 0, 0, 0));
        sheetGraphics.CompositingMode = CompositingMode.SourceCopy;
        for (int index = 0; index < FrameCount; ++index) {
          int sourceIndex = seamlessLoop && index == FrameCount - 1 ? 0 : index;
          using (Bitmap frame = RenderFrame(
              transparent, contentRegions[sourceIndex], scale, smooth)) {
            string framePath = Path.Combine(
                framesDirectory, String.Format("beckon-{0:00}.png", index + 1));
            frame.Save(framePath, ImageFormat.Png);
            sheetGraphics.DrawImageUnscaled(frame, index * CellSize, 0);
          }
        }
      }
      sheet.Save(sheetPath, ImageFormat.Png);
    }
  }
}
'@

if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    throw "Generated beckoning-hand source artwork is missing: $SourcePath"
}

[PlanePetBeckonPrep]::Export(
    $SourcePath, $OutputSheetPath, $OutputFramesDirectory,
    [bool]$Smooth, [bool]$SeamlessLoop)

Write-Output "PLANE_PET_BECKON_ASSETS_OK frames=8 cell=256 alpha=1"
