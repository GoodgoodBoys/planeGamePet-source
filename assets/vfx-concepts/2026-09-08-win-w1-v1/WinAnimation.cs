using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

// Offline material exporter, derived from the existing HIT preview compositor.
// One approved sprite, immutable glyphs; no game files or services are touched.
public static class WinAnimation {
    const int Count=120, StepMs=20, Width=384, Height=160, BaseWidth=300;
    static readonly Color Background=Color.FromArgb(7,10,19);
    static double Unit(double v) { return Math.Max(0,Math.Min(1,v)); }
    static double Ease(double v) { v=Unit(v); return v*v*(3-2*v); }
    static void Crisp(Graphics g) {
        g.InterpolationMode=InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode=PixelOffsetMode.Half;
        g.SmoothingMode=SmoothingMode.None;
    }
    static bool Warm(Color c) {
        return c.R>20 && ((c.R>c.B*1.08 && c.R>=c.G*.95) ||
            (c.R>160 && c.G>160 && c.B>140));
    }
    static Bitmap Master(Bitmap source) {
        // Technical chroma-keying of the corrected navy background; retains
        // every warm fill/bevel/shadow pixel, including separated ! and holes.
        int x0=source.Width,y0=source.Height,x1=-1,y1=-1;
        for(int y=0;y<source.Height;++y) for(int x=0;x<source.Width;++x)
            if(Warm(source.GetPixel(x,y))) { x0=Math.Min(x0,x);x1=Math.Max(x1,x);y0=Math.Min(y0,y);y1=Math.Max(y1,y); }
        if(x1<x0 || y1<y0) throw new Exception("Missing WIN source lettering");
        using(var crop=new Bitmap(x1-x0+1,y1-y0+1,PixelFormat.Format32bppArgb)) {
            for(int y=0;y<crop.Height;++y) for(int x=0;x<crop.Width;++x) {
                var c=source.GetPixel(x+x0,y+y0); if(Warm(c)) crop.SetPixel(x,y,Color.FromArgb(255,c.R,c.G,c.B));
            }
            var master=new Bitmap(600,(int)Math.Round(600.0*crop.Height/crop.Width),PixelFormat.Format32bppArgb);
            using(var g=Graphics.FromImage(master)) { g.Clear(Color.Transparent); Crisp(g); g.DrawImage(crop,new Rectangle(0,0,master.Width,master.Height)); }
            return master;
        }
    }
    static double Scale(int ms) {
        if(ms<180) return 1+.12*Ease(ms/180.0);
        if(ms<700) return 1.12;
        if(ms<900) return 1+.12*(1-Ease((ms-700)/200.0));
        return 1;
    }
    static Bitmap Shine(Bitmap master,int ms,int oldHitWidth) {
        var lit=new Bitmap(master);
        if(ms<180 || ms>=700) return lit;
        // Exactly 2x the old approved HIT ribbon widths at equal word width.
        // Main 14 -> 28 units, companion 6 -> 12, whole pair 27 -> 54.
        double ratio=master.Width/(double)oldHitWidth;
        double half=14*ratio, secondHalf=6*ratio, separation=34*ratio;
        double progress=(ms-180)/520.0;
        double start=-half-separation-secondHalf-4;
        double end=master.Width+master.Height*.44+half+separation+secondHalf+4;
        double scan=start+(end-start)*progress;
        for(int y=0;y<master.Height;++y) for(int x=0;x<master.Width;++x) {
            Color c=master.GetPixel(x,y); if(c.A==0 || c.R<100 || c.G<65) continue;
            double d=x+y*.44-scan;
            double main=.96*(1-Ease((Math.Abs(d)/half-.75)/.25));
            double companion=.78*(1-Ease((Math.Abs(d+separation)/secondHalf-.7)/.3));
            double blend=Math.Max(main,companion);
            if(blend<=0) continue;
            lit.SetPixel(x,y,Color.FromArgb(c.A,
                (int)Math.Round(c.R+(255-c.R)*blend),
                (int)Math.Round(c.G+(255-c.G)*blend),
                (int)Math.Round(c.B+(233-c.B)*blend)));
        }
        return lit;
    }
    static Bitmap Render(Bitmap master,int ms,int oldHitWidth) {
        var frame=new Bitmap(Width,Height,PixelFormat.Format32bppArgb);
        using(var lit=Shine(master,ms,oldHitWidth)) using(var g=Graphics.FromImage(frame)) {
            g.Clear(Color.Transparent); Crisp(g);
            int w=(int)Math.Round(BaseWidth*Scale(ms));
            int h=(int)Math.Round(w*(double)master.Height/master.Width);
            if(w>=Width || h>=Height) throw new Exception("Clipped frame");
            g.DrawImage(lit,new Rectangle((Width-w)/2,(Height-h)/2,w,h));
        }
        return frame;
    }
    static bool Equal(Bitmap a,Bitmap b) {
        if(a.Width!=b.Width || a.Height!=b.Height) return false;
        for(int y=0;y<a.Height;++y) for(int x=0;x<a.Width;++x)
            if(a.GetPixel(x,y).ToArgb()!=b.GetPixel(x,y).ToArgb()) return false;
        return true;
    }
    public static void Export(string output,string hitPath) {
        Directory.CreateDirectory(Path.Combine(output,"rgba-frames"));
        Directory.CreateDirectory(Path.Combine(output,"preview-frames"));
        using(var source=new Bitmap(Path.Combine(output,"w1-source-corrected.png")))
        using(var oldHit=new Bitmap(hitPath))
        using(var master=Master(source))
        using(var atlas=new Bitmap(Width*10,Height*12,PixelFormat.Format32bppArgb))
        using(var ag=Graphics.FromImage(atlas)) {
            master.Save(Path.Combine(output,"win-w1-master.png"),ImageFormat.Png);
            ag.Clear(Color.Transparent); ag.CompositingMode=CompositingMode.SourceCopy; Crisp(ag);
            using(var first=Render(master,0,oldHit.Width)) {
                for(int i=0;i<Count;++i) using(var frame=Render(master,i*StepMs,oldHit.Width)) {
                    if(i>=45 && !Equal(first,frame)) throw new Exception("Hold or loop seam changes glyphs");
                    if(frame.GetPixel(0,0).A!=0 || frame.GetPixel(Width-1,Height-1).A!=0) throw new Exception("Missing real alpha");
                    string name=i.ToString("D3")+".png";
                    frame.Save(Path.Combine(output,"rgba-frames",name),ImageFormat.Png);
                    ag.DrawImageUnscaled(frame,i%10*Width,i/10*Height);
                    using(var preview=new Bitmap(Width*2,Height*2)) using(var g=Graphics.FromImage(preview)) {
                        g.Clear(Background); Crisp(g); g.DrawImage(frame,new Rectangle(0,0,preview.Width,preview.Height));
                        preview.Save(Path.Combine(output,"preview-frames",name),ImageFormat.Png);
                    }
                }
            }
            atlas.Save(Path.Combine(output,"win-w1-spritesheet.png"),ImageFormat.Png);
            int[] selected={0,9,15,22,29,35,45,119};
            using(var contact=new Bitmap(Width*4,(Height+32)*2)) using(var g=Graphics.FromImage(contact))
            using(var font=new Font("Consolas",18,FontStyle.Regular,GraphicsUnit.Pixel))
            using(var brush=new SolidBrush(Color.FromArgb(165,194,227))) {
                g.Clear(Background); Crisp(g);
                for(int j=0;j<selected.Length;++j) using(var frame=Render(master,selected[j]*StepMs,oldHit.Width)) {
                    int x=j%4*Width,y=j/4*(Height+32);
                    g.DrawImageUnscaled(frame,x,y+32);
                    g.DrawString((selected[j]*StepMs)+" ms",font,brush,x+12,y+5);
                }
                contact.Save(Path.Combine(output,"win-w1-keyframe-check.png"),ImageFormat.Png);
            }
            File.WriteAllText(Path.Combine(output,"animation.json"),
                "{\n  \"name\": \"WIN W1\", \"fps\": 50, \"frames\": 120, \"frame_ms\": 20,\n"+
                "  \"loop_ms\": 2400, \"width\": 384, \"height\": 160,\n"+
                "  \"atlas_columns\": 10, \"atlas_rows\": 12, \"order\": \"row-major\",\n"+
                "  \"scale_peak\": 1.12, \"scale_up_ms\": 180, \"shine_start_ms\": 180,\n"+
                "  \"shine_end_ms\": 700, \"return_end_ms\": 900, \"hold_ms\": 1500,\n"+
                "  \"shine_width_relative_to_previous_hit\": 2, \"old_hit_master_width\": "+oldHit.Width+",\n"+
                "  \"transparent_rgba\": true, \"first_last_identical\": true\n}\n");
        }
        Console.WriteLine("WIN_W1_FRAMES_OK frames=120 fps=50 period=2400ms scale=1.12 shine=2x hold=1500ms real_alpha=1 same_master=1 hold_and_loop_pixel_equal=1 clipping=0");
    }
}
