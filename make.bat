@echo off
setlocal

set "PROJ_ROOT=%~dp0"
REM %~dp0 ends with a backslash; strip it so quoted paths don't end in \" (which
REM escapes the quote and corrupts the argument -> "invalid directory" os error 267).
if "%PROJ_ROOT:~-1%"=="\" set "PROJ_ROOT=%PROJ_ROOT:~0,-1%"

call :ensure_just
if %errorlevel% neq 0 goto fail

call :ensure_python
if %errorlevel% neq 0 goto fail
REM Use forward slashes: the recipe may run under sh, which would treat the
REM backslashes in a Windows path as escape characters and mangle it.
set "PYEXE=%PYEXE:\=/%"


REM The Justfile defaults Windows recipes to cmd.exe (since `sh` usually isn't on
REM PATH). If `sh` IS available, prefer it by overriding the shell back to sh.
set "JUST_SHELL="
where sh >nul 2>&1
if %errorlevel% equ 0 set "JUST_SHELL=--shell sh --shell-arg -cu"

REM Pass the resolved Python down to the recipes (overrides the Justfile default),
REM so workflow.py always runs under an interpreter with C dev files.
if "%~1"=="" (
    just --justfile "%PROJ_ROOT%\Justfile" --working-directory "%PROJ_ROOT%" %JUST_SHELL% python="%PYEXE%" build
) else (
    just --justfile "%PROJ_ROOT%\Justfile" --working-directory "%PROJ_ROOT%" %JUST_SHELL% python="%PYEXE%" %*
)

if %errorlevel% neq 0 goto fail
exit /b 0

:ensure_just
where just >nul 2>&1
if %errorlevel% equ 0 exit /b 0

echo [INFO] just not found. Installing via winget...
REM Don't treat winget's exit code as fatal: it returns non-zero when the
REM package is already installed/up-to-date. Trust the `where just` check below.
winget install -e --id Casey.Just --accept-package-agreements --accept-source-agreements

REM winget drops shims here; add them to PATH for this session so just is usable
REM immediately, without having to open a new terminal.
if exist "%LOCALAPPDATA%\Microsoft\WinGet\Links\just.exe" set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"

where just >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] just is still not visible in this terminal. Open a new terminal and retry.
    exit /b 1
)

exit /b 0

REM OpenUSD's python bindings need a Python with C development files (Python.h +
REM import lib). The Microsoft Store python lacks them, so prefer/install a
REM python.org build and hand its path to the recipes via PYEXE.
:ensure_python
set "PYEXE="
for %%v in (313 312 311) do call :find_python %%v
if defined PYEXE exit /b 0

echo [INFO] Python with dev files not found. Installing via winget...
winget install -e --id Python.Python.3.13 --accept-package-agreements --accept-source-agreements
if exist "%LOCALAPPDATA%\Microsoft\WinGet\Links" set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
for %%v in (313 312 311) do call :find_python %%v
if defined PYEXE exit /b 0

echo [ERROR] Could not find or install a Python with C development files.
exit /b 1

:find_python
if defined PYEXE exit /b 0
if exist "%LOCALAPPDATA%\Programs\Python\Python%1\python.exe" set "PYEXE=%LOCALAPPDATA%\Programs\Python\Python%1\python.exe"
if defined PYEXE exit /b 0
if exist "C:\Python%1\python.exe" set "PYEXE=C:\Python%1\python.exe"
if defined PYEXE exit /b 0
if exist "%ProgramFiles%\Python%1\python.exe" set "PYEXE=%ProgramFiles%\Python%1\python.exe"
exit /b 0

:fail
pause
exit /b %errorlevel%
