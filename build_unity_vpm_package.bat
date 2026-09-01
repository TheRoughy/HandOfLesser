@echo off
setlocal

set "ROOT_DIR=%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%build_unity_vpm_package.ps1" %*
if errorlevel 1 (
	echo ERROR: Failed to build the Unity VPM package.
	exit /b 1
)

exit /b 0
