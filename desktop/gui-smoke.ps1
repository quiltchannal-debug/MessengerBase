# Дымовой тест GUI OrangeM для Windows: запуск, проверка окна входа, скриншот, закрытие.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'orangem.exe'
if (-not (Test-Path $exe)) { throw "нет $exe" }

Add-Type -AssemblyName System.Drawing
$src = @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$hasWin32 = $true
try { Add-Type -TypeDefinition $src -ErrorAction Stop } catch { $hasWin32 = $false; Write-Output "P/Invoke недоступен: $($_.Exception.Message)" }

$proc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 4
$proc.Refresh()
$ok = $true

if ($proc.HasExited) {
  Write-Output ("FAIL процесс завершился, код " + $proc.ExitCode)
  exit 1
}
Write-Output ("PASS процесс запущен, PID " + $proc.Id)

$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output 'FAIL главное окно не найдено'; $ok = $false }
else {
  Write-Output ("PASS окно найдено: '" + $proc.MainWindowTitle + "'")
  if ($hasWin32) {
    $r = New-Object W+RECT
    if ([W]::GetWindowRect($h, [ref]$r)) {
      $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
      Write-Output ("PASS размер окна ${w}x${ht}")
      if ($w -lt 380 -or $ht -lt 360) { Write-Output 'FAIL окно меньше ожидаемого'; $ok = $false }
      $bmp = New-Object System.Drawing.Bitmap $w, $ht
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $hdc = $g.GetHdc()
      [void][W]::PrintWindow($h, $hdc, 0)
      $g.ReleaseHdc($hdc)
      $g.Dispose()
      $shot = Join-Path $root 'screenshot-login.png'
      $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
      # анализ: сколько различных цветов, есть ли оранжевые и «фоновые» пиксели
      $colors = @{}
      $orange = 0; $sampled = 0
      for ($y = 0; $y -lt $ht; $y += 4) {
        for ($x = 0; $x -lt $w; $x += 4) {
          $c = $bmp.GetPixel($x, $y)
          $sampled++
          $key = "$($c.R),$($c.G),$($c.B)"
          $colors[$key] = 1
          if ($c.R -gt 180 -and $c.G -gt 80 -and $c.G -lt 190 -and $c.B -lt 90) { $orange++ }
        }
      }
      $bmp.Dispose()
      Write-Output ("PASS скриншот сохранён: $shot (сэмплов $sampled, уникальных цветов " + $colors.Count + ")")
      Write-Output ("INFO оранжевых пикселей (акцент): $orange")
      if ($colors.Count -lt 20) { Write-Output 'FAIL окно почти пустое (мало цветов)'; $ok = $false } else { Write-Output 'PASS окно отрисовано (разнообразные цвета)' }
      if ($orange -lt 20) { Write-Output 'WARN оранжевый акцент почти не найден' } else { Write-Output 'PASS оранжевый акцент присутствует' }
    } else { Write-Output 'WARN не удалось получить размер окна' }
  }
}

Start-Sleep -Seconds 1
if (-not $proc.HasExited) {
  [void]$proc.CloseMainWindow()
  Start-Sleep -Seconds 2
  if (-not $proc.HasExited) { $proc.Kill(); Write-Output 'INFO процесс завершён принудительно' }
  else { Write-Output 'PASS окно закрылось штатно' }
}
if ($ok) { Write-Output '== GUI SMOKE: OK =='; exit 0 } else { Write-Output '== GUI SMOKE: FAIL =='; exit 1 }
