/**
 * Editor.h — Bare-metal modal text editor for GpuOS
 * Vi-inspired but beginner-friendly. Runs entirely in the framebuffer.
 * No OS, no terminal, no escape sequences — direct key event polling.
 *
 * Features:
 *   - Multi-line editing with scrolling
 *   - Line numbers
 *   - Cursor movement (arrow keys, Home, End, PgUp, PgDn)
 *   - Insert / Delete / Backspace
 *   - Ctrl+S to save to VFS, Ctrl+Q to quit
 *   - Ctrl+N new file, Ctrl+O open file (mini file picker)
 *   - Status bar with filename, line/col, modified indicator
 */
#ifndef EDITOR_H
#define EDITOR_H

#include "Graphics.h"
#include "Font.h"
#include "VFS.h"
#include <Protocol/SimpleTextIn.h>

/* ── Editor geometry (computed at runtime from Fb dimensions) ─────── */
#define EDITOR_LNUM_COLS    5   /* width in chars for line-number gutter */
#define EDITOR_STATUS_ROWS  2   /* rows reserved at bottom for status bar */
#define EDITOR_TAB_WIDTH    4   /* spaces per tab                         */

/* ── Buffer limits ───────────────────────────────────────────────── */
#define EDITOR_MAX_LINES    4096
#define EDITOR_MAX_LINE_LEN 512
#define EDITOR_FILENAME_MAX 64

/* ── A single line of text ────────────────────────────────────────── */
typedef struct {
    CHAR8  Buf[EDITOR_MAX_LINE_LEN];
    UINT32 Len;
} EDITOR_LINE;

/* ── Editor state ────────────────────────────────────────────────── */
typedef struct {
    /* Text buffer */
    EDITOR_LINE  Lines[EDITOR_MAX_LINES];
    UINT32       NumLines;

    /* Cursor position (0-based) */
    UINT32       CurRow;
    UINT32       CurCol;

    /* Viewport (top-left of visible area) */
    UINT32       ScrollRow;
    UINT32       ScrollCol;

    /* Filename & dirty flag */
    CHAR8        Filename[EDITOR_FILENAME_MAX];
    BOOLEAN      Modified;

    /* Framebuffer & input (set by EditorRun) */
    GPU_FB      *Fb;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *TextIn;

    /* Derived layout (pixels) */
    UINT32       VisRows;   /* number of text rows visible */
    UINT32       VisCols;   /* number of text columns visible */
    UINT32       TextX;     /* pixel X where text area starts (after gutter) */
    UINT32       TextY;     /* pixel Y where text area starts */

    /* Cursor blink toggle */
    UINT32       BlinkCounter;

    /* Status message (shown bottom-left for a few frames) */
    CHAR8        StatusMsg[128];
    UINT32       StatusMsgTimer;
} EDITOR;

/**
 * EditorInit — Initialise an editor instance with an empty buffer.
 * @param Ed     Editor state to initialise
 * @param Fb     Framebuffer to render into
 * @param TextIn Keyboard protocol (grabbed before ExitBootServices)
 * @param Filename  Optional filename to load from VFS (NULL = new file)
 */
VOID EditorInit(EDITOR *Ed, GPU_FB *Fb,
                EFI_SIMPLE_TEXT_INPUT_PROTOCOL *TextIn,
                const CHAR8 *Filename);

/**
 * EditorRun — Enter the editor event loop.
 * Returns when the user presses Ctrl+Q.
 */
VOID EditorRun(EDITOR *Ed);

/**
 * EditorLoadFromVfs — Load a VFS file into the editor buffer.
 */
VOID EditorLoadFromVfs(EDITOR *Ed, VFS_FILE *File);

/**
 * EditorSaveToVfs — Save the editor buffer to VFS.
 */
EFI_STATUS EditorSaveToVfs(EDITOR *Ed);

#endif /* EDITOR_H */
