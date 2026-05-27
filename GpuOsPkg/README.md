# GpuOS — Bare-Metal UEFI GPU Pipeline

> **Boot from USB → GPU takes over display → Windows never loads**

A complete bare-metal UEFI application that bypasses the OS entirely,
acquires the GPU framebuffer directly, and renders output straight to
HDMI/DisplayPort using pure VRAM writes — no drivers, no kernel, no OS.

---

## What This Does

```
UEFI Firmware
  └─ loads BOOTX64.EFI from USB
       ├─ GfxInit()      → grab GPU VRAM scanout address (GOP)
       ├─ FindAndMapGpu() → map AMD GPU BAR0 MMIO registers
       └─ ExitBootServices() ── OS permanently blocked
            ├─ Direct pixel writes → FrameBuffer → GPU display pipeline → screen
            └─ PM4 command packets → GPU shader cores (AMD compute)
```

**No Windows. No Linux. No kernel. No drivers. Just your code and the GPU.**

---

## Quick Start

### 1. First-Time Setup (Windows, run once as Admin)
```bat
GpuOsPkg\Scripts\setup.bat
```
Installs Python, LLVM/Clang, NASM, Git, clones EDK2, configures everything.

### 2. Build + Deploy to USB
```bat
GpuOsPkg\Scripts\build_and_deploy.bat
```
Compiles the UEFI application and copies it to your USB drive.

### 3. Test in QEMU First (Recommended)
```bat
:: Download OVMF.fd first (see below), then:
GpuOsPkg\Scripts\run_qemu.bat
```

### 4. Boot on Real Hardware
1. Insert USB
2. Reboot → enter BIOS (DEL/F2)
3. **Disable Secure Boot**
4. Set USB as first boot device
5. Save & Exit → GpuOS boots, Windows never starts

---

## File Structure

```
GpuOsPkg/
├── GpuOsPkg.dsc        EDK2 package descriptor
├── Main/
│   ├── Main.inf        EDK2 module descriptor
│   ├── Main.c          UEFI entry point, ExitBootServices, display rendering
│   ├── Graphics.c/h    Direct framebuffer pixel ops (no GPU driver needed)
│   ├── Font.c/h        8x16 bitmap font renderer (full ASCII, no FreeType)
│   ├── PciGpu.c/h      AMD GPU PCI detection, BAR0 MMIO mapping, PM4 helpers
│   └── OVMF.fd         ← Download and place here for QEMU testing
└── Scripts/
    ├── setup.bat        First-time toolchain installer
    ├── build_and_deploy.bat  Build EFI + deploy to USB
    └── run_qemu.bat     Test in QEMU before real hardware
```

---

## Requirements

| Component | Requirement |
|-----------|-------------|
| Build OS  | Windows 10/11 |
| Toolchain | EDK2 + LLVM/Clang (CLANGPDB) + NASM + Python 3 |
| GPU (display) | Any UEFI GOP-compatible GPU (NVIDIA/AMD/Intel all work) |
| GPU (compute) | AMD RDNA2/3 recommended (open MMIO register docs) |
| USB | FAT32 formatted, any size |
| OVMF | Download from https://retrage.github.io/edk2-nightly/ |

---

## How the Display Works

The GPU's VBIOS exposes a `EFI_GRAPHICS_OUTPUT_PROTOCOL` (GOP) during early
UEFI boot. This gives us the raw physical address of the GPU's VRAM scanout
buffer. We capture this pointer **before** `ExitBootServices()`.

After boot services exit, writing 32-bit BGRA pixels to `Fb.FrameBuffer[y * stride + x]`
appears directly on the connected HDMI/DisplayPort monitor — no GPU driver,
no compositor, no OS involved.

---

## AMD GPU Compute (PM4 Dispatch)

`PciGpu.c` detects your AMD GPU on the PCI bus and maps its BAR0 MMIO
register space before `ExitBootServices()`. After boot, write PM4 Type-3
command packets to the ring buffer and ring the doorbell register to dispatch
compute shaders to GPU shader cores.

Reference: `linux/drivers/gpu/drm/amd/amdgpu/amdgpu_ring.c`

---

## Extending GpuOS

| Feature | Where to Add |
|---------|-------------|
| Keyboard input | Grab `EFI_SIMPLE_TEXT_INPUT_PROTOCOL` before ExitBootServices |
| Double buffering | Allocate RAM back-buffer, render there, memcpy to FrameBuffer |
| GPU compute | Add PM4 ring writes in `PciGpu.c` after ExitBootServices |
| USB eGPU (Tiny Corp) | See https://github.com/tinygrad/tinygrad for USB3 AMD backend |
| Custom shell | Add event loop in `Main.c` after `DrawGpuOsScreen()` |

---

## References

- [EDK2 Documentation](https://github.com/tianocore/edk2)
- [UEFI Specification](https://uefi.org/specifications)
- [AMD GPU Register Reference (RDNA3)](https://gpuopen.com/amd-rdna3-isa/)
- [tinygrad USB GPU](https://github.com/tinygrad/tinygrad)
- [OSDev Wiki — UEFI](https://wiki.osdev.org/UEFI)
- [amdgpu ring source](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_ring.c)
