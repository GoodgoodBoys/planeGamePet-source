param()
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop') {
    & "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
    if ($LASTEXITCODE -ne 0) { throw 'Effect atlas import failed.' }
    return
}
Add-Type -AssemblyName System.Drawing
# Deterministic game import: convert imagegen's keyed sheets to RGBA, align
# cells, and nearest-neighbour downsample. No cloud/flame artwork is redrawn.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
public static class EffectAtlasImport {
  static Color Key(Color c, bool magenta) {
    if (!magenta) {
      // Approved hit sheet is on navy, and all effect pixels are warm/white.
      return c.R < 42 && c.G < 52 && c.B < 90 ? Color.Transparent : c;
    }
    double key = Math.Max(0, Math.Min(c.R, c.B) - c.G);
    if (key > 180 && c.G < 75) return Color.Transparent;
    if (key < 12) return c;
    double a = 1 - key / 255.0;
    if (a < .15) return Color.Transparent;
    int r = (int)Math.Round((c.R - 255 * (1 - a)) / a);
    int g = (int)Math.Round(c.G / a);
    int b = (int)Math.Round((c.B - 255 * (1 - a)) / a);
    return Color.FromArgb((int)(255*a), Math.Max(0,Math.Min(255,r)), Math.Max(0,Math.Min(255,g)), Math.Max(0,Math.Min(255,b)));
  }
  public static void Import(string input, string output, int columns, int rows, int cell, bool magenta, int[] origins) {
    using (Bitmap source = new Bitmap(input))
    using (Bitmap atlas = new Bitmap(columns*cell, rows*cell, PixelFormat.Format32bppArgb)) {
      for (int i=0;i<columns*rows;i++) {
        int crop = origins == null ? source.Width/columns : 320;
        int x = origins == null ? i%columns*crop : origins[i*2]-crop/2;
        int y = origins == null ? i/columns*crop : origins[i*2+1]-crop/2;
        using (Bitmap keyed = new Bitmap(crop,crop,PixelFormat.Format32bppArgb)) {
          int left=crop,top=crop,right=0,bottom=0;
          for(int yy=0;yy<crop;yy++) for(int xx=0;xx<crop;xx++) {
            if(x+xx<0||y+yy<0||x+xx>=source.Width||y+yy>=source.Height) continue;
            Color c=Key(source.GetPixel(x+xx,y+yy),magenta);
            keyed.SetPixel(xx,yy,c);
            if(c.A>32) {left=Math.Min(left,xx);top=Math.Min(top,yy);right=Math.Max(right,xx+1);bottom=Math.Max(bottom,yy+1);}
          }
          Rectangle bounds = new Rectangle(0,0,crop,crop);
          if(columns==3 && right>left) {
            int size=Math.Min(crop,Math.Max(right-left,bottom-top)+48);
            bounds=new Rectangle(Math.Max(0,Math.Min(crop-size,(left+right-size)/2)), Math.Max(0,Math.Min(crop-size,(top+bottom-size)/2)),size,size);
          }
          using(Graphics g=Graphics.FromImage(atlas)) {
            g.CompositingMode=CompositingMode.SourceCopy;
            g.InterpolationMode=InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode=PixelOffsetMode.Half;
            g.DrawImage(keyed,new Rectangle(i%columns*cell,i/columns*cell,cell,cell),bounds,GraphicsUnit.Pixel);
          }
        }
      }
      atlas.Save(output,ImageFormat.Png);
    }
  }
}
'@
$effectSources = Join-Path $PSScriptRoot 'source'
$effectHit = Join-Path $PSScriptRoot '..\hit-concepts\2026-09-08\12-frame-v1\hit-12-registered.png'
[EffectAtlasImport]::Import($effectHit, (Join-Path $PSScriptRoot 'hit-12.png'), 4, 3, 32, $false, $null)
$effectOrigins = [int[]]@(173,171, 533,174, 896,172, 1235,167, 173,511, 533,510, 882,514, 1232,514, 173,855, 533,855, 882,855, 1232,855)
[EffectAtlasImport]::Import((Join-Path $effectSources 'explosion-b-keyed.png'), (Join-Path $PSScriptRoot 'explosion-b-12.png'), 4, 3, 64, $true, $effectOrigins)
[EffectAtlasImport]::Import((Join-Path $effectSources 'smoke-b-keyed.png'), (Join-Path $PSScriptRoot 'smoke-b-6.png'), 3, 2, 32, $true, $null)
'EFFECT_ATLAS_IMPORT_OK hit=12 explosion=12 smoke=6'
