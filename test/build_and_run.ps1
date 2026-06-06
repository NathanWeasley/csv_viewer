# Build and run CSV Viewer unit tests (PowerShell)
# Usage: .\build_and_run.ps1 [Debug|Release]

param(
    [string]$Configuration = "Release"
)

Set-Location $PSScriptRoot

$BuildDir = "build"
$Generator = "Visual Studio 17 2022"
$Arch      = "x64"

Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  CSV Viewer Unit Test Build Script"            -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  Configuration: $Configuration"
Write-Host "  Generator:     $Generator $Arch"
Write-Host ""

# Step 1: CMake Configure
Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
}
Push-Location $BuildDir
try {
    cmake .. -G $Generator -A $Arch -DCMAKE_BUILD_TYPE=$Configuration
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configure failed!" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host "  Configure OK." -ForegroundColor Green

    # Step 2: Build
    Write-Host "[2/3] Building..." -ForegroundColor Yellow
    cmake --build . --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Write-Host "  Build OK." -ForegroundColor Green

} finally {
    Pop-Location
}

# Step 3: Run tests
Write-Host "[3/3] Running tests..." -ForegroundColor Yellow
$TestExe = Join-Path $BuildDir "datamgr" $Configuration "datamgr_test.exe"
if (-not (Test-Path $TestExe)) {
    Write-Host "Test executable not found: $TestExe" -ForegroundColor Red
    exit 1
}

& $TestExe
$ExitCode = $LASTEXITCODE
Write-Host ""
if ($ExitCode -eq 0) {
    Write-Host "Build and test completed SUCCESSFULLY." -ForegroundColor Green
} else {
    Write-Host "Tests FAILED (exit code: $ExitCode)." -ForegroundColor Red
}
exit $ExitCode