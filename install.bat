@echo off
rem ==========================================================================
rem  DwgViewer WLX plugin installer for Total Commander (no admin rights)
rem  Just double click this file, or run it from a console window.
rem  Any arguments are passed through to install.ps1
rem  (for example:  install.bat -TcDir "D:\TC"  or  install.bat -Uninstall)
rem ==========================================================================
setlocal
chcp 65001 >nul

set "PS1=%~dp0install.ps1"

if not exist "%PS1%" (
    echo.
    echo  ERROR: install.ps1 was not found next to this file.
    echo  Extract the whole release archive first, then run install.bat again.
    echo.
    pause
    exit /b 1
)

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell.exe"

"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo  Installation finished with errors ^(code %RC%^).
    pause
)

endlocal & exit /b %RC%
