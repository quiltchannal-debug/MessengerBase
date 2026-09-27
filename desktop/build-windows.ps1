# Локальная сборка Windows-клиента OrangeM портативным MinGW-w64 (D:\messenger\tools\mingw).
# Использование: powershell -File build-windows.ps1
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$mingw = Join-Path (Split-Path -Parent $root) 'tools\mingw\mingw64\bin'
$gpp   = Join-Path $mingw 'g++.exe'
$windres = Join-Path $mingw 'windres.exe'
if (-not (Test-Path $gpp)) { throw "Не найден компилятор: $gpp" }

$src = Get-ChildItem (Join-Path $root 'src\*.cpp') | ForEach-Object { $_.FullName }
Push-Location $root
try {
  & $windres -I res 'res\resources.rc' -O coff -o 'res\resources.res'
  if ($LASTEXITCODE -ne 0) { throw 'windres failed' }
  & $gpp -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -municode -DUNICODE -D_UNICODE -Isrc `
         @src 'res\resources.res' -o 'orangem.exe' `
         -mwindows -static -static-libgcc -static-libstdc++ `
         -lwinhttp -lws2_32 -lgdi32 -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -lshell32 -lshlwapi -luuid -luser32
  if ($LASTEXITCODE -ne 0) { throw 'сборка не удалась' }
  $exe = Join-Path $root 'orangem.exe'
  Write-Output ("built: {0} ({1:N0} КБ)" -f $exe, ((Get-Item $exe).Length / 1KB))
} finally { Pop-Location }
