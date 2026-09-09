$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop') {
    & "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
    if ($LASTEXITCODE -ne 0) { throw 'Result title import failed' }
    return
}
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
public static class ResultTitleImport {
    static void Crisp(Graphics g) {
        g.CompositingMode=CompositingMode.SourceCopy;
        g.InterpolationMode=InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode=PixelOffsetMode.Half;
        g.SmoothingMode=SmoothingMode.None;
    }
    static bool Ink(Color c) {
        return c.R>20 && ((c.R>c.B*1.08 && c.R>=c.G*.95) ||
            (c.R>160 && c.G>160 && c.B>140));
    }
    public static void Run(string output) {
        string approved=Path.GetFullPath(Path.Combine(output,"../vfx-concepts/2026-09-08-win-w1-v1"));
        using(var atlas=new Bitmap(1920,960,PixelFormat.Format32bppArgb))
        using(var g=Graphics.FromImage(atlas)) {
            g.Clear(Color.Transparent); Crisp(g);
            for(int i=0;i<120;++i) using(var frame=new Bitmap(Path.Combine(approved,"rgba-frames",i.ToString("D3")+".png"))) {
                if(frame.Width!=384 || frame.Height!=160) throw new Exception("Unexpected approved frame dimensions");
                g.DrawImage(frame,new Rectangle(i%10*192,i/10*80,192,80),0,0,384,160,GraphicsUnit.Pixel);
            }
            for(int i=45;i<120;++i) for(int y=0;y<80;++y) for(int x=0;x<192;++x)
                if(atlas.GetPixel(x,y).ToArgb()!=atlas.GetPixel(i%10*192+x,i/10*80+y).ToArgb())
                    throw new Exception("Runtime hold/loop mismatch");
            atlas.Save(Path.Combine(output,"win-w1-120.png"),ImageFormat.Png);
        }
        using(var sheet=new Bitmap(Path.Combine(approved,"approved-lost-options.png"))) {
            // Exact selected L1 large specimen. Only remove navy backing; no
            // new typography, recolouring, generated letter shapes or motion.
            Rectangle area=new Rectangle(20,335,488,212);
            int x0=area.Right,y0=area.Bottom,x1=0,y1=0;
            for(int y=area.Top;y<area.Bottom;++y) for(int x=area.Left;x<area.Right;++x)
                if(Ink(sheet.GetPixel(x,y))) { x0=Math.Min(x0,x);y0=Math.Min(y0,y);x1=Math.Max(x1,x);y1=Math.Max(y1,y); }
            if(x1-x0<400 || y1-y0<100) throw new Exception("L1 specimen missing or truncated");
            using(var master=new Bitmap(x1-x0+1,y1-y0+1,PixelFormat.Format32bppArgb)) {
                for(int y=0;y<master.Height;++y) for(int x=0;x<master.Width;++x) {
                    Color c=sheet.GetPixel(x0+x,y0+y); if(Ink(c)) master.SetPixel(x,y,c);
                }
                master.Save(Path.Combine(output,"lost-l1-master.png"),ImageFormat.Png);
                using(var card=new Bitmap(192,80,PixelFormat.Format32bppArgb))
                using(var g=Graphics.FromImage(card)) {
                    g.Clear(Color.Transparent); Crisp(g);
                    int height=(int)Math.Round(150.0*master.Height/master.Width);
                    if(height>70) throw new Exception("L1 would overlap result reason");
                    g.DrawImage(master,new Rectangle(21,(80-height)/2,150,height));
                    card.Save(Path.Combine(output,"lost-l1.png"),ImageFormat.Png);
                }
            }
        }
        Console.WriteLine("RESULT_ASSETS_OK win=120frames/192x80/2400ms hold_loop_equal=1 lost=L1-static exact_source=1 transparent=1");
    }
}
'@
[ResultTitleImport]::Run($PSScriptRoot)
