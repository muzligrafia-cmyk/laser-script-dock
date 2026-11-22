@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Laser Script Dock - build
cd /d "%~dp0"

rem ==========================================================================
rem  Laser Script Dock - one-click build for Windows
rem  Just double-click this file. It finds the tools by itself.
rem
rem  Only if it can't find the Illustrator SDK, put its folder here
rem  (the folder that contains "illustratorapi"):
set "SDK="
rem ==========================================================================

echo.
echo  ===== Laser Script Dock - build =====
echo.

rem ---- 1. Illustrator SDK -------------------------------------------------
if not defined SDK if exist "C:\SDK\AdobeIllustrator2027\illustratorapi" set "SDK=C:\SDK\AdobeIllustrator2027"
if not defined SDK (
    for /d %%D in ("C:\SDK\*" "%USERPROFILE%\Documents\*SDK*" "%USERPROFILE%\Downloads\*SDK*" "%USERPROFILE%\Desktop\*SDK*") do (
        if exist "%%~fD\illustratorapi" set "SDK=%%~fD"
    )
)
if not defined SDK (
    echo  [X] Couldn't find the Illustrator SDK.
    echo      Open build.bat in Notepad and write its folder after  set "SDK=
    goto :fail
)
if not exist "%SDK%\illustratorapi" (
    echo  [X] "%SDK%" doesn't look like the Illustrator SDK ^(no "illustratorapi" folder inside^).
    goto :fail
)
echo  SDK:    %SDK%

rem ---- 2. CMake -----------------------------------------------------------
set "CMAKE="
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%C"
if not defined CMAKE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "delims=" %%C in ('"!VSWHERE!" -latest -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe') do set "CMAKE=%%C"
    )
)
if not defined CMAKE (
    echo  [X] Couldn't find CMake. Install it from cmake.org, or add the
    echo      "C++ CMake tools" part in the Visual Studio Installer.
    goto :fail
)
echo  CMake:  %CMAKE%
echo.

rem ---- 3. Configure + build -----------------------------------------------
echo  Building... ^(the first time takes a minute or two^)
echo.
"%CMAKE%" -S . -B build -A x64 -DAI_SDK_ROOT="%SDK%" > build-log.txt 2>&1
if errorlevel 1 (
    echo  [X] Setting up the build failed. Details are in build-log.txt
    findstr /i /c:"error" /c:"could not find" build-log.txt
    goto :fail
)

"%CMAKE%" --build build --config Release >> build-log.txt 2>&1
if errorlevel 1 (
    echo  [X] Compiling failed. The important lines:
    echo.
    findstr /r /c:"error [A-Z][A-Z]*[0-9]" /c:"fatal error" build-log.txt
    echo.
    echo  Full details are in build-log.txt - you can send that file to Claude.
    goto :fail
)

rem ---- 4. Collect the result in dist\ -------------------------------------
if exist dist rmdir /s /q dist
mkdir dist
copy /y "build\Release\LaserScriptDock.aip" "dist\" >nul || goto :fail
xcopy /e /i /q /y "defaults" "dist\Laser Script Dock defaults\" >nul

echo  [OK] Built: dist\LaserScriptDock.aip
echo.

rem ---- 5. Offer to install --------------------------------------------------
choice /c YN /n /m " Copy it into Illustrator now? (Y/N) "
if errorlevel 2 (
    echo.
    echo  Skipped. Run install.bat whenever you're ready.
    goto :end
)
call "%~dp0install.bat" nopause
goto :end

:fail
echo.
echo  Build did not finish.
:end
echo.
pause
endlocal
