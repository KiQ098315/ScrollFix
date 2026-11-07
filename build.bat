@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "SRC=%ROOT%src\main.cpp"
set "OUTDIR=%ROOT%bin"
set "OUT=%OUTDIR%\ScrollFix.exe"
set "RES=%OUTDIR%\app.res.o"

set "GPP="
if exist "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe" (
  set "GPP=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe"
)
if "%GPP%"=="" (
  where g++ >nul 2>nul && for /f "delims=" %%I in ('where g++') do set "GPP=%%I" & goto :have_gpp
)
:have_gpp
if "%GPP%"=="" (
  echo ERROR: g++ not found.
  exit /b 1
)

for %%I in ("%GPP%") do set "BINDIR=%%~dpI"
set "WINDRES=%BINDIR%windres.exe"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo Using: %GPP%
pushd "%ROOT%src"
"%WINDRES%" -I. -O coff -o "%RES%" app.rc
if errorlevel 1 ( popd & echo RESOURCE BUILD FAILED & exit /b 1 )
popd

"%GPP%" -std=c++17 -O2 -s -mwindows -municode -fno-rtti -finput-charset=utf-8 -fexec-charset=UTF-8 ^
  "%SRC%" "%RES%" -o "%OUT%" ^
  -static-libgcc -static-libstdc++ ^
  -lcomctl32 -lshell32 -luser32 -lgdi32 -lgdiplus -ladvapi32 -lkernel32 -lwinmm

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

copy /Y "%ROOT%src\app.manifest" "%OUT%.manifest" >nul

echo.
echo OK: %OUT%
for %%A in ("%OUT%") do echo Size: %%~zA bytes
exit /b 0
