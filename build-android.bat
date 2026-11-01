@echo off
setlocal

rem Compila el APK de Android (debug) con JDK 17 y el SDK instalado.
set "JAVA_HOME=C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"
set "ANDROID_HOME=C:\Users\ASUS\Android\Sdk"
set "PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\platform-tools;%PATH%"

set "ROOT=%~dp0"
set "GRADLE=C:\Users\ASUS\AppData\Local\Temp\opencode\gradle-8.9\bin\gradle.bat"

if exist "%ROOT%android-client\gradlew.bat" (
    call "%ROOT%android-client\gradlew.bat" -p "%ROOT%android-client" assembleDebug
) else (
    call "%GRADLE%" -p "%ROOT%android-client" assembleDebug
)

if errorlevel 1 (
    echo [ERROR] Compilacion Android fallida.
    exit /b 1
)

echo [OK] APK: %ROOT%android-client\app\build\outputs\apk\debug\app-debug.apk
