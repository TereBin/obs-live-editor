@echo off
setlocal
cd /d "%~dp0"

echo Live Editor for OBS - Worker configuration
echo.
set /p "WORKER_URL=Worker URL (https://YOUR-WORKER.workers.dev): "
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Configure-Secrets.ps1" -WorkerUrl "%WORKER_URL%"

echo.
if errorlevel 1 (
  echo Configuration failed. Keep this window open and report the error shown above.
) else (
  echo Configuration completed successfully. You can close this window.
)
pause
