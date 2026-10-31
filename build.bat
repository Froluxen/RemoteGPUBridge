@echo off
setlocal enabledelayedexpansion

rem Script de compilacion para Windows (MSVC + Ninja + Vulkan SDK).

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

if "%VULKAN_SDK%"=="" (
    if exist "C:\VulkanSDK\1.4.363.0" set "VULKAN_SDK=C:\VulkanSDK\1.4.363.0"
)
if "%VULKAN_SDK%"=="" (
    echo [ERROR] VULKAN_SDK no esta definido. Instala el Vulkan SDK.
    exit /b 1
)

set "PATH=%VULKAN_SDK%\Bin;C:\Program Files\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"

if "%VSPATH%"=="" (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if "%VSPATH%"=="" (
    echo [ERROR] No se encontro Visual Studio Build Tools con el workload C++.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat fallo.
    exit /b 1
)

set "BUILD_DIR=%ROOT%\build"
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] Configuracion de CMake fallida.
    exit /b 1
)

cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo [ERROR] Compilacion fallida.
    exit /b 1
)

echo.
echo [OK] Compilacion completada: %BUILD_DIR%\pc-server\gpu-info.exe
