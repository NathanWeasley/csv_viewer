@echo off
REM Build and run CSV Viewer unit tests (CMD)
REM Usage: build_and_run.bat [Debug|Release]

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

cd /d "%~dp0"
set BUILD_DIR=build
set GENERATOR=Visual Studio 17 2022
set ARCH=x64

echo ================================================
echo   CSV Viewer Unit Test Build Script
echo ================================================
echo   Configuration: %CONFIG%
echo   Generator:     %GENERATOR% %ARCH%
echo.

REM Step 1: CMake Configure
echo [1/3] Configuring CMake...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
cmake .. -G "%GENERATOR%" -A %ARCH% -DCMAKE_BUILD_TYPE=%CONFIG%
if %ERRORLEVEL% neq 0 (
    echo CMake configure failed!
    exit /b %ERRORLEVEL%
)
echo   Configure OK.

REM Step 2: Build
echo [2/3] Building...
cmake --build . --config %CONFIG%
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)
echo   Build OK.
cd /d "%~dp0"

REM Step 3: Run tests
echo [3/3] Running tests...
set TEST_EXE=%BUILD_DIR%\datamgr\%CONFIG%\datamgr_test.exe
if not exist "%TEST_EXE%" (
    echo Test executable not found: %TEST_EXE%
    exit /b 1
)

"%TEST_EXE%"
set EXIT_CODE=%ERRORLEVEL%
echo.
if %EXIT_CODE% equ 0 (
    echo Build and test completed SUCCESSFULLY.
) else (
    echo Tests FAILED (exit code: %EXIT_CODE%).
)
exit /b %EXIT_CODE%