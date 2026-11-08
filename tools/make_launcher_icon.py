"""Maintainer tool: draws launcher/app.ico, the launcher's own icon (KarPur's, no game artwork).

A rounded square with a diagonal gradient from violet to scarlet (the two games' colours) and a white "K", drawn with
Windows' System.Drawing through Windows PowerShell at 256, 64, 48, 32 and 16 pixels and packed into an .ico.

  .venv\\Scripts\\python tools\\make_launcher_icon.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from game_icon import pack_ico  # noqa: E402

SIZES = (256, 64, 48, 32, 16)
DRAW = r'''
Add-Type -AssemblyName System.Drawing
foreach ($n in $args[1..($args.Count - 1)]) {
  $s = [int]$n
  $bmp = New-Object System.Drawing.Bitmap $s, $s
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $r = [single]($s * 0.22)
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $w = [single]($s - 1)
  $path.AddArc(0, 0, 2 * $r, 2 * $r, 180, 90); $path.AddArc($w - 2 * $r, 0, 2 * $r, 2 * $r, 270, 90)
  $path.AddArc($w - 2 * $r, $w - 2 * $r, 2 * $r, 2 * $r, 0, 90); $path.AddArc(0, $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
  $path.CloseFigure()
  $rect = New-Object System.Drawing.RectangleF 0, 0, $s, $s
  $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, ([System.Drawing.Color]::FromArgb(139, 92, 246)), ([System.Drawing.Color]::FromArgb(229, 72, 77)), 45.0
  $g.FillPath($grad, $path)
  $font = New-Object System.Drawing.Font 'Segoe UI', ([single]($s * 0.62)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
  $fmt = New-Object System.Drawing.StringFormat
  $fmt.Alignment = [System.Drawing.StringAlignment]::Center; $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
  $box = New-Object System.Drawing.RectangleF 0, ([single](-$s * 0.03)), $s, $s
  $g.DrawString('K', $font, [System.Drawing.Brushes]::White, $box, $fmt)
  $bmp.Save((Join-Path $args[0] "$s.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
'''


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        script = tmp / 'draw.ps1'
        script.write_text(DRAW, encoding='utf-8')
        r = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script),
                            str(tmp), *map(str, SIZES)], capture_output=True)
        if r.returncode or not all((tmp / f'{n}.png').is_file() for n in SIZES):
            sys.exit(f'drawing failed: {r.stderr.decode("utf-8", "replace")[-400:]}')
        out = ROOT / 'launcher' / 'app.ico'
        pack_ico([(n, (tmp / f'{n}.png').read_bytes()) for n in SIZES], out)
        (ROOT / 'build').mkdir(exist_ok=True)
        (ROOT / 'build' / 'launcher-icon-preview.png').write_bytes((tmp / '256.png').read_bytes())
        print(f'{out}: {len(SIZES)} sizes')


if __name__ == '__main__':
    main()
