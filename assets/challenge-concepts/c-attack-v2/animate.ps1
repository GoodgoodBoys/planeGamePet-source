param(
    [string]$SourcePath = (Join-Path $PSScriptRoot 'c-fist-master.png'),
    [string]$ImpactPath = (Join-Path $PSScriptRoot 'impact-master.png')
)
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

public static class ChallengePreviewV2 {
  // One immutable imagegen master. Only placement and uniform scale animate.
  const int Cell = 160;
  const int Frames = 50;
  static readonly double[,] Keys = {
    // frame, x offset, y offset, square size
    {0, -5, 5, 112}, {4, -5, 5, 112},
    {10, -15, 15, 99}, {14, 12, -12, 127},
    {15, 12, -12, 127}, {23, -5, 5, 112},
    {28, -13, 13, 101}, {32, 12, -12, 127},
    {33, 12, -12, 127}, {43, -5, 5, 112},
    {49, -5, 5, 112}
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

  static void Burst(Graphics g, Bitmap impact, double cx, double cy,
                    double side, double alpha) {
    using (ImageAttributes attributes = new ImageAttributes()) {
      ColorMatrix matrix = new ColorMatrix();
      matrix.Matrix33 = (float)alpha;
      attributes.SetColorMatrix(matrix);
      Rectangle target = new Rectangle((int)Math.Round(cx - side / 2),
        (int)Math.Round(cy - side / 2), (int)Math.Round(side), (int)Math.Round(side));
      g.DrawImage(impact, target, 0, 0, impact.Width, impact.Height,
                  GraphicsUnit.Pixel, attributes);
    }
  }

  static void HitEffects(Graphics g, Bitmap impact, int frame) {
    foreach (int start in new int[] {13, 31}) {
      int age = frame - start;
      if (age < 0 || age > 6) continue;
      double[] expansion = {0.35, 0.82, 1.0, 1.08, 1.10, 0.93, 0.65};
      double[] opacity = {0.85, 1.0, 1.0, 0.95, 0.80, 0.60, 0.35};
      double size = expansion[age];
      // Bursts remain around the knuckle contour, behind the unmodified fist.
      Burst(g, impact, 128 + age * 0.3, 33 - age * 0.2, 66 * size, opacity[age]);
      Burst(g, impact, 66 - age * 0.6, 28 - age * 0.45, 32 * size, opacity[age]);
      Burst(g, impact, 139 + age * 0.5, 78 + age * 0.4, 30 * size, opacity[age]);
    }
  }

  static Bitmap Sprite(Bitmap master, Bitmap impact, double[] pose, int index) {
    Bitmap frame = new Bitmap(Cell, Cell, PixelFormat.Format32bppArgb);
    using (Graphics g = Graphics.FromImage(frame)) {
      g.Clear(Color.Transparent);
      Crisp(g);
      HitEffects(g, impact, index);
      int side = (int)Math.Round(pose[2]);
      int x = (int)Math.Round((Cell - side) / 2.0 + pose[0]);
      int y = (int)Math.Round((Cell - side) / 2.0 + pose[1]);
      g.DrawImage(master, new Rectangle(x, y, side, side),
                  new Rectangle(0, 0, master.Width, master.Height), GraphicsUnit.Pixel);
    }
    return frame;
  }

  static Bitmap OnDark(Bitmap sprite) {
    Bitmap frame = new Bitmap(Cell, Cell, PixelFormat.Format32bppArgb);
    using (Graphics g = Graphics.FromImage(frame)) {
      g.Clear(Color.FromArgb(12, 18, 29));
      Crisp(g);
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

  public static void Export(string input, string impactPath, string output) {
    string rawDir = Path.Combine(output, "frames-transparent");
    string previewDir = Path.Combine(output, "frames-preview");
    string smallDir = Path.Combine(output, "frames-48px");
    Directory.CreateDirectory(rawDir);
    Directory.CreateDirectory(previewDir);
    Directory.CreateDirectory(smallDir);
    int[] contactIndices = {0, 10, 13, 14, 16, 20, 28, 32, 35, 40, 43, 49};
    int clipped = 0;
    int minMargin = Cell;
    using (Bitmap master = new Bitmap(input))
    using (Bitmap impact = new Bitmap(impactPath))
    using (Bitmap contact = new Bitmap(960, 720, PixelFormat.Format32bppArgb))
    using (Graphics cg = Graphics.FromImage(contact)) {
      cg.Clear(Color.FromArgb(12, 18, 29));
      Crisp(cg);
      for (int i = 0; i < Frames; ++i) {
        using (Bitmap sprite = Sprite(master, impact, Pose(i), i))
        using (Bitmap preview = OnDark(sprite))
        using (Bitmap large = Scaled(preview, 480))
        using (Bitmap small = Scaled(sprite, 48)) {
          for (int y = 0; y < Cell; ++y) {
            for (int x = 0; x < Cell; ++x) {
              if (sprite.GetPixel(x, y).A < 128) continue;
              minMargin = Math.Min(minMargin,
                Math.Min(Math.Min(x, y), Math.Min(Cell - 1 - x, Cell - 1 - y)));
              if (x == 0 || y == 0 || x == Cell - 1 || y == Cell - 1)
                ++clipped;
            }
          }
          sprite.Save(Path.Combine(rawDir, i.ToString("D3") + ".png"), ImageFormat.Png);
          large.Save(Path.Combine(previewDir, i.ToString("D3") + ".png"), ImageFormat.Png);
          small.Save(Path.Combine(smallDir, i.ToString("D3") + ".png"), ImageFormat.Png);
          int index = Array.IndexOf(contactIndices, i);
          if (index >= 0) {
            cg.DrawImage(preview, new Rectangle((index % 4) * 240, (index / 4) * 240, 240, 240),
                         new Rectangle(0, 0, Cell, Cell), GraphicsUnit.Pixel);
          }
        }
      }
      contact.Save(Path.Combine(output, "keyframes.png"), ImageFormat.Png);
    }
    if (clipped != 0) throw new Exception("Fist or effect touches canvas edge: " + clipped);
    Console.WriteLine("FRAMES=50; FPS=25; LOOP_SECONDS=2.0; CLIPPED_PIXELS=0; MIN_MARGIN=" + minMargin);
  }
}
'@
[ChallengePreviewV2]::Export($SourcePath, $ImpactPath, $PSScriptRoot)
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
$last = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'frames-transparent/049.png')).Hash
if ($first -ne $last) { throw 'Loop endpoints differ' }
'LOOP_ENDPOINTS_IDENTICAL=PASS'
