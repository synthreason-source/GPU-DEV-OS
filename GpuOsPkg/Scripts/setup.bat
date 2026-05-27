@echo off
:: =============================================================================
:: setup.bat — First-time GpuOS toolchain installer
:: Run as Administrator. Installs LLVM, NASM, Python, Git, clones EDK2,
:: and places GpuOsPkg inside it — ready to build.
:: =============================================================================
setlocal enabledelayedexpansion

echo.
echo ============================================================
echo GpuOS Development Environment Setup
echo Installs: Python, LLVM/Clang, NASM, Git, EDK2
echo ============================================================
echo.

:: ── Resolve this script's own location ──────────────────────────────
:: Script lives at: \GpuOsPkg\Scripts\setup.bat
:: GpuOsPkg root is one level up from Scripts\
pushd "%~dp0.."
set GPUOS_PKG=%CD%
popd

echo GpuOsPkg source: %GPUOS_PKG%
echo EDK2 target: C:\uefidev\edk2\
echo.

:: ── Check admin ──────────────────────────────────────────────────────
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Please right-click setup.bat and choose "Run as Administrator".
    pause
    exit /b 1
)

:: ── Check winget ─────────────────────────────────────────────────────
where winget >nul 2>&1
if errorlevel 1 (
    echo [ERROR] winget not found.
    echo Install "App Installer" from the Microsoft Store, then re-run.
    pause
    exit /b 1
)

:: ── Install tools ────────────────────────────────────────────────────
echo [1/6] Installing Python 3...
winget install --id Python.Python.3.12 --silent --accept-source-agreements --accept-package-agreements
echo.

echo [2/6] Installing LLVM/Clang...
winget install --id LLVM.LLVM --silent --accept-source-agreements --accept-package-agreements
echo.

echo [3/6] Installing NASM...
winget install --id NASM.NASM --silent --accept-source-agreements --accept-package-agreements
echo.

echo [4/6] Installing Git...
winget install --id Git.Git --silent --accept-source-agreements --accept-package-agreements
echo.

:: ── Extend PATH for this session ─────────────────────────────────────
set "PATH=%PATH%;C:\Program Files\LLVM\bin;C:\Program Files\NASM;C:\Program Files\Git\cmd"

:: ── Clone EDK2 ───────────────────────────────────────────────────────
echo [5/6] Setting up EDK2 at C:\uefidev\edk2...
if not exist "C:\uefidev" mkdir C:\uefidev
cd C:\uefidev

if exist "edk2\.git" (
    echo EDK2 already cloned, pulling latest...
    cd edk2
    git pull --quiet
    git submodule update --init --recursive --quiet
) else (
    echo Cloning EDK2 (this may take a few minutes)...
    git clone https://github.com/tianocore/edk2.git
    cd edk2
    git submodule update --init --recursive
)

:: ── Bootstrap EDK2 build tools ───────────────────────────────────────
echo.
echo [6/6] Bootstrapping EDK2 build system...
call edksetup.bat Rebuild

:: ── Copy GpuOsPkg into edk2\ ─────────────────────────────────────────
echo.
echo Copying GpuOsPkg into EDK2...
set DST=C:\uefidev\edk2\GpuOsPkg

if exist "%DST%" (
    echo Updating existing GpuOsPkg...
    xcopy /E /I /Y "%GPUOS_PKG%" "%DST%" >nul
) else (
    xcopy /E /I /Y "%GPUOS_PKG%" "%DST%" >nul
    echo Copied to %DST%
)

:: ── Write Conf\target.txt ─────────────────────────────────────────────
echo.
echo Writing EDK2 build config...
(
    echo ACTIVE_PLATFORM = GpuOsPkg/GpuOsPkg.dsc
    echo TARGET          = DEBUG
    echo TARGET_ARCH     = X64
    echo TOOL_CHAIN_TAG  = CLANGPDB
    echo BUILD_RULE_CONF = Conf/build_rule.txt
) > "C:\uefidev\edk2\Conf\target.txt"

echo.
echo ============================================================
echo Setup Complete!
echo ============================================================
echo.
echo Everything is at: C:\uefidev\edk2\GpuOsPkg\
echo.
echo NEXT STEPS:
echo.
echo 1. Open a NEW Command Prompt (so PATH changes take effect)
echo 2. Run: C:\uefidev\edk2\GpuOsPkg\Scripts\build_and_deploy.bat
echo.
echo To test in QEMU before booting real hardware:
echo a) Download OVMF.fd from:
echo    https://retrage.github.io/edk2-nightly/
echo    (file named RELEASEX64_OVMF.fd — rename to OVMF.fd)
echo b) Place it at: C:\uefidev\edk2\GpuOsPkg\OVMF.fd
echo c) Run: C:\uefidev\edk2\GpuOsPkg\Scripts\run_qemu.bat
echo.
pause
