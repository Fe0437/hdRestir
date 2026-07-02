@echo off
REM Thin shim around make.bat so all dependency / shell / working-directory
REM handling lives in one place. Runs the Justfile's `launch` recipe (usdview).
REM The scene path is forward-slashed so it survives whichever shell just uses.
setlocal
set "SCENE=%~1"
if "%SCENE%"=="" set "SCENE=example_scenes/scene.usda"
set "SCENE=%SCENE:\=/%"
"%~dp0make.bat" launch "%SCENE%"
