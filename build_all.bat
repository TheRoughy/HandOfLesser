@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "CLEAN_BUILD=0"

if not "%~1"=="" (
	if /i "%~1"=="--clean" (
		set "CLEAN_BUILD=1"
	) else (
		echo ERROR: Unexpected argument "%~1".
		echo Usage: build_all.bat [--clean]
		exit /b 1
	)
)

if not "%~2"=="" (
	echo ERROR: Unexpected argument "%~2".
	echo Usage: build_all.bat [--clean]
	exit /b 1
)

echo ========================================
echo HandOfLesser Distribution Build
echo ========================================
echo.

if "%CLEAN_BUILD%"=="1" (
	call "%ROOT_DIR%build.bat" release --clean
) else (
	call "%ROOT_DIR%build.bat" release
)
if errorlevel 1 exit /b 1

call "%ROOT_DIR%build_installer.bat"
if errorlevel 1 exit /b 1

call "%ROOT_DIR%build_unity_vpm_package.bat"
if errorlevel 1 exit /b 1

echo.
echo ========================================
echo Distribution build completed successfully!
echo ========================================
echo Output: %ROOT_DIR%distribution

exit /b 0
