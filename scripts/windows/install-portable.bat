REM SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
REM
REM SPDX-License-Identifier: GPL-3.0-or-later

@echo off
setlocal EnableExtensions

echo Install BGOBS into PortableApps OBS
echo -----------------------------------
echo Close OBS Studio before continuing.
echo.

set "PACKAGE_ROOT=%~dp0"
set "PORTABLE_ROOT=%~1"
if "%PORTABLE_ROOT%"=="" if exist "%CD%\OBSPortable.exe" set "PORTABLE_ROOT=%CD%"
if "%PORTABLE_ROOT%"=="" if exist "%CD%\OBS-StudioPortable.exe" set "PORTABLE_ROOT=%CD%"

if "%PORTABLE_ROOT%"=="" (
    set /p "PORTABLE_ROOT=Folder that contains OBSPortable.exe: "
)
set "PORTABLE_ROOT=%PORTABLE_ROOT:"=%"

if not exist "%PACKAGE_ROOT%bgobs\bin\64bit\bgobs.dll" (
    echo ERROR: bgobs\bin\64bit\bgobs.dll is missing from this package.
    pause
    exit /b 1
)

if not exist "%PORTABLE_ROOT%\OBSPortable.exe" if not exist "%PORTABLE_ROOT%\OBS-StudioPortable.exe" (
    echo ERROR: no OBSPortable.exe launcher found in:
    echo   %PORTABLE_ROOT%
    pause
    exit /b 1
)

set "BIN_DEST=%PORTABLE_ROOT%\App\obs-studio\obs-plugins\64bit"
set "APP_DATA_DEST=%PORTABLE_ROOT%\App\obs-studio\data\obs-plugins\bgobs"
set "USER_DATA_DEST=%PORTABLE_ROOT%\Data\obs-plugins\bgobs"

if not exist "%BIN_DEST%" mkdir "%BIN_DEST%"
if not exist "%APP_DATA_DEST%" mkdir "%APP_DATA_DEST%"
if not exist "%USER_DATA_DEST%" mkdir "%USER_DATA_DEST%"

xcopy "%PACKAGE_ROOT%bgobs\bin\64bit\*" "%BIN_DEST%\" /E /I /Y /Q >nul
if errorlevel 1 goto :CopyError
xcopy "%PACKAGE_ROOT%bgobs\data\*" "%APP_DATA_DEST%\" /E /I /Y /Q >nul
if errorlevel 1 goto :CopyError
xcopy "%PACKAGE_ROOT%bgobs\data\*" "%USER_DATA_DEST%\" /E /I /Y /Q >nul
if errorlevel 1 goto :CopyError

echo.
echo BGOBS installed successfully in:
echo   %PORTABLE_ROOT%
echo.
echo Restart OBS, then add the source "CaCam".
pause
exit /b 0

:CopyError
echo.
echo ERROR: installation failed while copying files. Close OBS and try again.
pause
exit /b 1
