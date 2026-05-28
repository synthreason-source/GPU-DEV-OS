cd C:\uefidev

git clone https://github.com/tianocore/edk2.git

cd edk2

git submodule update --init --recursive


Invoke-WebRequest -Uri "https://github.com/tianocore/edk2-BaseTools-win32/archive/refs/heads/master.zip" -OutFile "C:\uefidev\BaseTools-win32.zip"

Expand-Archive -Path "C:\uefidev\BaseTools-win32.zip" -DestinationPath "C:\uefidev\BaseTools-win32"

Copy-Item "C:\uefidev\BaseTools-win32\edk2-BaseTools-win32-master\*" "C:\uefidev\edk2\BaseTools\Bin\Win32\" -Recurse -Force

just copy BOOTX64.EFI to EFI/BOOT/ in any drive

