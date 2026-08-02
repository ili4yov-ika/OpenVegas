@echo off
chcp 65001 >nul
REM OpenVegas Windows installer build script
REM Requires: CMake, Qt6 (Widgets+Svg), NSIS

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_ROOT%\build\Windows-MinGW-x64"
set "INSTALL_DIR=%BUILD_DIR%\install"
set "NSIS_SCRIPT=%SCRIPT_DIR%nsis_installer.nsi"

echo ========================================
echo OpenVegas - Windows Installer Builder
echo ========================================
echo.

set "MAKENSIS="
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    set "MAKENSIS=C:\Program Files (x86)\NSIS\makensis.exe"
) else if exist "C:\Program Files\NSIS\makensis.exe" (
    set "MAKENSIS=C:\Program Files\NSIS\makensis.exe"
) else (
    where makensis >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        for /f "delims=" %%i in ('where makensis 2^>nul') do set "MAKENSIS=%%i" & goto :makensis_ok
    )
)
:makensis_ok
if "%MAKENSIS%"=="" (
    echo [ERROR] NSIS ^(makensis^) not found!
    echo Install NSIS: https://nsis.sourceforge.io/Download
    echo Or add NSIS to PATH
    exit /b 1
)

set "CMAKE_CMD="
if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files\CMake\bin\cmake.exe"
) else if exist "C:\Program Files (x86)\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files (x86)\CMake\bin\cmake.exe"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) else (
    where cmake >nul 2>&1
    if %ERRORLEVEL% EQU 0 set "CMAKE_CMD=cmake"
)
if "%CMAKE_CMD%"=="" (
    echo [ERROR] CMake not found! Add CMake to PATH or install it.
    exit /b 1
)

REM Generate logo.ico for EXE icon if missing
if not exist "%PROJECT_ROOT%\resources\icons\logo.ico" (
    echo [0/5] Generating logo.ico from logo.svg...
    python "%SCRIPT_DIR%svg_to_ico.py" 2>nul
    if not exist "%PROJECT_ROOT%\resources\icons\logo.ico" (
        echo [WARN] logo.ico not created - EXE will use default icon. Install Inkscape + Pillow.
    )
)

echo [1/5] CMake configure...
cd /d "%PROJECT_ROOT%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

set "EXTRA_CMAKE="
if defined CMAKE_PREFIX_PATH (
    set "EXTRA_CMAKE=-DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH%"
)

"%CMAKE_CMD%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" %EXTRA_CMAKE% "%PROJECT_ROOT%"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configure failed!
    exit /b 1
)

echo [2/5] Building...
"%CMAKE_CMD%" --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo [3/5] Installing to %INSTALL_DIR%...
"%CMAKE_CMD%" --install . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Install failed!
    exit /b 1
)

echo [4/5] Copying Qt dependencies ^(windeployqt^)...
if not exist "%INSTALL_DIR%\bin\OpenVegas.exe" (
    echo [ERROR] OpenVegas.exe not found in %INSTALL_DIR%\bin\
    exit /b 1
)
set "WINDEPLOYQT="
if exist "C:\Qt\6.9.3\mingw_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.9.3\mingw_64\bin\windeployqt.exe"
if exist "C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe"
if exist "C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe"
if exist "C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe"
if defined CMAKE_PREFIX_PATH if exist "%CMAKE_PREFIX_PATH%\bin\windeployqt.exe" set "WINDEPLOYQT=%CMAKE_PREFIX_PATH%\bin\windeployqt.exe"
where windeployqt >nul 2>&1
if %ERRORLEVEL% EQU 0 if "%WINDEPLOYQT%"=="" (
    for /f "delims=" %%i in ('where windeployqt 2^>nul') do set "WINDEPLOYQT=%%i" & goto :windeploy_ok
)
:windeploy_ok
if defined WINDEPLOYQT (
    "%WINDEPLOYQT%" --release "%INSTALL_DIR%\bin\OpenVegas.exe"
    if %ERRORLEVEL% NEQ 0 echo [WARN] windeployqt finished with error
) else (
    echo [WARN] windeployqt not found - installer may lack Qt DLLs
)

echo [5/5] Creating NSIS installer...
cd /d "%SCRIPT_DIR%"
pushd "%INSTALL_DIR%" 2>nul
if %ERRORLEVEL% EQU 0 (
    set "INSTALL_ABS=%CD%"
    popd
) else (
    set "INSTALL_ABS=%INSTALL_DIR%"
)
"%MAKENSIS%" /DBUILD_DIR="%INSTALL_ABS%" "%NSIS_SCRIPT%"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] NSIS installer creation failed!
    exit /b 1
)

echo.
echo ========================================
echo Done! Installer: %SCRIPT_DIR%OpenVegas_Setup.exe
echo ========================================

endlocal
