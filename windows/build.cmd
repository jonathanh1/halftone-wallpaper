@echo off
rem HalftoneWallpaper for Windows 11 — one-file build.
rem Needs MSVC (Visual Studio 2022 Build Tools or Community) + Windows SDK.
rem Cross-compile also works: zig rc -- app.rc app.res  (once)
rem   zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode
rem   -Wl,--subsystem,windows HalftoneWallpaper.cpp app.res -o HalftoneWallpaper.exe
rem   -ld3d11 -ldxgi -luser32 -lgdi32 -lshell32 -lole32
setlocal
set "SRC=%~dp0HalftoneWallpaper.cpp"
set "OUT=%~dp0HalftoneWallpaper.exe"

rem Locate a Visual Studio installation via vswhere.
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSW%" (
  echo vswhere not found. Install "Visual Studio Build Tools" with the
  echo "Desktop development with C++" workload, then rerun build.cmd.
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSW%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo MSVC C++ toolchain not found. Install the "Desktop development with C++" workload.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo vcvars64.bat failed.
  exit /b 1
)

rc /nologo "%~dp0app.rc" /fo"%~dp0app.res"
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /W3 /DUNICODE /D_UNICODE "%SRC%" "%~dp0app.res" /Fe:"%OUT%" /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1
echo Built %OUT%
echo Run once:            "%OUT%"
echo Autostart at login:  "%OUT%" --install
echo.
echo Optional — self-sign to quiet SmartScreen on THIS machine:
echo   PowerShell:  New-SelfSignedCertificate -Type CodeSigningCert ^
echo     -Subject "CN=HalftoneWallpaper" -CertStoreLocation Cert:\CurrentUser\My
echo   signtool sign /a /fd sha256 /td sha256 "%OUT%"
echo   then move the cert from Personal to Trusted Root Certification Authorities.
