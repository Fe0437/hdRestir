@echo off
setlocal

set "PROJ_ROOT=%~dp0"

call :ensure_just
if %errorlevel% neq 0 goto fail

if "%~1"=="" (
    just --justfile "%PROJ_ROOT%Justfile" --working-directory "%PROJ_ROOT%" build
) else (
    just --justfile "%PROJ_ROOT%Justfile" --working-directory "%PROJ_ROOT%" %*
)

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
