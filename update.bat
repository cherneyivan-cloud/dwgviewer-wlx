@echo off
rem ===========================================================================
rem  DWG Viewer (WLX) - update with administrator rights
rem
rem  Double-click this file. Windows will ask for administrator rights (UAC),
rem  then Total Commander is closed (if running), the plugin files are replaced
rem  and the registration in wincmd.ini is kept up to date.
rem
rem  All options of install.ps1 are passed through, e.g.:
rem     update.bat -IniPath "D:\TC\wincmd.ini"
rem ===========================================================================
setlocal
chcp 65001 >nul
cd /d "%~dp0"

rem --- Require administrator rights: relaunch elevated once -----------------
net session >nul 2>&1
if not errorlevel 1 goto :run

echo Requesting administrator rights...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Start-Process -FilePath '%~f0' -Verb RunAs -ArgumentList '%*'"
exit /b

:run
echo ==============================================
echo  DWG Viewer: update (administrator)
echo ==============================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Update %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo Update failed with code %RC%.
echo.
echo Start Total Commander again to use the new version.
pause
exit /b %RC%
