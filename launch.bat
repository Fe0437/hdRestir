@echo off
setlocal

SET "PROJ_ROOT=%~dp0"
SET "SCENE_FILE=%~1"
IF "%SCENE_FILE%"=="" SET "SCENE_FILE=scene.usda"

call :ensure_just
if %errorlevel% neq 0 goto fail

just --justfile "%PROJ_ROOT%Justfile" --working-directory "%PROJ_ROOT%" launch "%SCENE_FILE%"
if %errorlevel% neq 0 goto fail
exit /b 0

:ensure_just
where just >nul 2>&1
if %errorlevel% equ 0 exit /b 0

echo [INFO] just not found. Installing via winget...
winget install -e --id Casey.Just --accept-package-agreements --accept-source-agreements
if %errorlevel% neq 0 exit /b 1

where just >nul 2>&1
if %errorlevel% neq 0 (
	echo [ERROR] just installed but is not visible in this terminal. Open a new terminal and retry.
	exit /b 1
)

exit /b 0

:fail
pause
exit /b %errorlevel%
