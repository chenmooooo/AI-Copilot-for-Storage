@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

set BUILD_DIR=build_vs2022
set GENERATOR="Visual Studio 17 2022"
set ARCH=x64

rem --- Proxy setup ---
rem Usage: %~nx0 [proxy_url]
rem   e.g.: %~nx0 http://127.0.0.1:7897
rem   Or set http_proxy / https_proxy environment variables.
set PROXY_ARGS=
if not "%~1"=="" (
    set PROXY_ARGS=-DHTTP_PROXY=%~1
) else if not "%http_proxy%"=="" (
    set PROXY_ARGS=-DHTTP_PROXY=%http_proxy%
)

echo ========================================
echo  Generator  : %GENERATOR% (%ARCH%)
echo  Build dir  : %BUILD_DIR%
if not "!PROXY_ARGS!"=="" echo  Proxy      : %~1
echo ========================================

cmake -B %BUILD_DIR% -G %GENERATOR% -A %ARCH% !PROXY_ARGS!

if !ERRORLEVEL! EQU 0 (
    echo.
    echo  SUCCESS: Open %CD%\%BUILD_DIR%\AICopilotForStorage.sln in VS 2022
) else (
    echo.
    echo  FAILED. Common causes:
    echo    - Network issue: try "%~nx0 http://your-proxy-ip:port"
    echo    - Or set environment variable: set http_proxy=http://your-proxy-ip:port
    pause
    exit /b !ERRORLEVEL!
)

endlocal
