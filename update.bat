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
rem A PowerShell principal check is used instead of "net session" because the
rem Server service may be disabled, which would make the check loop.
set "ISADMIN="
for /f %%a in ('powershell -NoProfile -Command "([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)"') do set "ISADMIN=%%a"
if /i "%ISADMIN%"=="True" goto :run
if /i "%~1"=="--elevated" goto :noadmin

echo Requesting administrator rights...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Start-Process -FilePath '%~f0' -Verb RunAs -ArgumentList '--elevated %*'"
exit /b

:noadmin
echo Could not obtain administrator rights.
echo Run this file with "Run as administrator".
pause
exit /b 1

:run
set "ARGS=%*"
set "ARGS=%ARGS:--elevated=%"
echo ==============================================
echo  DWG Viewer: update (administrator)
echo ==============================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Update %ARGS%
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo Update failed with code %RC%.
echo.
echo Start Total Commander again to use the new version.
pause
exit /b %RC%
