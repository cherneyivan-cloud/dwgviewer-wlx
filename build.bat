@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
cd /d "%~dp0"

set ROOT=%~dp0
set ROOT=%ROOT:~0,-1%
set SRC=%ROOT%\src
set BIN=%ROOT%\tools\w64devkit\bin
set LR=%ROOT%\third_party\libredwg
set OBJ=%ROOT%\build\obj
set DIST=%ROOT%\dist

echo === DWG Viewer WLX plugin: build ===

if not exist "%BIN%\gcc.exe" (
  echo [setup] installing toolchain and libraries...
  powershell -ExecutionPolicy Bypass -File "%ROOT%\build\setup_toolchain.ps1" || goto :err
)
if not exist "%LR%\include\dwg.h" (
  echo [setup] installing LibreDWG...
  powershell -ExecutionPolicy Bypass -File "%ROOT%\build\setup_toolchain.ps1" || goto :err
)

if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%DIST%" mkdir "%DIST%"

set "CCOPTS=-O2 -Wall -m64 -D_WIN32_WINNT=0x0601"
set "INC=-I "%LR%\include" -I "%SRC%""

echo [1/6] aci.c
"%BIN%\gcc.exe" %CCOPTS% %INC% -c "%SRC%\aci.c" -o "%OBJ%\aci.o" || goto :err
echo [2/6] draw.c
"%BIN%\gcc.exe" %CCOPTS% %INC% -c "%SRC%\draw.c" -o "%OBJ%\draw.o" || goto :err
echo [3/6] model.c
"%BIN%\gcc.exe" %CCOPTS% %INC% -c "%SRC%\model.c" -o "%OBJ%\model.o" || goto :err
echo [4/6] dwgload.c
"%BIN%\gcc.exe" %CCOPTS% %INC% -c "%SRC%\dwgload.c" -o "%OBJ%\dwgload.o" || goto :err
echo [5/6] dwgwlx.c + version.rc
"%BIN%\gcc.exe" %CCOPTS% %INC% -c "%SRC%\dwgwlx.c" -o "%OBJ%\dwgwlx.o" || goto :err
pushd "%SRC%"
"%BIN%\windres.exe" version.rc -o "%OBJ%\version.o"
popd
if not exist "%OBJ%\version.o" goto :err

echo [6/6] linking dwgviewer.wlx
"%BIN%\gcc.exe" -shared -m64 -o "%DIST%\dwgviewer.wlx" ^
  "%OBJ%\aci.o" "%OBJ%\draw.o" "%OBJ%\model.o" "%OBJ%\dwgload.o" "%OBJ%\dwgwlx.o" "%OBJ%\version.o" ^
  "%SRC%\dwgwlx.def" ^
  -lgdi32 -lcomdlg32 -luser32 -lkernel32 -lcomctl32 -ladvapi32 ^
  -static-libgcc || goto :err

copy /Y "%LR%\bin\libredwg-0.dll" "%DIST%\" >nul
copy /Y "%ROOT%\README.md" "%DIST%\" >nul
copy /Y "%ROOT%\LICENSE" "%DIST%\" >nul

echo.
echo === OK ===
echo Plugin : %DIST%\dwgviewer.wlx
echo Runtime: %DIST%\libredwg-0.dll
echo.
echo Install: build\install.ps1  (copies into Total Commander and registers)
goto :eof

:err
echo.
echo BUILD FAILED (errorlevel %errorlevel%)
exit /b 1