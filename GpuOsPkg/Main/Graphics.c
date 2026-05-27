/**
 * Graphics.c — Direct GPU framebuffer pixel operations
 * No OS, no GPU driver, no DRM. Pure VRAM writes via GOP pointer.
 */
#include "Graphics.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>

EFI_STATUS GfxInit(EFI_BOOT_SERVICES *BS, GPU_FB *Fb) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    EFI_STATUS Status = BS->LocateProtocol(&GopGuid, NULL, (VOID**)&Gop);
    if (EFI_ERROR(Status)) return Status;

    /* Find highest-resolution BGRA mode */
    UINT32 BestMode = Gop->Mode->Mode;
    UINT32 BestRes  = 0;
    for (UINT32 i = 0; i < Gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
        UINTN SzInfo;
        Status = Gop->QueryMode(Gop, i, &SzInfo, &Info);
        if (EFI_ERROR(Status)) continue;
        UINT32 Res = Info->HorizontalResolution * Info->VerticalResolution;
        if (Res > BestRes &&
            Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
            BestRes  = Res;
            BestMode = i;
        }
    }
    Gop->SetMode(Gop, BestMode);

    /* Capture live framebuffer pointer — valid after ExitBootServices */
    Fb->FrameBuffer       = (UINT32*)(UINTN)Gop->Mode->FrameBufferBase;
    Fb->Width             = Gop->Mode->Info->HorizontalResolution;
    Fb->Height            = Gop->Mode->Info->VerticalResolution;
    Fb->PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;
    return EFI_SUCCESS;
}

VOID GfxPutPixel(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 BGRA) {
    if (X < Fb->Width && Y < Fb->Height)
        Fb->FrameBuffer[Y * Fb->PixelsPerScanLine + X] = BGRA;
}

VOID GfxFillRect(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
    UINT32 EndY = (Y + H < Fb->Height) ? Y + H : Fb->Height;
    UINT32 EndX = (X + W < Fb->Width)  ? X + W : Fb->Width;
    for (UINT32 dy = Y; dy < EndY; dy++) {
        UINT32 *Row = &Fb->FrameBuffer[dy * Fb->PixelsPerScanLine + X];
        for (UINT32 dx = X; dx < EndX; dx++)
            *Row++ = Color;
    }
}

VOID GfxDrawRect(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
    GfxFillRect(Fb, X,       Y,       W, 1, Color); /* top    */
    GfxFillRect(Fb, X,       Y+H-1,   W, 1, Color); /* bottom */
    GfxFillRect(Fb, X,       Y,       1, H, Color); /* left   */
    GfxFillRect(Fb, X+W-1,   Y,       1, H, Color); /* right  */
}

VOID GfxClear(GPU_FB *Fb, UINT32 Color) {
    GfxFillRect(Fb, 0, 0, Fb->Width, Fb->Height, Color);
}

VOID GfxGradientBackground(GPU_FB *Fb, UINT32 ColorTop, UINT32 ColorBottom) {
    UINT8 Tr = (ColorTop  >> 16) & 0xFF;
    UINT8 Tg = (ColorTop  >>  8) & 0xFF;
    UINT8 Tb = (ColorTop  >>  0) & 0xFF;
    UINT8 Br = (ColorBottom>>16) & 0xFF;
    UINT8 Bg = (ColorBottom>> 8) & 0xFF;
    UINT8 Bb = (ColorBottom>> 0) & 0xFF;

    for (UINT32 Y = 0; Y < Fb->Height; Y++) {
        UINT32 t  = (Y * 255) / Fb->Height;
        UINT8  R  = (UINT8)(Tr + (((INT32)Br - Tr) * t) / 255);
        UINT8  G  = (UINT8)(Tg + (((INT32)Bg - Tg) * t) / 255);
        UINT8  B  = (UINT8)(Tb + (((INT32)Bb - Tb) * t) / 255);
        UINT32 C  = 0xFF000000 | (R<<16) | (G<<8) | B;
        for (UINT32 X = 0; X < Fb->Width; X++)
            Fb->FrameBuffer[Y * Fb->PixelsPerScanLine + X] = C;
    }
}
