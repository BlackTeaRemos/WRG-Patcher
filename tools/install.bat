@echo off
setlocal

set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"

set "GAME="
if not "%~1"=="" call :setgame "%~1"

if not defined GAME if exist "%HERE%\WarGame3.exe" set "GAME=%HERE%"

if not defined GAME (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"') do call :steamgame "%%B"
)

if not defined GAME (
    for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul ^| find "InstallPath"') do call :steamgame "%%B"
)

if not defined GAME (
    for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>nul ^| find "InstallPath"') do call :steamgame "%%B"
)

if not defined GAME (
    echo Could not find Wargame Red Dragon.
    echo Run this from the game folder, or pass the path:  install.bat "D:\path\to\Wargame Red Dragon"
    pause
    exit /b 1
)

echo Game folder: %GAME%

if not exist "%HERE%\version.dll" (
    echo version.dll must sit next to this script.
    pause
    exit /b 1
)

if /i not "%HERE%"=="%GAME%" copy /Y "%HERE%\version.dll" "%GAME%\version.dll" >nul

set "SYSVER=%WINDIR%\System32\version.dll"
if exist "%WINDIR%\SysWOW64\version.dll" set "SYSVER=%WINDIR%\SysWOW64\version.dll"

copy /Y "%SYSVER%" "%GAME%\version_real.dll" >nul
if errorlevel 1 (
    echo Failed to copy system version.dll
    pause
    exit /b 1
)

if not exist "%GAME%\mods" mkdir "%GAME%\mods"

echo Installed version.dll, version_real.dll, and mods\ folder.
echo Launch the game normally.
pause
exit /b 0

:setgame
set "CAND=%~1"
if "%CAND:~-1%"=="\" set "CAND=%CAND:~0,-1%"
if exist "%CAND%\WarGame3.exe" set "GAME=%CAND%"
exit /b 0

:steamgame
set "STEAM=%~1"
set "STEAM=%STEAM:/=\%"
if exist "%STEAM%\steamapps\common\Wargame Red Dragon\WarGame3.exe" set "GAME=%STEAM%\steamapps\common\Wargame Red Dragon"
exit /b 0
