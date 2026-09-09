param([string]$SourcePath = (Join-Path $PSScriptRoot 'c-fist-master.png'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$drawingAssembly = [Drawing.Bitmap].Assembly.Location
$drawingPrimitivesAssembly = [Drawing.Rectangle].Assembly.Location
$references = @($drawingAssembly, $drawingPrimitivesAssembly, [Console].Assembly.Location)
foreach ($name in @('System.Private.Windows.GdiPlus.dll', 'System.Private.Windows.Core.dll')) {
    $path = Join-Path (Split-Path -Parent $drawingAssembly) $name
    if (Test-Path -LiteralPath $path) { $references += $path }
}
Add-Type -ReferencedAssemblies $references -TypeDefinition @'
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

public static class ChallengePreview {
  // One immutable imagegen master. Only placement and uniform scale animate.
  const int Cell = 160;
  const int Frames = 80;
  static readonly double[,] Keys = {
    // frame, x offset, y offset, square size
    {0, -5, 5, 112}, {7, -5, 5, 112},
    {16, -15, 15, 99}, {23, 12, -12, 127},
    {25, 12, -12, 127}, {38, -5, 5, 112},
    {45, -13, 13, 101}, {52, 12, -12, 127},
    {54, 12, -12, 127}, {69, -5, 5, 112},
    {79, -5, 5, 112}
  };

  static double[] Pose(int frame) {
    int i = 0;
    while (i + 1 < Keys.GetLength(0) - 1 && frame > Keys[i + 1, 0]) ++i;
    double t = Math.Max(0, Math.Min(1,
      (frame - Keys[i, 0]) / (Keys[i + 1, 0] - Keys[i, 0])));
    // Cubic ease: zero velocity at holds and a seamless idle-to-idle loop.
    t = t * t * (3 - 2 * t);
    double[] pose = new double[3];
    for (int n = 0; n < 3; ++n)
      pose[n] = Keys[i, n + 1] + (Keys[i + 1, n + 1] - Keys[i, n + 1]) * t;
    return pose;
  }

  static void Crisp(Graphics g) {
    g.InterpolationMode = InterpolationMode.NearestNeighbor;
    g.PixelOffsetMode = PixelOffsetMode.Half;
    g.SmoothingMode = SmoothingMode.None;
  }

  static Bitmap Sprite(Bitmap master, double[] pose) {
    Bitmap frame = new Bitmap(Cell, Cell, PixelFormat.Format32bppArgb);
    using (Graphics g = Graphics.FromImage(frame)) {
      g.Clear(Color.Transparent);
      Crisp(g);
      int side = (int)Math.Round(pose[2]);
      int x = (int)Math.Round((Cell - side) / 2.0 + pose[0]);
      int y = (int)Math.Round((Cell - side) / 2.0 + pose[1]);
      g.DrawImage(master, new Rectangle(x, y, side, side),
                  new Rectangle(0, 0, master.Width, master.Height), GraphicsUnit.Pixel);
    }
    return frame;
  }

  static Bitmap Button(Bitmap sprite) {
    Bitmap frame = new Bitmap(Cell, Cell, PixelFormat.Format32bppArgb);
    using (Graphics g = Graphics.FromImage(frame)) {
      g.Clear(Color.FromArgb(12, 18, 29));
      Crisp(g);
      using (SolidBrush face = new SolidBrush(Color.FromArgb(5, 9, 16)))
        g.FillEllipse(face, 5, 5, 150, 150);
      using (Pen edge = new Pen(Color.FromArgb(38, 168, 255), 3))
        g.DrawEllipse(edge, 5, 5, 150, 150);
      g.DrawImageUnscaled(sprite, 0, 0);
    }
    return frame;
  }

  static Bitmap Scaled(Bitmap frame, int side) {
    Bitmap result = new Bitmap(side, side, PixelFormat.Format32bppArgb);
    using (Graphics g = Graphics.FromImage(result)) {
      Crisp(g);
      g.DrawImage(frame, new Rectangle(0, 0, side, side),
                  new Rectangle(0, 0, frame.Width, frame.Height), GraphicsUnit.Pixel);
    }
    return result;
  }

  public static void Export(string input, string output) {
    string rawDir = Path.Combine(output, "frames-transparent");
    string previewDir = Path.Combine(output, "frames-preview");
    string smallDir = Path.Combine(output, "frames-48px");
    Directory.CreateDirectory(rawDir);
    Directory.CreateDirectory(previewDir);
    Directory.CreateDirectory(smallDir);
    int[] contactIndices = {0, 16, 20, 23, 31, 38, 45, 49, 52, 61, 69, 79};
    int clipped = 0;
    double maxRadius = 0;
    using (Bitmap master = new Bitmap(input))
    using (Bitmap contact = new Bitmap(960, 720, PixelFormat.Format32bppArgb))
    using (Graphics cg = Graphics.FromImage(contact)) {
      cg.Clear(Color.FromArgb(12, 18, 29));
      Crisp(cg);
      for (int i = 0; i < Frames; ++i) {
        using (Bitmap sprite = Sprite(master, Pose(i)))
        using (Bitmap button = Button(sprite))
        using (Bitmap large = Scaled(button, 480))
        using (Bitmap small = Scaled(button, 48)) {
          for (int y = 0; y < Cell; ++y) {
            for (int x = 0; x < Cell; ++x) {
              if (sprite.GetPixel(x, y).A < 128) continue;
              double radius = Math.Sqrt((x - 80.0) * (x - 80.0) +
                                        (y - 80.0) * (y - 80.0));
              maxRadius = Math.Max(maxRadius, radius);
              if (radius >= 72 || x == 0 || y == 0 || x == Cell - 1 || y == Cell - 1)
                ++clipped;
            }
          }
          sprite.Save(Path.Combine(rawDir, i.ToString("D3") + ".png"), ImageFormat.Png);
          large.Save(Path.Combine(previewDir, i.ToString("D3") + ".png"), ImageFormat.Png);
          small.Save(Path.Combine(smallDir, i.ToString("D3") + ".png"), ImageFormat.Png);
          int index = Array.IndexOf(contactIndices, i);
          if (index >= 0) {
            cg.DrawImage(button, new Rectangle((index % 4) * 240, (index / 4) * 240, 240, 240),
                         new Rectangle(0, 0, Cell, Cell), GraphicsUnit.Pixel);
          }
        }
      }
      contact.Save(Path.Combine(output, "keyframes.png"), ImageFormat.Png);
    }
    if (clipped != 0) throw new Exception("Fist overlaps button edge: " + clipped);
    Console.WriteLine("FRAMES=80; FPS=25; LOOP_SECONDS=3.2; CLIPPED_PIXELS=0; MAX_RADIUS=" + maxRadius);
  }
}
'@
[ChallengePreview]::Export($SourcePath, $PSScriptRoot)
$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
foreach ($pair in @(
    @('frames-preview', 'c-attack-preview.gif'),
    @('frames-48px', 'c-attack-48px.gif'),
    @('frames-transparent', 'c-attack-transparent.gif')
)) {
    & $ffmpeg -hide_banner -loglevel error -y -framerate 25 -i (Join-Path $PSScriptRoot ($pair[0] + '/%03d.png')) `
      -filter_complex '[0:v]split[a][b];[a]palettegen=stats_mode=full:reserve_transparent=1[p];[b][p]paletteuse=dither=none:alpha_threshold=128' `
      -loop 0 (Join-Path $PSScriptRoot $pair[1])
    if ($LASTEXITCODE -ne 0) { throw ('GIF encoding failed: ' + $pair[1]) }
}
$first = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'frames-transparent/000.png')).Hash
$last = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'frames-transparent/079.png')).Hash
if ($first -ne $last) { throw 'Loop endpoints differ' }
'LOOP_ENDPOINTS_IDENTICAL=PASS'
