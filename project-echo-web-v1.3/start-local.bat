@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "HOST=127.0.0.1"
set "PORT=8765"
set "URL=http://127.0.0.1:8765/"
set "READY=0"

echo.
echo ============================================================
echo             PROJECT ECHO - SERVIDOR V1.3
echo ============================================================
echo.

if not exist "index.html" (
  echo ERROR: No se encuentra index.html en:
  echo %CD%
  pause
  exit /b 1
)

where py >nul 2>&1
if not errorlevel 1 (
  set "PYTHON_CMD=py -3"
) else (
  where python >nul 2>&1
  if not errorlevel 1 (
    set "PYTHON_CMD=python"
  ) else (
    echo ERROR: No se encuentra Python 3.
    pause
    exit /b 1
  )
)

echo Carpeta: %CD%
echo Python: %PYTHON_CMD%
echo URL: %URL%
echo.

powershell -NoProfile -Command "$c=New-Object System.Net.Sockets.TcpClient; try{$c.Connect('%HOST%',%PORT%);exit 0}catch{exit 1}finally{$c.Dispose()}" >nul 2>&1
if not errorlevel 1 (
  curl.exe -fsS --max-time 3 "%URL%" 2>nul | findstr /i /c:"WEB V1.3" >nul 2>&1
  if not errorlevel 1 (
    echo Ya hay un servidor PROJECT ECHO V1.3 funcionando. Se reutilizara.
    goto OPEN_BROWSER
  )
  echo ERROR: El puerto %PORT% esta ocupado por otro servidor.
  echo Deten el servidor anterior con CTRL+C o libera el puerto.
  pause
  exit /b 1
)

echo Iniciando servidor Python...
start "PROJECT ECHO V1.3 - Servidor local (no cerrar)" /D "%~dp0" cmd /k "%PYTHON_CMD% -m http.server %PORT% --bind %HOST%"

for /l %%I in (1,1,15) do (
  if "!READY!"=="0" (
    curl.exe -fsS --max-time 2 "%URL%" 2>nul | findstr /i /c:"WEB V1.3" >nul 2>&1
    if not errorlevel 1 set "READY=1"
    if "!READY!"=="0" timeout /t 1 /nobreak >nul
  )
)

if "!READY!"=="0" (
  echo ERROR: El servidor no ha podido entregar index.html.
  echo Revisa la ventana del servidor Python.
  pause
  exit /b 1
)

echo Servidor comprobado correctamente.

:OPEN_BROWSER
echo Abriendo navegador...
start "" "%URL%"
echo.
echo Direccion: %URL%
echo Mantén abierta la ventana del servidor.
echo Para detenerlo, pulsa CTRL+C en la ventana Python.
echo.
timeout /t 2 /nobreak >nul
exit /b 0
