@echo off
setlocal

set "VANGUARD_PREMAKE=%~dp0premake5.exe"
if exist "%VANGUARD_PREMAKE%" goto generate

set "VANGUARD_PREMAKE=%~dp0tools\premake\premake5.exe"
if exist "%VANGUARD_PREMAKE%" goto generate

where premake5.exe >nul 2>nul
if errorlevel 1 (
    echo Premake 5 was not found.
    echo Place premake5.exe in the repository root, tools\premake\, or PATH.
    exit /b 1
)
set "VANGUARD_PREMAKE=premake5.exe"

:generate
pushd "%~dp0"
"%VANGUARD_PREMAKE%" vs2022
set "VANGUARD_RESULT=%ERRORLEVEL%"
popd
exit /b %VANGUARD_RESULT%
