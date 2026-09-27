# Проверка интерфейса десктопного клиента в демо-режиме: скриншот каждого раздела + анализ пикселей.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe  = Join-Path $root 'orangem.exe'
if (-not (Test-Path $exe)) { throw "нет $exe" }

Add-Type -AssemblyName System.Drawing
$src = @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
try { Add-Type -TypeDefinition $src -ErrorAction Stop } catch { Write-Output "FAIL P/Invoke: $($_.Exception.Message)"; exit 1 }

function Analyze($bmp, $w, $h) {
  $orange = 0; $text = 0; $panel = 0; $sidebar = 0; $colors = @{}
  $step = 3
  for ($y = 0; $y -lt $h; $y += $step) {
    for ($x = 0; $x -lt $w; $x += $step) {
      $c = $bmp.GetPixel($x, $y); $colors["$($c.R),$($c.G),$($c.B)"] = 1
      if ($c.R -gt 190 -and $c.G -gt 90 -and $c.G -lt 200 -and $c.B -lt 110 -and ($c.R - $c.B) -gt 90) { $orange++ }
      if ($c.R -gt 200 -and $c.G -gt 200 -and $c.B -gt 200) { $text++ }
      if ($x -lt 232 -and [Math]::Abs($c.R - 17) -lt 12 -and [Math]::Abs($c.G - 21) -lt 12 -and [Math]::Abs($c.B - 27) -lt 12) { $sidebar++ }
      if ($x -gt 560 -and [Math]::Abs($c.R - 21) -lt 12 -and [Math]::Abs($c.G - 26) -lt 12 -and [Math]::Abs($c.B - 33) -lt 12) { $panel++ }
    }
  }
  return @{ orange = $orange; text = $text; sidebar = $sidebar; panel = $panel; colors = $colors.Count }
}

$sections = @('chats','feed','communities','stories','profile')
$allOk = $true
foreach ($sec in $sections) {
  $p = Start-Process -FilePath $exe -ArgumentList @('--demo', "--section=$sec") -PassThru
  Start-Sleep -Seconds 4
  $p.Refresh()
  if ($p.HasExited) { Write-Output "FAIL [$sec] процесс упал"; $allOk = $false; continue }
  $h = $p.MainWindowHandle
  if ($h -eq [IntPtr]::Zero) { Write-Output "FAIL [$sec] окно не найдено"; $p.Kill(); $allOk = $false; continue }

  $r = New-Object W+RECT
  [void][W]::GetWindowRect($h, [ref]$r)
  $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc(); [void][W]::PrintWindow($h, $dc, 0); $g.ReleaseHdc($dc); $g.Dispose()
  $png = Join-Path $root "screenshot-$sec.png"
  $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)

  $a = Analyze $bmp $w $ht
  $bmp.Dispose()

  Write-Output ("--- [{0}] окно {1}x{2}, цветов {3}, оранжевых {4}, светлых {5}, сайдбар {6}, панель {7}" -f `
    $sec, $w, $ht, $a.colors, $a.orange, $a.text, $a.sidebar, $a.panel)
  $ok = $true
  if ($w -lt 900 -or $ht -lt 600) { Write-Output "FAIL [$sec] окно не развёрнуто"; $ok = $false }
  if ($a.colors -lt 60) { Write-Output "FAIL [$sec] интерфейс почти не отрисован"; $ok = $false }
  if ($a.text -lt 100) { Write-Output "FAIL [$sec] текст не найден"; $ok = $false }
  if ($a.orange -lt 40) { Write-Output "FAIL [$sec] оранжевый акцент отсутствует"; $ok = $false }
  if ($a.sidebar -lt 200) { Write-Output "FAIL [$sec] сайдбар не отрисован"; $ok = $false }
  if ($a.panel -lt 100) { Write-Output "FAIL [$sec] рабочая область не отрисована"; $ok = $false }
  if ($ok) { Write-Output "PASS [$sec] интерфейс отрисован корректно" } else { $allOk = $false }

  [void]$p.CloseMainWindow()
  Start-Sleep -Seconds 1
  if (-not $p.HasExited) { $p.Kill() }
}

if ($allOk) { Write-Output '== GUI DEMO: OK (все разделы) =='; exit 0 } else { Write-Output '== GUI DEMO: FAIL =='; exit 1 }
