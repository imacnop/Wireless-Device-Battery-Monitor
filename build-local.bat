@echo off
rem Local Windows build with the WinLibs MinGW-w64 installed by winget:
rem   build-local.bat [version] [dev|test|stable]     (defaults: 0.0.0 dev)
rem Runs the official build.bat, with two local fixes:
rem  * puts WinLibs' g++ on PATH;
rem  * WinLibs links its own default-manifest.o (longPathAware), which clashes with res\app.manifest;
rem    an empty object, found first through -B, replaces it.
rem A dev build keeps automatic updates off, so it never replaces itself with a release.
setlocal
cd /d "%~dp0"
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
where g++ >nul 2>nul || (echo g++ not found: winget install BrechtSanders.WinLibs.POSIX.UCRT& exit /b 1)
if not exist build\nomanifest mkdir build\nomanifest
if not exist build\nomanifest\default-manifest.o (
    echo.> build\nomanifest\empty.c
    gcc -c build\nomanifest\empty.c -o build\nomanifest\default-manifest.o || exit /b 1
)
rem The patched copy sits next to build.bat, which works from its own folder (cd /d "%~dp0").
powershell.exe -NoProfile -Command "(Get-Content build.bat -Raw) -replace '(?m)^g\+\+ ', 'g++ -B build\nomanifest\ ' | Set-Content build-patched.tmp.bat -Encoding ascii" || exit /b 1
call "%~dp0build-patched.tmp.bat" %*
set RC=%ERRORLEVEL%
del build-patched.tmp.bat
exit /b %RC%
