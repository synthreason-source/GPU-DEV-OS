/**
 * Font.h ? 8x16 bitmap font renderer for bare-metal framebuffer
 * No OS, no FreeType, no TTF. Pure bitmapped ASCII glyph blitting.
 */
#ifndef FONT_H
#include "Graphics.h"

#define FONT_W  8   /* glyph width  in pixels */
#define FONT_H 16   /* glyph height in pixels */

/**
 * Draw a single ASCII character at pixel position (X,Y).
 * @param Fb       Framebuffer
 * @param X,Y      Top-left pixel of glyph
 * @param Ch       ASCII character (0-127)
 * @param FgColor  Foreground BGRA
 * @param BgColor  Background BGRA (set to 0 for transparent ? skips bg pixels)
 * @param Transparent  If TRUE, skip background pixels (overlay mode)
 */
VOID FontDrawChar(GPU_FB *Fb, UINT32 X, UINT32 Y,
                  CHAR8 Ch, UINT32 FgColor, UINT32 BgColor,
                  BOOLEAN Transparent);

/**
 * Draw a NUL-terminated ASCII string at (X,Y).
 * Newline (\n) handled: X resets to StartX, Y advances by FONT_H.
 */
VOID FontDrawString(GPU_FB *Fb, UINT32 X, UINT32 Y,
                    const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor);

/**
 * Draw string with scale factor (integer upscale, e.g. Scale=2 = 16x32px glyphs).
 */
VOID FontDrawStringScaled(GPU_FB *Fb, UINT32 X, UINT32 Y,
                          const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor,
                          UINT32 Scale);

#endif /* FONT_H */
