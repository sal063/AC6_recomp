@echo off
setlocal enabledelayedexpansion

echo =========================================
echo Visual Studio Environment Initialization
echo =========================================

:: Check if already in a VS developer prompt
if not "%VCINSTALLDIR%"=="" (
    echo [OK] Already running in Visual Studio developer environment.
    goto :env_ready
)

:: Locate vswhere.exe
set "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE_PATH!" (
    set "VSWHERE_PATH=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if exist "!VSWHERE_PATH!" (
    echo Locating Visual Studio installation...
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE_PATH!" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
    
    if not "!VS_INSTALL_DIR!"=="" (
        set "VCVARS_BAT=!VS_INSTALL_DIR!\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!VCVARS_BAT!" (
            echo [OK] Found Visual Studio variables script: !VCVARS_BAT!
            echo Initializing x64 Developer Environment...
            
            :: Call vcvars64.bat inside a temporary environment block, then preserve env
            :: We do this by calling it and dumping the env changes
            call "!VCVARS_BAT!" >nul
            goto :env_ready
        )
    )
)

echo [WARNING] Could not automatically initialize Visual Studio developer environment.
echo If compilation fails, please run this script from the "x64 Native Tools Command Prompt for VS".
echo.

:env_ready

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
    echo Please install LLVM/Clang or select the "C++ Clang tools for Windows" component in the Visual Studio Installer.
    pause
    exit /b 1
)
clang++ --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Clang++ not found on PATH.
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
    echo Please install it via the Visual Studio Installer.
    pause
    exit /b 1
)
echo [OK] All prerequisites found!
echo.

echo =========================================
echo Environment Validation
echo =========================================
if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" (
    echo [ERROR] Detected an x86 Visual Studio developer environment.
    echo This project must be configured from an x64 environment.
    echo Please run this script in an x64 Native Tools prompt.
    pause
    exit /b 1
)
if /I "%Platform%"=="x86" (
    echo [ERROR] Detected Platform=x86 in the current shell.
    echo This project must be configured for a 64-bit target.
    pause
    exit /b 1
)
echo [OK] Target environment is 64-bit.
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
        pause
        exit /b 1
    )
)

set EXTRACT_DIR=assets
if exist "!EXTRACT_DIR!\default.xex" (
    echo [OK] '!EXTRACT_DIR!\default.xex' already exists. Skipping extraction.
) else (
    echo Performing checksum...
    set "ISO_CHECKSUM="
    for /f "skip=1 tokens=*" %%h in ('certutil -hashfile "!ISO_FILE!" SHA256') do (
        set "ISO_CHECKSUM=%%h"
        goto :_iso_checksum_goto
    )
    :_iso_checksum_goto
    rem Remove any spaces that certutil inserts between byte groups
    set "ISO_CHECKSUM=!ISO_CHECKSUM: =!"
    if "!ISO_CHECKSUM!"=="" (
        echo [ERROR] Failed to calculate ISO checksum.
        pause
        exit /b 1
    )
    echo ISO checksum: !ISO_CHECKSUM!

    set ISO_REGION_CODE=2
    :: NTSC-J / NTSC-U Version
    if !ISO_CHECKSUM! == 204c5e645d79da8776699c12f17bd069f869fbdb10ae79015d1a0ef2b743c98c (
        echo [INFO] NTSC-J / NTSC-U Version detected.
        set ISO_REGION_CODE=0
    )
    :: EU Rev 1
    if !ISO_CHECKSUM! == c1a5a5aa773197b3f171716e3d6a04ac3fde31f42edd969765ee6e38a4892b5f (
        echo [INFO] EU Revision 1 detected.
        set ISO_REGION_CODE=1
    )
    :: EU Original Revision
    if !ISO_CHECKSUM! == ae078ef27df875fe706536e86f37d6653b0c01aa214f693d3f96d576483a462e (
        echo [INFO] EU Original Revision detected.
        set ISO_REGION_CODE=1
    )

    :: PAL Region Detected
    if !ISO_REGION_CODE! equ 1 (
        echo [ERROR] PAL Version detected. PAL Version is currently not supported. You can try recompiling manually, but it probably won't work.
        pause
        exit /b 1
    )
    :: Checksum not recognized for any known region
    if !ISO_REGION_CODE! equ 2 (
        echo [ERROR] Checksum failed. The ISO file may be corrupted or an unsupported version. You can try recompiling manually, but it might not work. 
        pause
        exit /b 1
    )

    if !ISO_REGION_CODE! equ 0 (
        set ISO_REGION=NTSC
    )
    echo [OK] Checksum complete. !ISO_REGION! version detected.

    echo Extracting '!ISO_FILE!' to '!EXTRACT_DIR!' directory...
    if not exist "!EXTRACT_DIR!" mkdir "!EXTRACT_DIR!"
    !EXTRACT_XISO_EXE! -d "!EXTRACT_DIR!" "!ISO_FILE!"
    if !errorlevel! neq 0 (
        echo [ERROR] Failed to extract ISO.
        pause
        exit /b 1
    )
    echo [OK] Extraction complete!
)
echo.

echo =========================================
echo Building the Game (MSVC Backend)
echo =========================================
:: Check for clean build argument
set "clean_build=0"
if "%~1"=="--clean" set "clean_build=1"
if "%~1"=="-clean" set "clean_build=1"
if "%~1"=="clean" set "clean_build=1"

if "!clean_build!"=="1" (
    echo Cleaning previous build directory out...
    if exist "out" rd /s /q "out"
) else (
    echo Incremental build enabled. Use --clean argument for a clean build.
)

echo Step 1: Configuring CMake...
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

echo Step 3: Re-configuring CMake...
cmake --preset win-amd64-relwithdebinfo
if !errorlevel! neq 0 (
    echo [ERROR] Re-configuration failed.
    pause
    exit /b 1
)

echo Step 4: Building the runtime...
cmake --build --preset win-amd64-relwithdebinfo > build.log 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] Build failed. Showing the last 50 lines of build.log:
    echo --------------------------------------------------
    powershell -Command "Get-Content build.log -Tail 50"
    echo --------------------------------------------------
    pause
    exit /b 1
)

echo [SUCCESS] Setup and Build completed successfully!
pause
exit /b 0