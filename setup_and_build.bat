@echo off
setlocal enabledelayedexpansion

echo =========================================
echo Prerequisites Check
echo =========================================

:: Check CMake
cmake --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found on PATH.
    echo Please download and install CMake 3.25+ from https://cmake.org/download/
    echo Make sure to add CMake to the system PATH during installation.
    pause
    exit /b 1
)

:: Check Ninja
ninja --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Ninja not found on PATH.
    echo Please download Ninja from https://github.com/ninja-build/ninja/releases and add it to your PATH.
    pause
    exit /b 1
)

:: Check Clang
clang --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Clang not found on PATH.
    echo Please download LLVM/Clang from https://github.com/llvm/llvm-project/releases or via Visual Studio Installer ^(C++ Clang tools^) and add to PATH.
    pause
    exit /b 1
)
clang++ --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Clang++ not found on PATH.
    echo Please ensure clang++ is available.
    pause
    exit /b 1
)

:: Check Windows SDK
set sdk_found=0
reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0" /v InstallationFolder >nul 2>&1
if !errorlevel! equ 0 set sdk_found=1
reg query "HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0" /v InstallationFolder >nul 2>&1
if !errorlevel! equ 0 set sdk_found=1

if !sdk_found! equ 0 (
    echo [ERROR] Windows SDK 10.0.19041+ not found in registry.
    echo Please install it via the Visual Studio Installer by selecting the "Windows 10 SDK (10.0.19041.0)" or newer under the "Desktop development with C++" workload.
    pause
    exit /b 1
)
echo [OK] All prerequisites found!
echo.

echo =========================================
echo Environment Check
echo =========================================
if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" (
    echo [ERROR] Detected an x86 Visual Studio developer environment.
    echo This project must be configured from an x64 environment.
    echo Close this shell and reopen a normal 64-bit PowerShell/CMD window, or use an x64 Native Tools prompt.
    pause
    exit /b 1
)
if /I "%Platform%"=="x86" (
    echo [ERROR] Detected Platform=x86 in the current shell.
    echo This project must be configured for a 64-bit target.
    echo Close this shell and reopen a normal 64-bit PowerShell/CMD window, or use an x64 Native Tools prompt.
    pause
    exit /b 1
)
echo [OK] No x86-only VS environment detected.
echo.

echo =========================================
echo Git Branch Check
echo =========================================
for /f "delims=" %%i in ('git rev-parse --abbrev-ref HEAD') do set CURRENT_BRANCH=%%i
echo Current branch: !CURRENT_BRANCH!
if /I "!CURRENT_BRANCH!"=="main" (
    echo Switching from main to dev-test branch...
    git checkout dev-test
    if !errorlevel! neq 0 (
        echo [ERROR] Failed to checkout dev-test branch.
        pause
        exit /b 1
    )
) else (
    echo Already on !CURRENT_BRANCH! branch or not on main.
)
echo.

echo =========================================
echo ISO Detection and Extraction
echo =========================================
set ISO_FILE=
for %%f in (*.iso) do (
    set ISO_FILE=%%f
    goto :found_iso
)
:found_iso
if "!ISO_FILE!"=="" (
    echo [ERROR] No .iso file found in the current directory.
    echo Please place the Ace Combat 6 ISO in this folder.
    pause
    exit /b 1
)
echo Found ISO: !ISO_FILE!

set EXTRACT_XISO_EXE=extract-xiso.exe
if not exist "!EXTRACT_XISO_EXE!" (
    echo extract-xiso not found. Downloading...
    powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://github.com/XboxDev/extract-xiso/releases/download/build-202505152050/extract-xiso-Win32_Release.zip' -OutFile 'extract-xiso.zip'; Expand-Archive -Path 'extract-xiso.zip' -DestinationPath 'extract-xiso-temp' -Force; Move-Item -Path 'extract-xiso-temp\artifacts\extract-xiso.exe' -Destination '.' -Force; Remove-Item 'extract-xiso.zip'; Remove-Item 'extract-xiso-temp' -Recurse -Force"
    if not exist "!EXTRACT_XISO_EXE!" (
        echo [ERROR] Failed to download or extract extract-xiso.
        echo Please download it manually from https://github.com/XboxDev/extract-xiso/releases and place extract-xiso.exe in this folder.
        pause
        exit /b 1
    )
)

set EXTRACT_DIR=assets
echo Extracting '!ISO_FILE!' to '!EXTRACT_DIR!' directory...
if not exist "!EXTRACT_DIR!" mkdir "!EXTRACT_DIR!"
!EXTRACT_XISO_EXE! -d "!EXTRACT_DIR!" "!ISO_FILE!"
if !errorlevel! neq 0 (
    echo [ERROR] Failed to extract ISO.
    pause
    exit /b 1
)
echo [OK] Extraction complete!
echo.

echo =========================================
echo Building the Game
echo =========================================
echo Step 1: Configuring...
cmake --preset win-amd64-relwithdebinfo
if !errorlevel! neq 0 (
    echo [ERROR] Configuration failed.
    pause
    exit /b 1
)

echo Step 2: Generating recompiled code...
cmake --build --preset win-amd64-relwithdebinfo --target ac6recomp_codegen
if !errorlevel! neq 0 (
    echo [ERROR] Codegen failed.
    pause
    exit /b 1
)

echo Step 3: Re-configuring...
cmake --preset win-amd64-relwithdebinfo
if !errorlevel! neq 0 (
    echo [ERROR] Re-configuration failed.
    pause
    exit /b 1
)

echo Step 4: Building the runtime...
cmake --build --preset win-amd64-relwithdebinfo
if !errorlevel! neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo [SUCCESS] Setup and Build completed successfully!
pause
exit /b 0
