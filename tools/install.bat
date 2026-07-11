@echo off
setlocal enabledelayedexpansion

set "GAME=%~1"

if not defined GAME (
    if exist "%~dp0WarGame3.exe" set "GAME=%~dp0"
)

if not defined GAME (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"') do set "STEAM=%%B"
    if defined STEAM (
        set "STEAM=!STEAM:/=\!"
        if exist "!STEAM!\steamapps\common\Wargame Red Dragon\WarGame3.exe" set "GAME=!STEAM!\steamapps\common\Wargame Red Dragon"
    )
)

if not defined GAME (
    for %%D in ("HKLM\SOFTWARE\WOW6432Node\Valve\Steam" "HKLM\SOFTWARE\Valve\Steam") do (
        for /f "tokens=2,*" %%A in ('reg query %%D /v InstallPath 2^>nul ^| find "InstallPath"') do set "STEAM=%%B"
        if defined STEAM if exist "!STEAM!\steamapps\common\Wargame Red Dragon\WarGame3.exe" set "GAME=!STEAM!\steamapps\common\Wargame Red Dragon"
    )
)

if not defined GAME (
    echo Could not find Wargame Red Dragon.
    echo Run this from the game folder, or pass the path:  install.bat "D:\path\to\Wargame Red Dragon"
    pause
    exit /b 1
)

if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"

if not exist "%GAME%\WarGame3.exe" (
    echo WarGame3.exe not found in: %GAME%
    pause
    exit /b 1
)

echo Game folder: %GAME%

if not exist "%~dp0version.dll" (
    echo version.dll must sit next to this script.
    pause
    exit /b 1
)

if /i not "%~dp0"=="%GAME%\" (
    copy /Y "%~dp0version.dll" "%GAME%\version.dll" >nul
)

copy /Y "%WINDIR%\System32\version.dll" "%GAME%\version_real.dll" >nul
if errorlevel 1 (
    echo Failed to copy system version.dll
    pause
    exit /b 1
)

if not exist "%GAME%\mods" mkdir "%GAME%\mods"

echo Installed version.dll, version_real.dll, and mods\ folder.
echo Launch the game normally.
pause
