@echo off
:: =============================================================================
:: run_qemu.bat — Test GpuOs.efi in QEMU before deploying to real hardware
:: Requires: QEMU  (winget install SoftwareFreedomConservancy.QEMU)
::           OVMF.fd placed at GpuOsPkg\OVMF.fd
::             -> download from https://retrage.github.io/edk2-nightly/
::             -> get RELEASEX64_OVMF.fd, rename to OVMF.fd
:: =============================================================================
setlocal enabledelayedexpansion

:: ── Resolve paths ────────────────────────────────────────────────────
:: Script is at edk2\GpuOsPkg\Scripts\run_qemu.bat
pushd "%~dp0.."
set PKG_DIR=%CD%
popd

pushd "%~dp0..\.."
set EDK2_DIR=%CD%
popd

set EFI_BINARY=%EDK2_DIR%\Build\GpuOsPkg\DEBUG_CLANGPDB\X64\GpuOs.efi
set OVMF_FD=%PKG_DIR%\OVMF.fd
set USB_DIR=%TEMP%\gpuos_qemu_usb

echo.
echo  ============================================================
echo   GpuOS QEMU Test Runner
echo  ============================================================
echo.
echo   EDK2 root: %EDK2_DIR%
echo   Package:   %PKG_DIR%
echo   EFI:       %EFI_BINARY%
echo   OVMF:      %OVMF_FD%
echo.

:: ── Check QEMU ───────────────────────────────────────────────────────
where qemu-system-x86_64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] qemu-system-x86_64 not found on PATH.
    echo.
    echo  Install QEMU via:
    echo    winget install SoftwareFreedomConservancy.QEMU
    echo  Then open a new Command Prompt and re-run this script.
    pause
    exit /b 1
)

:: ── Check OVMF ───────────────────────────────────────────────────────
if not exist "%OVMF_FD%" (
    echo [ERROR] OVMF.fd not found at:
    echo         %OVMF_FD%
    echo.
    echo  Download steps:
    echo    1. Go to: https://retrage.github.io/edk2-nightly/
    echo    2. Download: RELEASEX64_OVMF.fd
    echo    3. Rename it to OVMF.fd
    echo    4. Place it at: %PKG_DIR%\OVMF.fd
    pause
    exit /b 1
)

:: ── Check EFI binary ─────────────────────────────────────────────────
if not exist "%EFI_BINARY%" (
    echo [ERROR] GpuOs.efi not found at:
    echo         %EFI_BINARY%
    echo.
    echo  Run build_and_deploy.bat first to compile the project.
    pause
    exit /b 1
)

:: ── Build virtual FAT32 USB image directory ──────────────────────────
if exist "%USB_DIR%" rmdir /s /q "%USB_DIR%"
mkdir "%USB_DIR%\EFI\BOOT"
copy /Y "%EFI_BINARY%" "%USB_DIR%\EFI\BOOT\BOOTX64.EFI" >nul
echo   Virtual USB image prepared.
echo.

echo  Starting QEMU... (close the QEMU window to exit)
echo.

qemu-system-x86_64 ^
    -bios "%OVMF_FD%" ^
    -drive format=raw,file=fat:rw:"%USB_DIR%" ^
    -net none ^
    -vga std ^
    -m 512M ^
    -display sdl ^
    -no-reboot

echo.
echo   QEMU session ended.
pause
