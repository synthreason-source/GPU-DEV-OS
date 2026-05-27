/**
 * Font.h - 8x16 bitmap font renderer for bare-metal framebuffer
 * No OS, no FreeType, no TTF. Pure bitmapped ASCII glyph blitting.
 */
#ifndef FONT_H
#include "Graphics.h"

#define FONT_W  8   /* glyph width  in pixels */
#define FONT_H 16   /* glyph height in pixels */

VOID FontDrawChar(GPU_FB *Fb, UINT32 X, UINT32 Y,
                  CHAR8 Ch, UINT32 FgColor, UINT32 BgColor,
                  BOOLEAN Transparent);

VOID FontDrawString(GPU_FB *Fb, UINT32 X, UINT32 Y,
                    const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor);

VOID FontDrawStringScaled(GPU_FB *Fb, UINT32 X, UINT32 Y,
                          const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor,
                          UINT32 Scale);

#endif /* FONT_H */
