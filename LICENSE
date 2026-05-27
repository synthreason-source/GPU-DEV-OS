@echo off
:: =============================================================================
:: build_and_deploy.bat — GpuOS automated build and USB deploy script
:: Can be double-clicked OR run from any directory — path is auto-resolved.
:: =============================================================================
setlocal enabledelayedexpansion

echo.
echo  ============================================================
echo   GpuOS Build ^& Deploy Script
echo   Bare-metal UEFI GPU pipeline — No OS loaded
echo  ============================================================
echo.

:: ── Resolve EDK2 root: this script lives in edk2\GpuOsPkg\Scripts\
:: %~dp0 = absolute path to this script's directory (with trailing \)
:: We go up two levels: Scripts\ -> GpuOsPkg\ -> edk2\
pushd "%~dp0..\.."
set EDK2_DIR=%CD%
popd

set PKG_DIR=%EDK2_DIR%\GpuOsPkg
set BUILD_OUTPUT=%EDK2_DIR%\Build\GpuOsPkg\DEBUG_CLANGPDB\X64\GpuOs.efi
set USB_DRIVE=

echo   EDK2 root: %EDK2_DIR%
echo.

:: ── Verify this is actually an EDK2 directory ────────────────────────
if not exist "%EDK2_DIR%\edksetup.bat" (
    echo [ERROR] edksetup.bat not found at: %EDK2_DIR%
    echo.
    echo  Make sure the folder structure is:
    echo    C:\uefidev\edk2\              ^<-- EDK2 root
    echo    C:\uefidev\edk2\GpuOsPkg\    ^<-- this package
    echo    C:\uefidev\edk2\GpuOsPkg\Scripts\build_and_deploy.bat  ^<-- this script
    echo.
    echo  Run setup.bat first if you haven't cloned EDK2 yet.
    pause
    exit /b 1
)

:: ── Verify GpuOsPkg is in place ──────────────────────────────────────
if not exist "%PKG_DIR%\GpuOsPkg.dsc" (
    echo [ERROR] GpuOsPkg.dsc not found at: %PKG_DIR%
    echo  Make sure GpuOsPkg\ is placed inside the edk2\ root directory.
    pause
    exit /b 1
)

:: ── Step 1: Build ────────────────────────────────────────────────────
echo [1/3] Building GpuOs.efi with EDK2 + CLANGPDB...
echo.
cd /d "%EDK2_DIR%"
call edksetup.bat >nul 2>&1

build -p GpuOsPkg\GpuOsPkg.dsc ^
      -a X64 ^
      -t CLANGPDB ^
      -b DEBUG ^
      -n 4

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Check output above for details.
    echo.
    echo  Common fixes:
    echo    - Ensure clang is on PATH:  clang --version
    echo    - Ensure nasm is on PATH:   nasm --version
    echo    - Ensure python is on PATH: python --version
    echo    - Re-run setup.bat as Administrator
    pause
    exit /b 1
)

if not exist "%BUILD_OUTPUT%" (
    echo [ERROR] Build succeeded but output not found:
    echo         %BUILD_OUTPUT%
    pause
    exit /b 1
)

echo.
echo [OK] Build succeeded:
echo      %BUILD_OUTPUT%
echo.

:: ── Step 2: Find USB drive ───────────────────────────────────────────
echo [2/3] Searching for USB FAT32 drive...
for %%D in (D E F G H I J K L M N O P) do (
    if exist "%%D:\" (
        for /f "tokens=2 delims==" %%F in (
            'wmic logicaldisk where "DeviceID='%%D:'" get FileSystem /value 2^>nul'
        ) do (
            if /i "%%F"=="FAT32" (
                set USB_DRIVE=%%D
                echo      Found FAT32 volume at %%D:\
            )
        )
    )
)

if "!USB_DRIVE!"=="" (
    echo.
    echo [WARN] No FAT32 USB drive detected automatically.
    echo  Make sure your USB is formatted as FAT32.
    echo.
    set /p USB_DRIVE="  Enter your USB drive letter (e.g. E, no colon): "
    if "!USB_DRIVE!"=="" (
        echo [ERROR] No drive specified. Exiting.
        pause
        exit /b 1
    )
)

echo.
echo   Target: !USB_DRIVE!:\EFI\BOOT\BOOTX64.EFI
echo.
set /p CONFIRM="  Continue with deploy? (Y/N): "
if /i not "!CONFIRM!"=="Y" (
    echo   Cancelled.
    exit /b 0
)

:: ── Step 3: Deploy to USB ────────────────────────────────────────────
echo.
echo [3/3] Deploying to USB...

if not exist "!USB_DRIVE!:\EFI\BOOT\" (
    echo   Creating \EFI\BOOT\ directory...
    mkdir "!USB_DRIVE!:\EFI\BOOT"
    if errorlevel 1 (
        echo [ERROR] Could not create directory on !USB_DRIVE!:\ — is it write-protected?
        pause
        exit /b 1
    )
)

copy /Y "%BUILD_OUTPUT%" "!USB_DRIVE!:\EFI\BOOT\BOOTX64.EFI"
if errorlevel 1 (
    echo [ERROR] Copy failed. Check: write protection, free space, correct drive letter.
    pause
    exit /b 1
)

echo.
echo  ============================================================
echo   SUCCESS — GpuOS deployed to USB
echo  ============================================================
echo.
echo   File: !USB_DRIVE!:\EFI\BOOT\BOOTX64.EFI
echo.
echo   Boot instructions:
echo     1. Safely eject the USB
echo     2. Reboot target machine, enter BIOS (DEL or F2)
echo     3. Disable Secure Boot
echo     4. Set USB as first boot device
echo     5. Save and Exit
echo        -^> GpuOS loads, Windows never starts
echo        -^> GPU owns display via direct VRAM framebuffer
echo.
echo   To test in QEMU without real hardware: run run_qemu.bat
echo.
pause
