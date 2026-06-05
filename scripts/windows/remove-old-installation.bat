REM SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
REM SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
REM
REM SPDX-License-Identifier: GPL-3.0-or-later

@echo off
setlocal EnableExtensions

echo Remove old obs-backgroundremoval installations
echo ------------------------------------------------
echo Close OBS Studio before continuing.
echo.

set "PORTABLE_ROOT=%~1"
if "%PORTABLE_ROOT%"=="" if exist "%CD%\OBS-StudioPortable.exe" set "PORTABLE_ROOT=%CD%"

if "%PORTABLE_ROOT%"=="" (
    echo Optional: enter the folder that contains OBS-StudioPortable.exe.
    echo Leave empty to only clean standard OBS Studio locations.
    set /p "PORTABLE_ROOT=OBS-StudioPortable folder: "
)

if not "%PORTABLE_ROOT%"=="" (
    set "PORTABLE_ROOT=%PORTABLE_ROOT:"=%"
    echo.
    echo Cleaning PortableApps OBS locations under:
    echo   %PORTABLE_ROOT%
    call :RemoveDir "%PORTABLE_ROOT%\Data\obs-studio\plugins\obs-backgroundremoval"
    call :RemoveFile "%PORTABLE_ROOT%\Data\obs-studio\obs-plugins\64bit\obs-backgroundremoval.dll"
    call :RemoveFile "%PORTABLE_ROOT%\Data\obs-studio\obs-plugins\64bit\obs-backgroundremoval.pdb"
    call :RemoveDir "%PORTABLE_ROOT%\Data\obs-studio\data\obs-plugins\obs-backgroundremoval"
    call :RemoveDir "%PORTABLE_ROOT%\Data\config\obs-studio\plugins\obs-backgroundremoval"
    call :RemoveDir "%PORTABLE_ROOT%\Data\obs-plugins\obs-backgroundremoval"
    call :RemoveFile "%PORTABLE_ROOT%\App\obs-studio\obs-plugins\64bit\obs-backgroundremoval.dll"
    call :RemoveFile "%PORTABLE_ROOT%\App\obs-studio\obs-plugins\64bit\obs-backgroundremoval.pdb"
    call :RemoveDir "%PORTABLE_ROOT%\App\obs-studio\data\obs-plugins\obs-backgroundremoval"
)

echo.
echo Cleaning standard OBS Studio locations.
call :RemoveDir "%ProgramData%\obs-studio\plugins\obs-backgroundremoval"
call :RemoveFile "%ProgramFiles%\obs-studio\obs-plugins\64bit\obs-backgroundremoval.dll"
call :RemoveFile "%ProgramFiles%\obs-studio\obs-plugins\64bit\obs-backgroundremoval.pdb"
call :RemoveDir "%ProgramFiles%\obs-studio\data\obs-plugins\obs-backgroundremoval"

if not "%ProgramFiles(x86)%"=="" (
    call :RemoveFile "%ProgramFiles(x86)%\obs-studio\obs-plugins\64bit\obs-backgroundremoval.dll"
    call :RemoveFile "%ProgramFiles(x86)%\obs-studio\obs-plugins\64bit\obs-backgroundremoval.pdb"
    call :RemoveDir "%ProgramFiles(x86)%\obs-studio\data\obs-plugins\obs-backgroundremoval"
)

echo.
echo Done. Install the new package by copying:
echo   obs-backgroundremoval\bin\64bit\*.dll
echo into:
echo   ^<OBS-StudioPortable^>\App\obs-studio\obs-plugins\64bit\
echo.
echo Then copy obs-backgroundremoval\data\* into:
echo   ^<OBS-StudioPortable^>\App\obs-studio\data\obs-plugins\obs-backgroundremoval\
echo   ^<OBS-StudioPortable^>\Data\obs-plugins\obs-backgroundremoval\
echo.
echo If OBS still reports version 1.3.x, open the latest OBS log and search
echo for obs-backgroundremoval to find the old DLL path that is still loaded.
echo.
pause
exit /b 0

:RemoveDir
if exist "%~1" (
    echo Removing directory "%~1"
    rmdir /s /q "%~1"
) else (
    echo Not found: "%~1"
)
exit /b 0

:RemoveFile
if exist "%~1" (
    echo Removing file "%~1"
    del /f /q "%~1"
) else (
    echo Not found: "%~1"
)
exit /b 0
