@echo off
setlocal

rem Build and run the log parser tests.
rem Usage: build_and_run.bat [Debug^|Release^|RelWithDebInfo^|MinSizeRel]

set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"

if /I not "%CONFIG%"=="Debug" if /I not "%CONFIG%"=="Release" if /I not "%CONFIG%"=="RelWithDebInfo" if /I not "%CONFIG%"=="MinSizeRel" (
    echo Invalid configuration: %CONFIG%
    echo Usage: %~nx0 [Debug^|Release^|RelWithDebInfo^|MinSizeRel]
    exit /b 2
)

set "SOURCE_DIR=%~dp0.."
set "BUILD_DIR=%~dp0build"
set "TEST_EXE=%BUILD_DIR%\logparse\%CONFIG%\logparse_test.exe"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found in PATH.
    exit /b 1
)

echo [1/3] Configuring logparse tests (%CONFIG%)...
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo CMake configure failed.
    exit /b 1
)

echo [2/3] Building logparse_test...
cmake --build "%BUILD_DIR%" --config "%CONFIG%" --target logparse_test --parallel
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if not exist "%TEST_EXE%" (
    echo Test executable not found: %TEST_EXE%
    exit /b 1
)

echo [3/3] Running logparse_test...
"%TEST_EXE%"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" echo Tests failed with exit code %EXIT_CODE%.
exit /b %EXIT_CODE%
