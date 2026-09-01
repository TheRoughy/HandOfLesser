@echo off
setlocal

set "BUILD_FLAVOR=release"
set "BUILD_FLAVOR_SET="
set "CLEAN_BUILD=0"

:parse_arguments
if "%~1"=="" goto arguments_parsed
if /i "%~1"=="--clean" (
    set "CLEAN_BUILD=1"
) else (
    if defined BUILD_FLAVOR_SET (
        echo ERROR: Unexpected argument "%~1".
        echo Usage: build.bat [debug^|release^|relwithdebinfo] [--clean]
        exit /b 1
    )
    set "BUILD_FLAVOR=%~1"
    set "BUILD_FLAVOR_SET=1"
)
shift
goto parse_arguments

:arguments_parsed

if /i "%BUILD_FLAVOR%"=="debug" (
    set "BUILD_PRESET=x64-debug"
    set "BUILD_CONFIG=Debug"
) else if /i "%BUILD_FLAVOR%"=="release" (
    set "BUILD_PRESET=x64-release"
    set "BUILD_CONFIG=Release"
) else if /i "%BUILD_FLAVOR%"=="relwithdebinfo" (
    set "BUILD_PRESET=x64-relwithdebinfo"
    set "BUILD_CONFIG=RelWithDebInfo"
) else (
    echo ERROR: Unknown build flavor "%BUILD_FLAVOR%".
    echo Usage: build.bat [debug^|release^|relwithdebinfo] [--clean]
    exit /b 1
)

echo ========================================
echo HandOfLesser Build Script
echo ========================================
echo.
echo Build flavor: %BUILD_FLAVOR%
echo Preset: %BUILD_PRESET%
echo Configuration: %BUILD_CONFIG%
if "%CLEAN_BUILD%"=="1" (echo Clean build: yes) else (echo Clean build: no)
echo.

REM Find a Visual Studio installation containing the x64 C++ build tools.
set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE_EXE%" (
    echo ERROR: Could not find vswhere.exe.
    echo Install Visual Studio 2022 or Visual Studio 2022 Build Tools.
    exit /b 1
)

set "VS_INSTALL="
set VSWHERE_CMD="%VSWHERE_EXE%"
for /f "usebackq tokens=*" %%I in (`%VSWHERE_CMD% -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
    echo ERROR: Could not find Visual Studio 2022 with the x64 C++ build tools.
    exit /b 1
)

set "VSDEVCMD=%VS_INSTALL%\Common7\Tools\VsDevCmd.bat"
set "CMAKE_EXE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%VSDEVCMD%" (
    echo ERROR: Could not find "%VSDEVCMD%".
    exit /b 1
)
if not exist "%CMAKE_EXE%" (
    echo ERROR: Could not find Visual Studio's bundled CMake.
    echo Install the CMake tools component for Visual Studio.
    exit /b 1
)

REM Initialize the compiler environment in this process before invoking CMake.
echo Initializing Visual Studio 2022 environment...
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
    echo ERROR: Failed to initialize VS Developer environment!
    exit /b 1
)

REM Navigate to project directory
cd /d "%~dp0"

if "%CLEAN_BUILD%"=="1" (
    echo Cleaning out\build\%BUILD_PRESET%...
    if exist "out\build\%BUILD_PRESET%" (
        rmdir /s /q "out\build\%BUILD_PRESET%"
        if errorlevel 1 (
            echo ERROR: Failed to remove the build directory.
            exit /b 1
        )
    )
    echo Clean complete.
    echo.
) else (
    REM Incremental builds require an existing configured build directory.
    if not exist "out\build\%BUILD_PRESET%" (
        echo ERROR: Build directory does not exist!
        echo Run build.bat %BUILD_FLAVOR% --clean first to initialize it.
        echo.
        echo REMINDER: Calling cmake directly kills kittens. Please use build scripts!
        exit /b 1
    )
)

REM Re-run configure so target/source list changes are picked up without requiring a clean build.
echo Refreshing CMake configuration...
"%CMAKE_EXE%" --preset %BUILD_PRESET%
if errorlevel 1 (
    echo ERROR: CMake configuration failed!
    exit /b 1
)
echo Configuration complete.
echo.

REM Build with Ninja
echo Building project with Ninja...
"%CMAKE_EXE%" --build out/build/%BUILD_PRESET% --config %BUILD_CONFIG%
if errorlevel 1 (
    echo ERROR: Build failed!
    exit /b 1
)
echo.

echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Output locations:
echo   Executable: output\drivers\00handoflesser\resources\bin\win64\HandOfLesser.exe
echo   Driver DLL: output\drivers\00handoflesser\bin\win64\driver_00handoflesser.dll
echo.

endlocal
