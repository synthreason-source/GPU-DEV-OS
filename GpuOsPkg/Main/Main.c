/**
 * Main.c ? GpuOS bare-metal UEFI entry point
 *
 * Boot sequence:
 *   1. Grab GOP framebuffer base (GPU VRAM scanout address)
 *   2. Detect and map AMD GPU BAR0 (MMIO compute registers)
 *   3. ExitBootServices() ? OS never loads
 *   4. Direct framebuffer writes ? GPU display pipeline ? HDMI/DP output
 *   5. (Optional) PM4 dispatch ? GPU shader cores
 *   6. Halt loop
 *
 * Build: EDK2 + CLANGPDB on Windows, outputs GpuOs.efi
 * Deploy: Copy to USB:/EFI/BOOT/BOOTX64.EFI and boot from USB
 */

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include "Graphics.h"
#include "Font.h"
#include "PciGpu.h"

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
STATIC VOID DrawGpuOsScreen(GPU_FB *Fb, GPU_INFO *GpuInfo);
//STATIC VOID DrawProgressBar(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                             //UINT32 Percent, UINT32 FgColor, UINT32 BgColor);
STATIC VOID DrawHexU32(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 Value,
                        UINT32 FgColor, UINT32 BgColor);

/* ------------------------------------------------------------------ */
/* Hex formatting (no libc, no snprintf)                               */
/* ------------------------------------------------------------------ */
STATIC VOID U32ToHexStr(UINT32 Value, CHAR8 *Buf) {
    /* Buf must be at least 11 bytes: "0x" + 8 hex digits + NUL */
    const CHAR8 HexChars[] = "0123456789ABCDEF";
    Buf[0] = '0'; Buf[1] = 'x';
    for (INT32 i = 7; i >= 0; i--) {
        Buf[2 + (7 - i)] = HexChars[(Value >> (i * 4)) & 0xF];
    }
    Buf[10] = '\0';
}

STATIC VOID U16ToDecStr(UINT16 Value, CHAR8 *Buf) {
    /* Buf must be at least 6 bytes */
    UINT16 V = Value;
    Buf[0] = '0' + (V / 10000) % 10;
    Buf[1] = '0' + (V /  1000) % 10;
    Buf[2] = '0' + (V /   100) % 10;
    Buf[3] = '0' + (V /    10) % 10;
    Buf[4] = '0' + (V        ) % 10;
    Buf[5] = '\0';
    /* Trim leading zeros */
    CHAR8 *p = Buf;
    while (*p == '0' && *(p+1)) p++;
    if (p != Buf) {
        CHAR8 *d = Buf;
        while (*p) *d++ = *p++;
        *d = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* GPU OS Shell ? renders entirely via direct framebuffer writes       */
/* ------------------------------------------------------------------ */
/*
STATIC VOID DrawProgressBar(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                              UINT32 Percent, UINT32 FgColor, UINT32 BgColor) {
    GfxFillRect(Fb, X, Y, W, H, BgColor);
    GfxDrawRect(Fb, X, Y, W, H, FgColor);
    UINT32 Fill = (W * Percent) / 100;
    if (Fill > 2) GfxFillRect(Fb, X+1, Y+1, Fill-2, H-2, FgColor);
}
*/
STATIC VOID DrawHexU32(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 Value,
                         UINT32 FgColor, UINT32 BgColor) {
    CHAR8 Buf[12];
    U32ToHexStr(Value, Buf);
    FontDrawString(Fb, X, Y, Buf, FgColor, BgColor);
}

STATIC VOID DrawGpuOsScreen(GPU_FB *Fb, GPU_INFO *GpuInfo) {
    /* ?? Background: deep navy gradient ??????????????????????????? */
    GfxGradientBackground(Fb, 0xFF0A0A12, 0xFF0D1B2A);

    UINT32 CX = Fb->Width / 2;

    (VOID)CX;

    /* ?? Title bar ???????????????????????????????????????????????? */
    GfxFillRect(Fb, 0, 0, Fb->Width, 36, 0xFF0A0E1A);
    GfxFillRect(Fb, 0, 36, Fb->Width, 1, 0xFF00FF88);  /* accent line */
    FontDrawStringScaled(Fb, 16, 8, "GpuOS", 0xFF00FF88, 0xFF0A0E1A, 2);
    FontDrawString(Fb, 16+5*2*8+8, 16, "bare-metal UEFI GPU pipeline", 0xFF4488AA, 0xFF0A0E1A);
    FontDrawString(Fb, Fb->Width - 9*8, 14, "v0.1.0", 0xFF335566, 0xFF0A0E1A);

    /* ?? Main panel ??????????????????????????????????????????????? */
    UINT32 PX = 60, PY = 56, PW = Fb->Width - 120, PH = Fb->Height - 120;
    GfxFillRect(Fb, PX, PY, PW, PH, 0xE0080C18);
    GfxDrawRect (Fb, PX, PY, PW, PH, 0xFF1A3050);

    /* ?? Status header ???????????????????????????????????????????? */
    GfxFillRect(Fb, PX, PY, PW, 26, 0xFF0E1828);
    FontDrawString(Fb, PX+12, PY+6, "SYSTEM STATUS", 0xFF00CCFF, 0xFF0E1828);
    FontDrawString(Fb, PX+PW-11*8, PY+6, "NO OS LOADED", 0xFF00FF88, 0xFF0E1828);

    UINT32 TY = PY + 40;
    UINT32 TX = PX + 20;
    UINT32 COL2 = TX + 28*8;

    /* ?? Boot info ???????????????????????????????????????????????? */
    FontDrawString(Fb, TX, TY, "Boot mode:", 0xFF6688AA, 0x00000000);
    FontDrawString(Fb, COL2, TY, "UEFI bare-metal (ExitBootServices)", 0xFF00FF88, 0x00000000);
    TY += 20;

    FontDrawString(Fb, TX, TY, "Windows status:", 0xFF6688AA, 0x00000000);
    FontDrawString(Fb, COL2, TY, "NEVER LOADED", 0xFFFF4444, 0x00000000);
    TY += 20;

    FontDrawString(Fb, TX, TY, "Display source:", 0xFF6688AA, 0x00000000);
    FontDrawString(Fb, COL2, TY, "Direct VRAM framebuffer write (GOP)", 0xFF00FF88, 0x00000000);
    TY += 20;

    /* ?? Resolution ??????????????????????????????????????????????? */
    CHAR8 Res[24] = "                       ";
    CHAR8 NumBuf[8];
    U16ToDecStr(Fb->Width,  NumBuf); 
    /* manual string build: WxH */
    UINT32 ri = 0;
    for (CHAR8 *p = NumBuf; *p; p++) Res[ri++] = *p;
    Res[ri++] = 'x';
    U16ToDecStr(Fb->Height, NumBuf);
    for (CHAR8 *p = NumBuf; *p; p++) Res[ri++] = *p;
    Res[ri] = '\0';

    FontDrawString(Fb, TX, TY, "Resolution:", 0xFF6688AA, 0x00000000);
    FontDrawString(Fb, COL2, TY, Res, 0xFFDDEEFF, 0x00000000);
    TY += 20;

    /* ?? Framebuffer address ??????????????????????????????????????? */
    FontDrawString(Fb, TX, TY, "FB base address:", 0xFF6688AA, 0x00000000);
    DrawHexU32(Fb, COL2, TY, (UINT32)(UINTN)Fb->FrameBuffer, 0xFFFFCC00, 0x00000000);
    TY += 30;

    /* ?? Divider ?????????????????????????????????????????????????? */
    GfxFillRect(Fb, TX, TY, PW-40, 1, 0xFF1A3050);
    TY += 12;

    /* ?? GPU Info section ??????????????????????????????????????????? */
    FontDrawString(Fb, TX, TY, "GPU HARDWARE", 0xFF00CCFF, 0x00000000);
    TY += 20;

    if (GpuInfo && GpuInfo->VendorId == 0x1002) {
        FontDrawString(Fb, TX, TY, "Vendor:", 0xFF6688AA, 0x00000000);
        FontDrawString(Fb, COL2, TY,
            GpuInfo->IsKnownRdna ? "AMD (RDNA2/3 - Verified)" : "AMD (Unknown revision)",
            0xFF00FF88, 0x00000000);
        TY += 20;

        FontDrawString(Fb, TX, TY, "Device ID:", 0xFF6688AA, 0x00000000);
        DrawHexU32(Fb, COL2, TY, (UINT32)GpuInfo->DeviceId, 0xFFFFCC00, 0x00000000);
        TY += 20;

        FontDrawString(Fb, TX, TY, "BAR0 (MMIO):", 0xFF6688AA, 0x00000000);
        DrawHexU32(Fb, COL2, TY, (UINT32)(GpuInfo->Bar0Base >> 32), 0xFFFF8844, 0x00000000);
        FontDrawString(Fb, COL2 + 10*8, TY, ":", 0xFF6688AA, 0x00000000);
        DrawHexU32(Fb, COL2 + 11*8, TY, (UINT32)GpuInfo->Bar0Base, 0xFFFF8844, 0x00000000);
        TY += 20;

        FontDrawString(Fb, TX, TY, "Compute dispatch:", 0xFF6688AA, 0x00000000);
        FontDrawString(Fb, COL2, TY, "PM4 ring ready (see PciGpu.h)", 0xFF00FF88, 0x00000000);
        TY += 20;
    } else {
        FontDrawString(Fb, TX, TY, "AMD GPU:", 0xFF6688AA, 0x00000000);
        FontDrawString(Fb, COL2, TY,
            GpuInfo ? "NOT FOUND (non-AMD or detection failed)" : "GPU scan skipped",
            0xFFFF6644, 0x00000000);
        TY += 20;
        FontDrawString(Fb, TX, TY, "Tip:", 0xFF6688AA, 0x00000000);
        FontDrawString(Fb, COL2, TY, "Display still works via GOP framebuffer", 0xFF888888, 0x00000000);
        TY += 20;
    }
    TY += 10;

    /* ?? Divider ?????????????????????????????????????????????????? */
    GfxFillRect(Fb, TX, TY, PW-40, 1, 0xFF1A3050);
    TY += 12;

    /* ?? Kernel log simulation ????????????????????????????????????? */
    FontDrawString(Fb, TX, TY, "KERNEL LOG", 0xFF00CCFF, 0x00000000);
    TY += 20;

    struct { const CHAR8 *Prefix; const CHAR8 *Msg; UINT32 PfxColor; } Log[] = {
        { "[  OK  ] ", "UEFI GOP protocol acquired",             0xFF00FF88 },
        { "[  OK  ] ", "Framebuffer mapped, resolution detected", 0xFF00FF88 },
        { "[  OK  ] ", "ExitBootServices() ? host OS terminated", 0xFF00FF88 },
        { "[  OK  ] ", "PCI bus scanned for AMD GPU",            0xFF00FF88 },
        { "[  OK  ] ", "GPU BAR0 MMIO mapped",                   0xFF00FF88 },
        { "[ INFO ] ", "PM4 command ring: awaiting dispatch",    0xFF88AAFF },
        { "[ INFO ] ", "No OS loaded. No kernel. No drivers.",   0xFF88AAFF },
        { "[ HALT ] ", "System running bare-metal. Enjoy.",      0xFFFFCC00 },
    };
    for (UINT32 i = 0; i < sizeof(Log)/sizeof(Log[0]) && TY + 18 < PY+PH-20; i++) {
        FontDrawString(Fb, TX, TY, Log[i].Prefix, Log[i].PfxColor, 0x00000000);
        FontDrawString(Fb, TX + 10*8, TY, Log[i].Msg, 0xFF99AABB, 0x00000000);
        TY += 18;
    }

    /* ?? Bottom footer bar ???????????????????????????????????????? */
    UINT32 FY = Fb->Height - 28;
    GfxFillRect(Fb, 0, FY, Fb->Width, 28, 0xFF060912);
    GfxFillRect(Fb, 0, FY, Fb->Width, 1,  0xFF1A3050);
    FontDrawString(Fb, 16, FY+8,
        "GpuOS  |  Bare-metal UEFI GPU pipeline  |  github.com/tinygrad/tinygrad",
        0xFF334455, 0xFF060912);
    FontDrawString(Fb, Fb->Width - 22*8, FY+8,
        "No OS. GPU owns display.",
        0xFF00FF88, 0xFF060912);
}

/* ------------------------------------------------------------------ */
/* UEFI Entry Point                                                    */
/* ------------------------------------------------------------------ */
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle,
                            EFI_SYSTEM_TABLE *SystemTable) {
    GPU_FB   Fb;
    GPU_INFO GpuInfo;
    BOOLEAN  GpuFound = FALSE;

    SetMem(&Fb,      sizeof(Fb),      0);
    SetMem(&GpuInfo, sizeof(GpuInfo), 0);

    /* ?? A: Initialize framebuffer (must be before ExitBootServices) */
    EFI_STATUS Status = GfxInit(gBS, &Fb);
    if (EFI_ERROR(Status)) {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L"FATAL: EFI GOP not found. No GPU framebuffer available.\r\n");
        return Status;
    }

    /* ?? B: Detect and map AMD GPU BAR0 for compute dispatch ???????? */
    Status = FindAndMapGpu(gBS, &GpuInfo);
    if (!EFI_ERROR(Status))
        GpuFound = TRUE;

    /* ?? C: Get memory map key (required for ExitBootServices) ??????? */
    UINTN MapSize = 0, MapKey = 0, DescSize = 0;
    UINT32 DescVer = 0;
    EFI_MEMORY_DESCRIPTOR *MemMap = NULL;

    gBS->GetMemoryMap(&MapSize, MemMap, &MapKey, &DescSize, &DescVer);
    MapSize += 2 * DescSize;
    gBS->AllocatePool(EfiLoaderData, MapSize, (VOID**)&MemMap);
    gBS->GetMemoryMap(&MapSize, MemMap, &MapKey, &DescSize, &DescVer);

    /* ?? D: EXIT BOOT SERVICES ? point of no return ??????????????? */
    /*       Windows / any OS is now permanently blocked from loading */
    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if (EFI_ERROR(Status)) {
        /* MapKey went stale ? retry (UEFI spec mandates this path) */
        gBS->GetMemoryMap(&MapSize, MemMap, &MapKey, &DescSize, &DescVer);
        gBS->ExitBootServices(ImageHandle, MapKey);
    }

    /* ?????????????????????????????????????????????????????????????? */
    /* FROM THIS POINT: NO OS. NO UEFI. NO DRIVERS.                 */
    /* Fb.FrameBuffer is a live pointer to GPU VRAM scanout region.  */
    /* Writing pixels here produces direct HDMI/DP output.           */
    /* GGpuMmio points to AMD GPU MMIO register space.               */
    /* ?????????????????????????????????????????????????????????????? */

    DrawGpuOsScreen(&Fb, GpuFound ? &GpuInfo : NULL);

    /* ?? E: Halt forever (your OS / event loop goes here) ?????????? */
    while (1) {
        __asm__ volatile ("hlt");
    }

    return EFI_SUCCESS; /* unreachable */
}
