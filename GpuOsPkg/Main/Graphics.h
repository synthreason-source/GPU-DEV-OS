/**
 * Graphics.h — Bare-metal GPU framebuffer interface
 * Direct VRAM pixel writes, no OS, no DRM/KMS, no X11
 * v2: double-buffer support to eliminate flicker
 */
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

/* Framebuffer descriptor — survives ExitBootServices */
typedef struct {
    UINT32  *FrameBuffer;       /* Direct pointer into GPU VRAM scanout (front) */
    UINT32  *BackBuffer;        /* RAM back-buffer for flicker-free rendering    */
    UINT32   Width;
    UINT32   Height;
    UINT32   PixelsPerScanLine; /* May be larger than Width (stride)             */
    UINTN    BackBufferSize;    /* Byte size of back-buffer allocation           */
} GPU_FB;

/* Color macros — BGRA format (what GOP/UEFI uses) */
#define COLOR_BGRA(b,g,r)   ((UINT32)(((b)&0xFF) | (((g)&0xFF)<<8) | (((r)&0xFF)<<16) | 0xFF000000))
#define COLOR_BLACK         0xFF000000
#define COLOR_WHITE         0xFFFFFFFF
#define COLOR_RED           COLOR_BGRA(0,0,255)
#define COLOR_GREEN         COLOR_BGRA(0,255,0)
#define COLOR_BLUE          COLOR_BGRA(255,0,0)
#define COLOR_CYAN          COLOR_BGRA(255,255,0)
#define COLOR_MAGENTA       COLOR_BGRA(255,0,255)
#define COLOR_YELLOW        COLOR_BGRA(0,255,255)
#define COLOR_DARK_BG       COLOR_BGRA(10,10,18)
#define COLOR_PANEL         COLOR_BGRA(20,26,46)
#define COLOR_TITLE_BAR     COLOR_BGRA(16,33,62)
#define COLOR_ACCENT        COLOR_BGRA(0,255,136)
#define COLOR_DIM           COLOR_BGRA(120,120,120)
#define COLOR_EDITOR_BG     COLOR_BGRA(18,18,28)
#define COLOR_EDITOR_LINE   COLOR_BGRA(30,30,48)
#define COLOR_CURSOR        COLOR_BGRA(0,200,255)
#define COLOR_SELECT        COLOR_BGRA(50,80,120)

/**
 * GfxInit — Locate GOP protocol, select best resolution, capture framebuffer.
 * Allocates a RAM back-buffer for flicker-free double-buffered rendering.
 * Must be called BEFORE ExitBootServices.
 * @param BS   Boot services table pointer
 * @param Fb   Output: populated GPU_FB descriptor
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS GfxInit(EFI_BOOT_SERVICES *BS, GPU_FB *Fb);

/**
 * GfxFlip — Blit the back-buffer to the real framebuffer in one shot.
 * Call once per frame after all drawing is done — eliminates tearing/flicker.
 */
VOID GfxFlip(GPU_FB *Fb);

/** Write a single pixel to the BACK BUFFER at (X,Y). Bounds-checked. */
VOID GfxPutPixel(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 BGRA);

/** Fill a filled rectangle in the back-buffer. Clips to framebuffer bounds. */
VOID GfxFillRect(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);

/** Draw a hollow rectangle border (1px) in the back-buffer. */
VOID GfxDrawRect(GPU_FB *Fb, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);

/** Clear entire back-buffer to Color. */
VOID GfxClear(GPU_FB *Fb, UINT32 Color);

/** Draw a horizontal gradient across the full back-buffer. */
VOID GfxGradientBackground(GPU_FB *Fb, UINT32 ColorTop, UINT32 ColorBottom);

#endif /* GRAPHICS_H */
