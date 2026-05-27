## GpuOsPkg.dsc — EDK2 Package Descriptor for GpuOS
## v2: VFS + Editor added

[Defines]
  PLATFORM_NAME           = GpuOsPkg
  PLATFORM_GUID           = 7a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d
  PLATFORM_VERSION        = 2.0
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/GpuOsPkg
  SUPPORTED_ARCHITECTURES = X64
  BUILD_TARGETS           = DEBUG|RELEASE

[LibraryClasses]
  ## Entry point
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf

  ## Core UEFI
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

  ## Base
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf

  ## Print / debug (needed by UefiLib)
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf

  ## Device path (needed by UefiLib)
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf

  ## Register filter (needed by BaseLib on some EDK2 versions)
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf

  ## PcdLib — consumed transitively by BaseLib and many other MdePkg libs
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf

  ## SafeIntLib — consumed transitively by BaseLib on recent EDK2
  SafeIntLib|MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf

  ## StackCheckLib — consumed by UefiApplicationEntryPoint; our null stub
  ## satisfies the class without pulling in MSVC runtime stack cookie support
  StackCheckLib|GpuOsPkg/Lib/StackCheckLibNull/StackCheckLibNull.inf

[Components]
  GpuOsPkg/Main/Main.inf

[BuildOptions]
  *_CLANGPDB_X64_CC_FLAGS  = -fno-stack-protector -Wno-unused-function
  *_GCC5_X64_CC_FLAGS      = -fno-stack-protector
  *_VS2019_X64_CC_FLAGS    = /GS-
