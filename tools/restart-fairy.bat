@echo off
rem Restart all Fairy services (called by console 'Save & Restart')
rem Keep this file pure ASCII + CRLF.
rem Paths derived from this script's own location -- do not hardcode.
rem Override the interpreter with the FAIRY_PY environment variable.
set PY=%FAIRY_PY%
if not defined PY set PY=python
set SCRIPT=%~dp0start-fairy-all.py
set LOG=%~dp0..\logs\restart.log
if not exist "%~dp0..\logs" mkdir "%~dp0..\logs"
echo [%date% %time%] restart requested >> "%LOG%"
"%PY%" "%SCRIPT%" --restart >> "%LOG%" 2>&1
echo [%date% %time%] restart finished exit=%ERRORLEVEL% >> "%LOG%"
