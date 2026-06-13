# Build far-file.ico — true orange #F97316 (PNG-embedded ICO keeps color in Explorer).
#Requires -Version 5.1
$ErrorActionPreference = "Stop"

$OutDir = Split-Path -Parent $PSScriptRoot
$IcoPath = Join-Path $OutDir "icons\far-file.ico"
$PngPath = Join-Path $OutDir "icons\far-file.png"

Add-Type -AssemblyName System.Drawing

# Balanced orange: enough green to read as orange, not yellow or red (#F97316)
$BrandOrange = [System.Drawing.Color]::FromArgb(255, 0xFF, 0x9F, 0x43)
$BrandLetter = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xFF, 0xFF)

function New-FarBitmap([int]$size) {
  $bmp = New-Object System.Drawing.Bitmap $size, $size
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
  $g.Clear([System.Drawing.Color]::Transparent)

  $margin = [int]($size * 0.08)
  $radius = [int]($size * 0.2)
  $rect = New-Object System.Drawing.Rectangle $margin, $margin, ($size - 2 * $margin), ($size - 2 * $margin)

  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = $radius * 2
  $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
  $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
  $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
  $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
  $path.CloseFigure()
  $g.FillPath((New-Object System.Drawing.SolidBrush $BrandOrange), $path)

  $fontSize = [single]($size * 0.52)
  $font = [System.Drawing.Font]::new('Segoe UI', $fontSize, [System.Drawing.FontStyle]::Bold)
  $sf = New-Object System.Drawing.StringFormat
  $sf.Alignment = [System.Drawing.StringAlignment]::Center
  $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
  $rectF = New-Object System.Drawing.RectangleF ([single]$margin), ([single]$margin), ([single]($size - 2 * $margin)), ([single]($size - 2 * $margin))
  $g.DrawString('F', $font, (New-Object System.Drawing.SolidBrush $BrandLetter), $rectF, $sf)

  $font.Dispose()
  $g.Dispose()
  return $bmp
}

Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
public static class FarIcoWriter {
  public static void WritePngIco(string path, Bitmap[] bitmaps) {
    using (var fs = File.Open(path, FileMode.Create))
    using (var bw = new BinaryWriter(fs)) {
      bw.Write((short)0);
      bw.Write((short)1);
      bw.Write((short)bitmaps.Length);
      var offset = 6 + 16 * bitmaps.Length;
      var pngs = new List<byte[]>();
      foreach (var bmp in bitmaps) {
        using (var ms = new MemoryStream()) {
          bmp.Save(ms, ImageFormat.Png);
          pngs.Add(ms.ToArray());
        }
      }
      for (int i = 0; i < bitmaps.Length; i++) {
        int w = bitmaps[i].Width;
        int h = bitmaps[i].Height;
        bw.Write((byte)(w >= 256 ? 0 : w));
        bw.Write((byte)(h >= 256 ? 0 : h));
        bw.Write((byte)0);
        bw.Write((byte)0);
        bw.Write((short)1);
        bw.Write((short)32);
        bw.Write(pngs[i].Length);
        bw.Write(offset);
        offset += pngs[i].Length;
      }
      foreach (var png in pngs) bw.Write(png);
    }
  }
}
"@

New-Item -ItemType Directory -Path (Split-Path $IcoPath) -Force | Out-Null

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$bitmaps = New-Object System.Collections.Generic.List[System.Drawing.Bitmap]
foreach ($s in $sizes) { $bitmaps.Add((New-FarBitmap $s)) }

$bitmaps[$bitmaps.Count - 1].Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
[FarIcoWriter]::WritePngIco($IcoPath, $bitmaps.ToArray())

foreach ($bmp in $bitmaps) { $bmp.Dispose() }

Write-Host "Created: $IcoPath"
