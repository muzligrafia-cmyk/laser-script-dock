@echo off
setlocal EnableExtensions
title Laser Script Dock - install
cd /d "%~dp0"

rem ==========================================================================
rem  Copies dist\LaserScriptDock.aip into Illustrator's Plug-ins folder.
rem  Windows will ask for permission (Program Files is protected).
rem
rem  Leave empty to use the newest Illustrator found, or set it yourself:
set "AI_DIR="
rem ==========================================================================

rem ---- Ask for admin rights if we don't have them yet -----------------------
net session >nul 2>&1
if errorlevel 1 (
    echo  Asking Windows for permission to copy into Program Files...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    if errorlevel 1 echo  [X] Permission was not given, nothing was copied.
    if /i not "%~1"=="nopause" pause
    exit /b
)

if not exist "dist\LaserScriptDock.aip" (
    echo  [X] Nothing to install yet - double-click build.bat first.
    goto :end
)

rem ---- Find Illustrator -----------------------------------------------------
if not defined AI_DIR (
    for /d %%D in ("%ProgramFiles%\Adobe\Adobe Illustrator 20*") do (
        if exist "%%~fD\Plug-ins" set "AI_DIR=%%~fD"
    )
)
if not defined AI_DIR (
    echo  [X] Couldn't find Illustrator in "%ProgramFiles%\Adobe".
    echo      Open install.bat in Notepad and fill in  set "AI_DIR=
    goto :end
)
echo  Illustrator: %AI_DIR%

rem ---- Illustrator must be closed (it locks the plugin file) -------------
:checkRunning
tasklist /fi "imagename eq Illustrator.exe" 2>nul | find /i "Illustrator.exe" >nul
if not errorlevel 1 (
    echo.
    echo  Illustrator is running. Please quit it, then press a key here.
    pause >nul
    goto :checkRunning
)

rem ---- Copy -------------------------------------------------------------------
set "DEST=%AI_DIR%\Plug-ins\LaserScriptDock"
if not exist "%DEST%" mkdir "%DEST%"

copy /y "dist\LaserScriptDock.aip" "%DEST%\" >nul
if errorlevel 1 (
    echo  [X] Couldn't copy the plugin into "%DEST%".
    goto :end
)
xcopy /e /i /q /y "dist\Laser Script Dock defaults" "%DEST%\Laser Script Dock defaults\" >nul

echo.
echo  [OK] Installed into: %DEST%
echo.
echo  Start Illustrator and open  Window ^> Laser Script Dock.
echo  Your scripts folder will be:  %USERPROFILE%\Documents\Laser Script Dock
echo  (if your Documents folder is on OneDrive, it's in there instead)

:end
echo.
if /i not "%~1"=="nopause" pause
endlocal
