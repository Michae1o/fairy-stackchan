@echo off
setlocal EnableExtensions
title Install FTDI driver (for the K151-R remote)
echo ============================================================
echo   Install the FTDI USB-serial driver
echo   (needed before you can flash the K151-R remote)
echo ============================================================
echo.

rem ---- admin rights: relaunch elevated if needed ----
net session >nul 2>&1
if errorlevel 1 (
  echo   Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

rem ---- driver folder: <repo>\ftdi-driver  (created by fetch-ftdi-driver.py) ----
set "DRV=%~dp0..\ftdi-driver"
if not exist "%DRV%\ftdibus.inf" goto no_driver

echo   Driver folder:
echo     %DRV%
echo.
echo   [1/3] Adding driver packages ...
pnputil /add-driver "%DRV%\ftdibus.inf" /install
pnputil /add-driver "%DRV%\ftdiport.inf" /install

echo.
echo   [2/3] Rescanning devices ...
pnputil /scan-devices

echo.
echo   [3/3] Serial ports present now:
powershell -NoProfile -Command "Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -notlike 'ACPI*' } | ForEach-Object { '        ' + $_.FriendlyName }"

echo.
echo ------------------------------------------------------------
echo   Look for:   USB Serial Port (COMx)    ==^> installed OK
echo.
echo   If it is not there:
echo     - unplug the remote, wait 3s, plug it back in, run this again
echo     - or open Device Manager and check for a yellow "!" on M5stack
echo   The COM number shown is what you pass to esptool when flashing.
echo ------------------------------------------------------------
pause
goto :eof

:no_driver
echo   ERROR: driver files not found:
echo     %DRV%\ftdibus.inf
echo.
echo   Download them first, from the repo root:
echo.
echo       python fetch-ftdi-driver.py
echo.
echo   (it pulls the correct package from the Microsoft Update Catalog --
echo    the FTDI website is behind a Cloudflare challenge, and Microsoft
echo    splits the driver into several per-chip packages, so picking the
echo    right one by hand is easy to get wrong)
echo.
pause
goto :eof
