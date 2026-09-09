$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$drawingAssembly = [Drawing.Bitmap].Assembly.Location
$refs = @($drawingAssembly, [Drawing.Rectangle].Assembly.Location, [Console].Assembly.Location)
foreach ($name in @('System.Private.Windows.GdiPlus.dll','System.Private.Windows.Core.dll')) {
    $path = Join-Path (Split-Path -Parent $drawingAssembly) $name
    if (Test-Path -LiteralPath $path) { $refs += $path }
}
Add-Type -ReferencedAssemblies $refs -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
public static class DamageLiveryImport {
  static bool Protected(Color c, int y) {
    return c.A != 255 || y >= 354 || Math.Min(c.R,Math.Min(c.G,c.B)) > 120;
  }
  static double Luma(Color c) { return .2126*c.R+.7152*c.G+.0722*c.B; }
  public static void Build(string directory) {
    using (var reference = new Bitmap(System.IO.Path.Combine(directory,"feedback-source/damage-c-approved.png")))
    using (var contact = new Bitmap(768,1024))
    using (var g = Graphics.FromImage(contact)) {
      g.Clear(Color.FromArgb(7,10,19)); g.InterpolationMode=InterpolationMode.NearestNeighbor;
      for (int side=0;side<2;++side) {
        string color=side==0?"blue":"red";
        using (var master = new Bitmap(System.IO.Path.Combine(directory,"../planes/pet-"+color+"-d2.png"))) {
          g.DrawImage(master,new Rectangle(0,side*512,256,512),new Rectangle(128,0,256,512),GraphicsUnit.Pixel);
          for (int severity=1;severity<=2;++severity) {
            int changed=0,nativeChanged=0;
            using (var result=new Bitmap(master)) {
              for(int y=150;y<354;++y) for(int x=130;x<384;++x) {
                Color original=master.GetPixel(x,y);
                if(Protected(original,y)) continue;
                // Register approved sheet hull bounds to the actual D2 hull.
                int px=(int)Math.Round((x-129)*248.0/253.0);
                int py=(int)Math.Round((y-19)*318.0/335.0);
                int top=side==0?120:568;
                Color healthy=reference.GetPixel(174+px,top+py);
                Color damaged=reference.GetPixel((severity==1?642:1111)+px,top+py);
                double before=Luma(healthy), after=Luma(damaged);
                if(after>=before*.77 || before-after<15 || after>113) continue;
                // Import only actual dark scar/soot pixels, never model geometry.
                // An original saturated hull pixel must underlie every change.
                bool paint=side==0?original.B>original.R*1.3:original.R>original.G*1.3;
                if(!paint) continue;
                double amount=Math.Min(.96,Math.Max(.35,(before-after)/Math.Max(1,before)));
                result.SetPixel(x,y,Color.FromArgb(original.A,
                  (int)(original.R*(1-amount)+damaged.R*amount),
                  (int)(original.G*(1-amount)+damaged.G*amount),
                  (int)(original.B*(1-amount)+damaged.B*amount)));
                ++changed;
              }
              for(int y=0;y<512;++y) for(int x=0;x<512;++x) {
                Color a=master.GetPixel(x,y),b=result.GetPixel(x,y);
                if(a.A!=b.A || Protected(a,y)&&a.ToArgb()!=b.ToArgb()) throw new Exception("Hull/alpha/outline changed");
              }
              for(int y=0;y<36;++y) for(int x=0;x<36;++x) {
                int sx=(int)((x+.5)*512/36),sy=(int)((y+.5)*512/36);
                if(master.GetPixel(sx,sy).ToArgb()!=result.GetPixel(sx,sy).ToArgb()) ++nativeChanged;
              }
              if(changed<100 || nativeChanged<3) throw new Exception("Damage invisible at game size");
              result.Save(System.IO.Path.Combine(directory,"plane-"+color+"-hp"+(3-severity)+".png"));
              g.DrawImage(result,new Rectangle(severity*256,side*512,256,512),new Rectangle(128,0,256,512),GraphicsUnit.Pixel);
              Console.WriteLine("DAMAGE_IMPORT color="+color+" hp="+(3-severity)+" changed="+changed+" native36="+nativeChanged+" alpha_outline_unchanged=1");
            }
          }
        }
      }
      contact.Save(System.IO.Path.Combine(directory,"feedback-source/damage-c-import-check.png"));
    }
  }
}
'@
[DamageLiveryImport]::Build($PSScriptRoot)
