@echo off
chcp 65001 >nul
setlocal EnableExtensions DisableDelayedExpansion

rem 一键构建并部署 Viewer 及 plugins-dev 下的全部插件。
rem 默认用于双击运行；自动化调用可传入 --no-pause 跳过末尾暂停。

set "ROOT_DIR=%~dp0"
set "VIEWER_SOLUTION=%ROOT_DIR%project\vc143\CsvViewer.sln"
set "VIEWER_UI_PROJECT=%ROOT_DIR%project\vc143\UI\UI.vcxproj"
set "VIEWER_LIB_DIR=%ROOT_DIR%lib"
set "PLUGIN_DEV_DIR=%ROOT_DIR%plugins-dev"
set "VIEWER_PLUGIN_PACKAGE_ROOT=%VIEWER_LIB_DIR%\plugins"
set "DEPLOY_DIR=%ROOT_DIR%deploy"
set "DEPLOY_PLUGIN_DIR=%DEPLOY_DIR%\plugins"
set "BUILD_CONFIGURATION=Release"
set "BUILD_PLATFORM=x64"
set "NO_PAUSE="

if /i "%~1"=="--no-pause" set "NO_PAUSE=1"

echo ============================================================
echo  构建 Viewer 及全部开发插件（%BUILD_CONFIGURATION%^|%BUILD_PLATFORM%）
echo ============================================================

call :find_msbuild
if errorlevel 1 goto :failed

call :initialize_vs_environment
if errorlevel 1 goto :failed

call :find_windeployqt
if errorlevel 1 goto :failed

if not exist "%VIEWER_SOLUTION%" (
    echo [错误] 找不到 Viewer 解决方案："%VIEWER_SOLUTION%"
    goto :failed
)

echo.
echo [1/4] 构建 Viewer...
call :build_solution "%VIEWER_SOLUTION%"
if errorlevel 1 goto :failed

echo.
echo [2/4] 扫描并构建 plugins-dev 下的插件...
set "PLUGIN_FOUND="
for /d %%D in ("%PLUGIN_DEV_DIR%\*") do if exist "%%~fD\plugin.json" (
    set "PLUGIN_FOUND=1"
    call :build_plugin "%%~fD"
    if errorlevel 1 goto :failed
)

if not defined PLUGIN_FOUND (
    echo [错误] "%PLUGIN_DEV_DIR%" 下未找到包含 plugin.json 的插件目录。
    goto :failed
)

echo.
echo [3/4] 部署 Viewer 运行时文件...
call :deploy_viewer
if errorlevel 1 goto :failed

echo.
echo [4/4] 部署插件（保留目标中已有的 data 目录）...
for /d %%D in ("%PLUGIN_DEV_DIR%\*") do if exist "%%~fD\plugin.json" (
    call :deploy_plugin "%%~fD"
    if errorlevel 1 goto :failed
)

echo.
echo ============================================================
echo  构建及部署成功："%DEPLOY_DIR%"
echo  已保留 deploy\user 及各插件已有的 data 目录。
echo ============================================================
call :maybe_pause
exit /b 0

:find_msbuild
set "MSBUILD_EXE="
set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem 工程使用 v143 工具集，优先选择 Visual Studio 2022（17.x）的 MSBuild。
if exist "%VSWHERE_EXE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE_EXE%" -latest -products * -version "[17.0,18.0)" -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do if not defined MSBUILD_EXE set "MSBUILD_EXE=%%~fI"
)

if not defined MSBUILD_EXE (
    for %%E in (Enterprise Professional Community BuildTools) do if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\MSBuild\Current\Bin\MSBuild.exe" set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\MSBuild\Current\Bin\MSBuild.exe"
)

if not defined MSBUILD_EXE (
    for /f "delims=" %%I in ('where msbuild.exe 2^>nul') do if not defined MSBUILD_EXE set "MSBUILD_EXE=%%~fI"
)

if not defined MSBUILD_EXE (
    echo [错误] 未找到 MSBuild。请安装带“使用 C++ 的桌面开发”工作负载的 Visual Studio 2022。
    exit /b 1
)

echo [工具] MSBuild："%MSBUILD_EXE%"
exit /b 0

:initialize_vs_environment
if defined VCINSTALLDIR exit /b 0

set "MSBUILD_DIR="
set "VS_INSTALL_DIR="
set "VSDEVCMD_BAT="
for %%I in ("%MSBUILD_EXE%") do set "MSBUILD_DIR=%%~dpI"
for %%I in ("%MSBUILD_DIR%..\..\..") do set "VS_INSTALL_DIR=%%~fI"
set "VSDEVCMD_BAT=%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat"

if not exist "%VSDEVCMD_BAT%" (
    echo [警告] 未找到 VsDevCmd.bat，windeployqt 可能无法部署 MSVC 运行库。
    exit /b 0
)

call "%VSDEVCMD_BAT%" -no_logo -arch=amd64 -host_arch=amd64
if errorlevel 1 (
    echo [错误] Visual Studio 构建环境初始化失败："%VSDEVCMD_BAT%"
    exit /b 1
)
exit /b 0

:find_windeployqt
set "WINDEPLOYQT_EXE="
set "QT_TOOLS_DIR="

rem 先使用 PATH；否则从 Viewer 工程的 Qt VS Tools 配置中读取工具目录。
for /f "delims=" %%I in ('where windeployqt.exe 2^>nul') do if not defined WINDEPLOYQT_EXE set "WINDEPLOYQT_EXE=%%~fI"

if not defined WINDEPLOYQT_EXE (
    for /f "usebackq delims=" %%I in (`"%MSBUILD_EXE%" "%VIEWER_UI_PROJECT%" /nologo /p:Configuration=%BUILD_CONFIGURATION% /p:Platform=%BUILD_PLATFORM% /getProperty:QtToolsPath 2^>nul`) do if not defined QT_TOOLS_DIR set "QT_TOOLS_DIR=%%I"
    if exist "%QT_TOOLS_DIR%\windeployqt.exe" set "WINDEPLOYQT_EXE=%QT_TOOLS_DIR%\windeployqt.exe"
)

if not defined WINDEPLOYQT_EXE (
    echo [错误] 未找到 windeployqt.exe。请检查 Qt VS Tools 中的 Qt 版本配置。
    exit /b 1
)

echo [工具] windeployqt："%WINDEPLOYQT_EXE%"
exit /b 0

:build_solution
echo [构建] "%~1"
"%MSBUILD_EXE%" "%~1" /m /nologo /t:Build /p:Configuration=%BUILD_CONFIGURATION% /p:Platform=%BUILD_PLATFORM% /p:PostBuildEventUseInBuild=false /verbosity:minimal
if errorlevel 1 (
    echo [错误] 构建失败："%~1"
    exit /b 1
)
exit /b 0

:build_plugin
setlocal
set "CURRENT_PLUGIN_DIR=%~1"
set "PLUGIN_SOLUTION_FOUND="

if not exist "%CURRENT_PLUGIN_DIR%\project" (
    echo [错误] 插件缺少 project 目录："%CURRENT_PLUGIN_DIR%"
    endlocal & exit /b 1
)

for /r "%CURRENT_PLUGIN_DIR%\project" %%S in (*.sln) do (
    set "PLUGIN_SOLUTION_FOUND=1"
    call :build_solution "%%~fS"
    if errorlevel 1 endlocal & exit /b 1
)

if not defined PLUGIN_SOLUTION_FOUND (
    echo [错误] 插件目录中未找到解决方案："%CURRENT_PLUGIN_DIR%"
    endlocal & exit /b 1
)

endlocal & exit /b 0

:deploy_viewer
if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
if errorlevel 1 (
    echo [错误] 无法创建部署目录："%DEPLOY_DIR%"
    exit /b 1
)

call :copy_required "%VIEWER_LIB_DIR%\CsvViewer.exe" "%DEPLOY_DIR%\"
if errorlevel 1 exit /b 1

set "RELEASE_DLL_FOUND="
for %%F in ("%VIEWER_LIB_DIR%\*_Release.dll") do if exist "%%~fF" (
    set "RELEASE_DLL_FOUND=1"
    copy /y /b "%%~fF" "%DEPLOY_DIR%\" >nul
    if errorlevel 1 (
        echo [错误] 无法部署："%%~fF"
        exit /b 1
    )
)

if not defined RELEASE_DLL_FOUND (
    echo [错误] 未找到 Viewer 的 Release DLL："%VIEWER_LIB_DIR%\*_Release.dll"
    exit /b 1
)

call :copy_required "%ROOT_DIR%extra\qads\qtadvanceddocking.dll" "%DEPLOY_DIR%\"
if errorlevel 1 exit /b 1
call :copy_required "%ROOT_DIR%extra\libzip\bin\zip.dll" "%DEPLOY_DIR%\"
if errorlevel 1 exit /b 1
call :copy_required "%ROOT_DIR%extra\zlib\bin\z.dll" "%DEPLOY_DIR%\"
if errorlevel 1 exit /b 1

rem windeployqt 仅更新 Qt 运行库及 Qt 插件，不会清理或写入 deploy\user。
call :deploy_qt_runtime "%DEPLOY_DIR%\CsvViewer.exe"
if errorlevel 1 exit /b 1
exit /b 0

:copy_required
if not exist "%~1" (
    echo [错误] 缺少待部署文件："%~1"
    exit /b 1
)
copy /y /b "%~1" "%~2" >nul
if errorlevel 1 (
    echo [错误] 无法部署："%~1"
    exit /b 1
)
echo [部署] "%~nx1"
exit /b 0

:deploy_plugin
setlocal
set "CURRENT_PLUGIN_DIR=%~1"
set "CURRENT_PLUGIN_MANIFEST=%CURRENT_PLUGIN_DIR%\plugin.json"
set "PLUGIN_PACKAGE_DIR="
set "PLUGIN_DEPLOY_NAME="
set "PLUGIN_SOURCE_DIR=%CURRENT_PLUGIN_DIR%\lib"
set "PLUGIN_DEST_DIR="
set "PLUGIN_ENTRY="
set "PLUGIN_DEBUG_ENTRY="
set "ROBOCOPY_DATA_ARGS="
set "ROBOCOPY_DEBUG_ARGS="

rem 依据 manifest.id 在 Viewer 的插件包目录中定位工程声明的部署目录名。
for /f "usebackq delims=" %%P in (`powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$wanted = (Get-Content -Raw -LiteralPath $env:CURRENT_PLUGIN_MANIFEST | ConvertFrom-Json).id; $matches = [Collections.Generic.List[string]]::new(); if ([IO.Directory]::Exists($env:VIEWER_PLUGIN_PACKAGE_ROOT)) { foreach ($dir in [IO.Directory]::GetDirectories($env:VIEWER_PLUGIN_PACKAGE_ROOT)) { $manifest = [IO.Path]::Combine($dir, 'plugin.json'); if ([IO.File]::Exists($manifest)) { try { if ((Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json).id -eq $wanted) { $matches.Add($dir) } } catch {} } } }; if ($matches.Count -eq 1) { [Console]::WriteLine($matches[0]) }"`) do if not defined PLUGIN_PACKAGE_DIR set "PLUGIN_PACKAGE_DIR=%%P"

if not defined PLUGIN_PACKAGE_DIR (
    echo [错误] 无法唯一定位插件的构建包："%CURRENT_PLUGIN_DIR%"
    endlocal & exit /b 1
)

if not exist "%PLUGIN_SOURCE_DIR%\plugin.json" (
    echo [错误] 插件构建包不完整："%PLUGIN_SOURCE_DIR%"
    endlocal & exit /b 1
)

rem Release 部署不复制包目录中可能残留的旧 Debug DLL。
set "CURRENT_PLUGIN_MANIFEST=%PLUGIN_SOURCE_DIR%\plugin.json"
for /f "usebackq delims=" %%E in (`powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$entry = [string]((Get-Content -Raw -LiteralPath $env:CURRENT_PLUGIN_MANIFEST | ConvertFrom-Json).debugEntry); if ([IO.Path]::GetFileName($entry) -eq $entry -and $entry.EndsWith('.dll', [StringComparison]::OrdinalIgnoreCase)) { [Console]::WriteLine($entry) }"`) do if not defined PLUGIN_DEBUG_ENTRY set "PLUGIN_DEBUG_ENTRY=%%E"
if defined PLUGIN_DEBUG_ENTRY set "ROBOCOPY_DEBUG_ARGS=/xf "%PLUGIN_DEBUG_ENTRY%""

for %%P in ("%PLUGIN_PACKAGE_DIR%") do set "PLUGIN_DEPLOY_NAME=%%~nxP"
set "PLUGIN_DEST_DIR=%DEPLOY_PLUGIN_DIR%\%PLUGIN_DEPLOY_NAME%"

if not exist "%PLUGIN_DEST_DIR%" mkdir "%PLUGIN_DEST_DIR%"
if errorlevel 1 (
    echo [错误] 无法创建插件部署目录："%PLUGIN_DEST_DIR%"
    endlocal & exit /b 1
)

if exist "%PLUGIN_DEST_DIR%\data\" (
    echo [保留] "%PLUGIN_DEST_DIR%\data"
    set "ROBOCOPY_DATA_ARGS=/xd "%PLUGIN_SOURCE_DIR%\data""
)

robocopy "%PLUGIN_SOURCE_DIR%" "%PLUGIN_DEST_DIR%" /e %ROBOCOPY_DATA_ARGS% %ROBOCOPY_DEBUG_ARGS% /r:2 /w:1 /nfl /ndl /njh /njs /np

if errorlevel 8 (
    echo [错误] 插件部署失败："%PLUGIN_SOURCE_DIR%"
    endlocal & exit /b 1
)

rem 使用 manifest.entry 找到 Release 入口 DLL，并补齐该插件需要的 Qt 运行库。
set "CURRENT_PLUGIN_MANIFEST=%PLUGIN_DEST_DIR%\plugin.json"
for /f "usebackq delims=" %%E in (`powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$entry = [string]((Get-Content -Raw -LiteralPath $env:CURRENT_PLUGIN_MANIFEST | ConvertFrom-Json).entry); if ([IO.Path]::GetFileName($entry) -eq $entry -and $entry.EndsWith('.dll', [StringComparison]::OrdinalIgnoreCase)) { [Console]::WriteLine($entry) }"`) do if not defined PLUGIN_ENTRY set "PLUGIN_ENTRY=%%E"

if not defined PLUGIN_ENTRY (
    echo [错误] plugin.json 中缺少有效的 Release 入口 DLL："%CURRENT_PLUGIN_MANIFEST%"
    endlocal & exit /b 1
)

if not exist "%PLUGIN_DEST_DIR%\%PLUGIN_ENTRY%" (
    echo [错误] 插件入口 DLL 不存在："%PLUGIN_DEST_DIR%\%PLUGIN_ENTRY%"
    endlocal & exit /b 1
)

call :deploy_qt_runtime "%PLUGIN_DEST_DIR%\%PLUGIN_ENTRY%"
if errorlevel 1 endlocal & exit /b 1

echo [部署] 插件 "%PLUGIN_DEPLOY_NAME%"
endlocal & exit /b 0

:deploy_qt_runtime
"%WINDEPLOYQT_EXE%" --release --force --dir "%DEPLOY_DIR%" "%~1"
if errorlevel 1 (
    echo [错误] Qt 运行库部署失败，目标文件："%~1"
    exit /b 1
)
exit /b 0

:maybe_pause
if not defined NO_PAUSE pause
exit /b 0

:failed
echo.
echo ============================================================
echo  构建或部署失败，请查看上方错误信息。
echo  deploy\user 及已有插件 data 目录未被清理。
echo ============================================================
call :maybe_pause
exit /b 1
