using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

// Offline preview compositor only. No game/network files are consumed or changed.
// The approved raster art is reused; animation changes transforms/opacity only.
public static class ImpactAnimationPreview {
    const int Count = 100;
    const int StepMs = 20;
    static readonly Color Backdrop = Color.FromArgb(16, 22, 34);
    static int pieceChecks;
    static int clipped;

    static bool Ink(Color c, int kind) {
        if (kind == 0) return c.B > 100 && c.G > 60 && c.B > c.R * 1.45 || c.R > 170 && c.G > 180 && c.B > 180;
        if (kind == 1) return c.R > 100 && c.R > c.G * 1.45 && c.R > c.B * 1.25 || c.R > 185 && c.G > 160 && c.B > 160;
        return c.R > 48 && c.R > c.G * 1.10 && c.G > c.B * 1.12;
    }

    static Bitmap Extract(Bitmap sheet, Rectangle rect, int kind, int outline) {
        var result = new Bitmap(rect.Width, rect.Height, PixelFormat.Format32bppArgb);
        var mask = new bool[rect.Width, rect.Height];
        for (int y = 0; y < rect.Height; ++y)
            for (int x = 0; x < rect.Width; ++x)
                mask[x, y] = Ink(sheet.GetPixel(rect.X + x, rect.Y + y), kind);
        for (int y = 0; y < rect.Height; ++y) {
            for (int x = 0; x < rect.Width; ++x) {
                bool keep = mask[x, y];
                for (int dy = -outline; !keep && dy <= outline; ++dy)
                    for (int dx = -outline; !keep && dx <= outline; ++dx) {
                        int sx = x + dx, sy = y + dy;
                        if (sx >= 0 && sy >= 0 && sx < rect.Width && sy < rect.Height && mask[sx, sy]) keep = true;
                    }
                if (keep) result.SetPixel(x, y, sheet.GetPixel(rect.X + x, rect.Y + y));
            }
        }
        return result;
    }

    // Flood outside the gray outline, preserving its dark filled interior and notch.
    static Bitmap ExtractSlot(Bitmap sheet) {
        var r = new Rectangle(1346, 264, 160, 142);
        int w = r.Width, h = r.Height;
        var wall = new bool[w, h];
        var outside = new bool[w, h];
        var queue = new int[w * h];
        int head = 0, tail = 0;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            Color c = sheet.GetPixel(r.X + x, r.Y + y);
            wall[x, y] = c.R > 45 && c.G > 50 && c.B - c.R < 80;
        }
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && !wall[x, y] && !outside[x, y]) {
                outside[x, y] = true; queue[tail++] = y * w + x;
            }
        int[] xs = {1, -1, 0, 0}, ys = {0, 0, 1, -1};
        while (head < tail) {
            int index = queue[head++], x = index % w, y = index / w;
            for (int k = 0; k < 4; ++k) {
                int nx = x + xs[k], ny = y + ys[k];
                if (nx >= 0 && nx < w && ny >= 0 && ny < h && !outside[nx, ny] && !wall[nx, ny]) {
                    outside[nx, ny] = true; queue[tail++] = ny * w + nx;
                }
            }
        }
        var slot = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            if (!outside[x, y]) slot.SetPixel(x, y, sheet.GetPixel(r.X + x, r.Y + y));
        return slot;
    }

    // Read the existing zigzag seam in approved frame04, not a new random fracture.
    static int[] TraceCrack(Bitmap sheet, int width, int height) {
        var cut = new int[height];
        int changes = 0;
        for (int y = 0; y < height; ++y) {
            int sy = 272 + y, center = 707, best = center, bestDist = 100;
            for (int x = center - 15; x <= center + 15; ++x) {
                if (Ink(sheet.GetPixel(x, sy), 0)) continue;
                int end = x;
                while (end < center + 18 && !Ink(sheet.GetPixel(end, sy), 0)) ++end;
                if (end - x < 15 && Ink(sheet.GetPixel(x - 1, sy), 0) && Ink(sheet.GetPixel(end, sy), 0)) {
                    int mid = (x + end) / 2, distance = Math.Abs(mid - center);
                    if (distance < bestDist) { best = mid; bestDist = distance; }
                }
                x = end;
            }
            cut[y] = width / 2 + best - center;
            if (y > 0 && cut[y] != cut[y - 1]) ++changes;
        }
        if (changes < 8) throw new Exception("Fracture trace is unexpectedly flat");
        return cut;
    }

    static Bitmap[] Split(Bitmap master, int[] cut) {
        var pieces = new [] {new Bitmap(master.Width, master.Height), new Bitmap(master.Width, master.Height)};
        for (int y = 0; y < master.Height; ++y) for (int x = 0; x < master.Width; ++x) {
            Color original = master.GetPixel(x, y);
            if (original.A == 0) continue;
            int side = x < cut[y] ? 0 : 1;
            pieces[side].SetPixel(x, y, original);
            if (pieces[side].GetPixel(x, y).ToArgb() != original.ToArgb() || pieces[1-side].GetPixel(x, y).A != 0)
                throw new Exception("Halves do not reconstruct source exactly");
            ++pieceChecks;
        }
        return pieces;
    }

    static void Crisp(Graphics g) {
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        g.SmoothingMode = SmoothingMode.None;
    }

    static void Draw(Graphics g, Bitmap b, double cx, double cy, double width, double opacity) {
        if (opacity <= 0) return;
        int w = (int)Math.Round(width), h = (int)Math.Round(width * b.Height / b.Width);
        var target = new Rectangle((int)Math.Round(cx - w / 2.0), (int)Math.Round(cy - h / 2.0), w, h);
        if (target.Top < 0 || target.Left < 0 || target.Right > 640 || target.Bottom > 400) ++clipped;
        using (var ia = new ImageAttributes()) {
            var cm = new ColorMatrix(); cm.Matrix33 = (float)opacity; ia.SetColorMatrix(cm);
            g.DrawImage(b, target, 0, 0, b.Width, b.Height, GraphicsUnit.Pixel, ia);
        }
    }

    static double Clamp(double v) { return Math.Max(0, Math.Min(1, v)); }
    static double Ease(double v) { v = Clamp(v); return v * v * (3 - 2 * v); }

    static void Label(Graphics g, string text, float x, float y, Color color, float size) {
        using (var f = new Font("Microsoft YaHei UI", size, FontStyle.Regular, GraphicsUnit.Pixel))
        using (var b = new SolidBrush(color)) g.DrawString(text, f, b, x, y);
    }

    static void Heart(Graphics g, Bitmap master, Bitmap[] halves, Bitmap slot, int[] cut, int ms, int cx) {
        const int cy = 170, size = 154;
        if (ms < 300) { Draw(g, master, cx, cy, size, 1); return; }
        Draw(g, slot, cx, cy, size, 1);
        if (ms < 420) {
            using (var cracking = new Bitmap(master)) {
                int reach = (int)(master.Height * Clamp((ms - 300) / 120.0));
                for (int y = 0; y < reach; ++y) for (int x = cut[y] - 2; x <= cut[y] + 1; ++x)
                    if (x >= 0 && x < master.Width && cracking.GetPixel(x, y).A > 0)
                        cracking.SetPixel(x, y, Color.FromArgb(255, 8, 17, 30));
                Draw(g, cracking, cx, cy, size, 1);
            }
            return;
        }
        if (ms >= 1060) return;
        double separation = 10 * Ease((ms - 420) / 80.0);
        double fall = ms < 800 ? 0 : Clamp((ms - 800) / 260.0);
        double dy = 105 * fall * fall;
        double opacity = 1 - fall;
        // The same two immutable sprites are reused in all split/hold/fall frames.
        Draw(g, halves[0], cx - separation, cy + dy, size, opacity);
        Draw(g, halves[1], cx + separation, cy + dy, size, opacity);
    }

    static Bitmap Shine(Bitmap master, double sweep) {
        var result = new Bitmap(master);
        if (sweep < 0 || sweep > 1) return result;
        double start = -master.Height * 0.44 - 25;
        double end = master.Width + 35;
        double position = start + (end - start) * sweep;
        for (int y = 0; y < master.Height; ++y) for (int x = 0; x < master.Width; ++x) {
            Color c = master.GetPixel(x, y);
            if (c.A == 0 || c.R < 100 || c.G < 65) continue;
            double d = x + y * 0.44 - position;
            double blend = Math.Abs(d) < 7 ? 0.96 : Math.Abs(d + 17) < 3 ? 0.78 : 0;
            if (blend > 0) result.SetPixel(x, y, Color.FromArgb(c.A,
                (int)(c.R + (255 - c.R) * blend), (int)(c.G + (255 - c.G) * blend), (int)(c.B + (233 - c.B) * blend)));
        }
        return result;
    }

    static double HitScale(int ms) {
        if (ms < 380) return 0.70 + 0.55 * Ease((ms - 300) / 80.0);
        if (ms < 700) return 1.25;
        if (ms < 780) return 1.25 - 0.15 * Ease((ms - 700) / 80.0);
        return 1.10;
    }

    static void Hit(Graphics g, Bitmap master, int ms) {
        if (ms < 300 || ms >= 1180) return;
        double scale = HitScale(ms), opacity = ms < 780 ? 1 : 1 - Ease((ms - 780) / 400.0);
        double sweep = ms >= 380 && ms < 660 ? (ms - 380) / 280.0 : -1;
        using (var lit = Shine(master, sweep)) {
            Draw(g, lit, 244, 79, 51.2 * scale, opacity);
            Draw(g, lit, 320, 246, 236 * scale, opacity);
        }
    }

    static void Contact(string output, string name, int[] indices) {
        using (var sheet = new Bitmap(1280, ((indices.Length + 3) / 4) * 200))
        using (var g = Graphics.FromImage(sheet)) {
            g.Clear(Backdrop); Crisp(g);
            for (int i = 0; i < indices.Length; ++i) using (var f = new Bitmap(Path.Combine(output, name + "-frames", indices[i].ToString("D3") + ".png"))) {
                g.DrawImage(f, new Rectangle(i % 4 * 320, i / 4 * 200, 320, 200));
                Label(g, (indices[i] * StepMs) + " ms", i % 4 * 320 + 8, i / 4 * 200 + 3, Color.LightGray, 13);
            }
            sheet.Save(Path.Combine(output, name + "-contact.png"));
        }
    }

    public static void Export(string output) {
        Directory.CreateDirectory(Path.Combine(output, "heart-break-frames"));
        Directory.CreateDirectory(Path.Combine(output, "hit-orange-frames"));
        using (var heartSheet = new Bitmap(Path.Combine(output, "approved-heart-keyframes.png")))
        using (var hitSheet = new Bitmap(Path.Combine(output, "approved-hit-keyframes.png")))
        using (var hud = new Bitmap(Path.Combine(output, "hud-reference.bmp")))
        using (var blue = Extract(heartSheet, new Rectangle(109, 269, 154, 138), 0, 1))
        using (var red = Extract(heartSheet, new Rectangle(109, 675, 154, 138), 1, 1))
        using (var slot = ExtractSlot(heartSheet))
        using (var hit = Extract(hitSheet, new Rectangle(426, 440, 314, 118), 2, 1)) {
            int[] cut = TraceCrack(heartSheet, blue.Width, blue.Height);
            Bitmap[] blueHalves = Split(blue, cut), redHalves = Split(red, cut);
            try {
                blue.Save(Path.Combine(output, "blue-master.png")); red.Save(Path.Combine(output, "red-master.png"));
                slot.Save(Path.Combine(output, "gray-slot-master.png")); hit.Save(Path.Combine(output, "hit-master.png"));
                for (int side = 0; side < 2; ++side) {
                    blueHalves[side].Save(Path.Combine(output, "blue-half-" + side + ".png"));
                    redHalves[side].Save(Path.Combine(output, "red-half-" + side + ".png"));
                }
                for (int i = 0; i < Count; ++i) {
                    int ms = i * StepMs;
                    using (var f = new Bitmap(640, 400, PixelFormat.Format24bppRgb))
                    using (var g = Graphics.FromImage(f)) {
                        g.Clear(Backdrop); Crisp(g);
                        Label(g, "蓝色爱心", 148, 39, Color.FromArgb(92, 203, 255), 23);
                        Label(g, "红色爱心", 408, 39, Color.FromArgb(255, 118, 132), 23);
                        Heart(g, blue, blueHalves, slot, cut, ms, 194);
                        Heart(g, red, redHalves, slot, cut, ms, 454);
                        f.Save(Path.Combine(output, "heart-break-frames", i.ToString("D3") + ".png"));
                    }
                    using (var f = new Bitmap(640, 400, PixelFormat.Format24bppRgb))
                    using (var g = Graphics.FromImage(f)) {
                        g.Clear(Backdrop); Crisp(g);
                        g.DrawImage(hud, new Rectangle(80, 35, 480, 104), new Rectangle(0, 0, 240, 52), GraphicsUnit.Pixel);
                        Label(g, "HIT! 局部放大预览", 231, 166, Color.FromArgb(150, 162, 181), 19);
                        Hit(g, hit, ms);
                        f.Save(Path.Combine(output, "hit-orange-frames", i.ToString("D3") + ".png"));
                    }
                }
            } finally { foreach (var b in blueHalves) b.Dispose(); foreach (var b in redHalves) b.Dispose(); }
        }
        // Validate exact hold reuse: no drift, fading or shape change during stage05.
        byte[] hold = File.ReadAllBytes(Path.Combine(output, "heart-break-frames", "025.png"));
        for (int i = 26; i <= 40; ++i) {
            byte[] other = File.ReadAllBytes(Path.Combine(output, "heart-break-frames", i.ToString("D3") + ".png"));
            if (hold.Length != other.Length) throw new Exception("Stage05 hold changed");
            for (int n = 0; n < hold.Length; ++n) if (hold[n] != other[n]) throw new Exception("Stage05 hold drift");
        }
        if (clipped != 0) throw new Exception("Preview sprite clipped: " + clipped);
        for (int ms = 380; ms < 700; ms += 20) if (HitScale(ms) != 1.25) throw new Exception("Shrank before sweep completed");
        Contact(output, "heart-break", new [] {14, 17, 20, 23, 30, 44, 48, 53});
        Contact(output, "hit-orange", new [] {15, 19, 23, 27, 31, 34, 39, 51});
        Console.WriteLine("QA_PASS: immutable half sprites; exact reconstruction pixels=" + pieceChecks + "; stage05 hold identical; zero clipping; shine-before-shrink");
    }
}
