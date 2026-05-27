## GpuOsPkg.dsc — Package descriptor for bare-metal GPU OS
## Bare-metal UEFI → GPU framebuffer → direct display pipeline

[Defines]
  PLATFORM_NAME           = GpuOsPkg
  PLATFORM_GUID           = 11111111-2222-3333-4444-555555555555
  PLATFORM_VERSION        = 0.1
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/GpuOsPkg
  SUPPORTED_ARCHITECTURES = X64
  BUILD_TARGETS           = DEBUG|RELEASE
  
[BuildOptions]
  MSFT:*_*_*_CC_FLAGS = /GS-
  
[LibraryClasses]
  StackCheckLib|GpuOsPkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  StackCheckLib|GpuOsPkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
[Packages]
  MdePkg/MdePkg.dec

[Components]
  GpuOsPkg/Main/Main.inf
