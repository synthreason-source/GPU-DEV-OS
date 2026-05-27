/**
 * Editor.c ? Bare-metal text editor for GpuOS
 * Renders into the double-buffered framebuffer, polls keyboard via
 * EFI_SIMPLE_TEXT_INPUT_PROTOCOL.
 *
 * Key bindings:
 *   Arrow keys       Move cursor
 *   Home / End       Start / end of line
 *   PgUp / PgDn      Scroll by one viewport
 *   Backspace        Delete before cursor
 *   Delete           Delete at cursor
 *   Enter            Split line
 *   Ctrl+S           Save to VFS
 *   Ctrl+Q           Quit editor
 *   Ctrl+N           New file (prompts for name)
 *   Ctrl+K           Delete (kill) current line
 *   Ctrl+A           Move to line start
 *   Ctrl+E           Move to line end
 */
#include "Editor.h"
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

/* ?? SCAN CODES from EFI_SIMPLE_TEXT_INPUT ??????????????????????? */
#define SCAN_PGUP   0x09
#define SCAN_PGDN   0x0A
/* ?? Control characters ??????????????????????????????????????????? */
#define CTRL_A  0x01
#define CTRL_E  0x05
#define CTRL_K  0x0B
#define CTRL_N  0x0E
#define CTRL_Q  0x11
#define CTRL_S  0x13
#define CHAR_BS 0x08
#define CHAR_CR 0x0D
#define CHAR_LF 0x0A

/* ?? Colors ??????????????????????????????????????????????????????? */
#define C_BG        COLOR_EDITOR_BG
#define C_GUTTER    COLOR_PANEL
#define C_GUTTER_FG COLOR_DIM
#define C_TEXT      COLOR_WHITE
#define C_CURSOR_BG COLOR_CURSOR
#define C_CURSOR_FG COLOR_BLACK
#define C_STATUS_BG COLOR_TITLE_BAR
#define C_STATUS_FG COLOR_ACCENT
#define C_MODIFIED  COLOR_RED
#define C_MSG       COLOR_YELLOW

/* ?? Internal helpers ?????????????????????????????????????????????? */

STATIC VOID EStrCopy(CHAR8 *Dst, const CHAR8 *Src, UINT32 MaxLen) {
    UINT32 i = 0;
    while (i + 1 < MaxLen && Src[i]) { Dst[i] = Src[i]; i++; }
    Dst[i] = '\0';
}

STATIC UINT32 EStrLen(const CHAR8 *S) {
    UINT32 n = 0;
    while (S[n]) n++;
    return n;
}

STATIC VOID EStrCat(CHAR8 *Dst, const CHAR8 *Src, UINT32 MaxLen) {
    UINT32 DstLen = EStrLen(Dst);
    EStrCopy(Dst + DstLen, Src, MaxLen - DstLen);
}

/* Integer to decimal ASCII, returns pointer past last digit */
STATIC CHAR8 *U32ToStr(UINT32 V, CHAR8 *Buf, UINT32 BufSz) {
    if (BufSz < 2) { Buf[0]='\0'; return Buf; }
    CHAR8 Tmp[12];
    INT32 i = 0;
    if (V == 0) { Tmp[i++] = '0'; }
    while (V > 0) { Tmp[i++] = '0' + (V % 10); V /= 10; }
    /* reverse */
    UINT32 out = 0;
    while (i > 0 && out + 1 < BufSz)
        Buf[out++] = Tmp[--i];
    Buf[out] = '\0';
    return Buf + out;
}

/* ?? Layout calculation ???????????????????????????????????????????? */

STATIC VOID EditorCalcLayout(EDITOR *Ed) {
    /* Gutter: EDITOR_LNUM_COLS characters wide + 1 separator column */
    UINT32 GutterPx = (EDITOR_LNUM_COLS + 1) * FONT_W;
    UINT32 StatusPx = EDITOR_STATUS_ROWS * FONT_H;

    Ed->TextX    = GutterPx;
    Ed->TextY    = 0;
    Ed->VisRows  = (Ed->Fb->Height - StatusPx) / FONT_H;
    Ed->VisCols  = (Ed->Fb->Width  - GutterPx) / FONT_W;
    if (Ed->VisRows < 1) Ed->VisRows = 1;
    if (Ed->VisCols < 1) Ed->VisCols = 1;
}

/* ?? Cursor clamping ??????????????????????????????????????????????? */

STATIC VOID EditorClampCursor(EDITOR *Ed) {
    if (Ed->CurRow >= Ed->NumLines)
        Ed->CurRow = Ed->NumLines > 0 ? Ed->NumLines - 1 : 0;
    UINT32 LineLen = Ed->Lines[Ed->CurRow].Len;
    if (Ed->CurCol > LineLen) Ed->CurCol = LineLen;
}

STATIC VOID EditorScrollToCursor(EDITOR *Ed) {
    if (Ed->CurRow < Ed->ScrollRow)
        Ed->ScrollRow = Ed->CurRow;
    if (Ed->CurRow >= Ed->ScrollRow + Ed->VisRows)
        Ed->ScrollRow = Ed->CurRow - Ed->VisRows + 1;
    if (Ed->CurCol < Ed->ScrollCol)
        Ed->ScrollCol = Ed->CurCol;
    if (Ed->CurCol >= Ed->ScrollCol + Ed->VisCols)
        Ed->ScrollCol = Ed->CurCol - Ed->VisCols + 1;
}

/* ?? Rendering ????????????????????????????????????????????????????? */

STATIC VOID EditorDrawGutter(EDITOR *Ed, UINT32 Row, UINT32 PyY) {
    UINT32 LineNum = Row + 1;
    CHAR8 Buf[8];
    U32ToStr(LineNum, Buf, sizeof(Buf));
    /* Right-align in EDITOR_LNUM_COLS columns */
    UINT32 Len  = EStrLen(Buf);
    UINT32 PadX = (EDITOR_LNUM_COLS > Len) ? (EDITOR_LNUM_COLS - Len) : 0;

    GfxFillRect(Ed->Fb, 0, PyY, Ed->TextX - FONT_W, FONT_H, C_GUTTER);
    FontDrawString(Ed->Fb, PadX * FONT_W, PyY, Buf, C_GUTTER_FG, C_GUTTER);
    /* Separator */
    GfxFillRect(Ed->Fb, Ed->TextX - FONT_W, PyY, FONT_W, FONT_H, C_GUTTER);
}

STATIC VOID EditorDrawLine(EDITOR *Ed, UINT32 Row, UINT32 PyY) {
    /* Background for text area */
    UINT32 RowColor = (Row % 2 == 0) ? C_BG : COLOR_EDITOR_LINE;
    GfxFillRect(Ed->Fb, Ed->TextX, PyY, Ed->Fb->Width - Ed->TextX, FONT_H, RowColor);

    if (Row >= Ed->NumLines) return;

    EDITOR_LINE *L = &Ed->Lines[Row];
    if (L->Len == 0) return;

    /* Draw visible columns */
    UINT32 ColStart = Ed->ScrollCol;
    UINT32 ColEnd   = ColStart + Ed->VisCols;
    if (ColEnd > L->Len) ColEnd = L->Len;

    for (UINT32 Col = ColStart; Col < ColEnd; Col++) {
        UINT32 Px = Ed->TextX + (Col - ColStart) * FONT_W;
        BOOLEAN IsCursor = (Row == Ed->CurRow && Col == Ed->CurCol);
        UINT32 Bg = IsCursor ? C_CURSOR_BG : RowColor;
        UINT32 Fg = IsCursor ? C_CURSOR_FG : C_TEXT;
        CHAR8  Ch = L->Buf[Col];
        if (Ch < 0x20 || Ch > 0x7E) Ch = '.';
        FontDrawChar(Ed->Fb, Px, PyY, Ch, Fg, Bg, FALSE);
    }

    /* Draw cursor at end of line if applicable */
    if (Ed->CurRow == Row && Ed->CurCol == L->Len &&
        Ed->CurCol >= ColStart && Ed->CurCol < ColStart + Ed->VisCols) {
        UINT32 Px = Ed->TextX + (Ed->CurCol - ColStart) * FONT_W;
        GfxFillRect(Ed->Fb, Px, PyY, FONT_W / 2, FONT_H, C_CURSOR_BG);
    }
}

STATIC VOID EditorDrawStatusBar(EDITOR *Ed) {
    UINT32 StatusY = Ed->Fb->Height - EDITOR_STATUS_ROWS * FONT_H;
    GfxFillRect(Ed->Fb, 0, StatusY, Ed->Fb->Width, EDITOR_STATUS_ROWS * FONT_H, C_STATUS_BG);

    /* Top status row: filename + modified + position */
    CHAR8 Status[256];
    Status[0] = '\0';
    EStrCat(Status, " ", sizeof(Status));
    EStrCat(Status, Ed->Filename[0] ? Ed->Filename : "[No Name]", sizeof(Status));
    if (Ed->Modified) EStrCat(Status, " [+]", sizeof(Status));

    /* Right-side: Ln/Col */
    CHAR8 Pos[32];
    CHAR8 TmpBuf[12];
    Pos[0] = '\0';
    EStrCat(Pos, "Ln ", sizeof(Pos));
    U32ToStr(Ed->CurRow + 1, TmpBuf, sizeof(TmpBuf));
    EStrCat(Pos, TmpBuf, sizeof(Pos));
    EStrCat(Pos, " Col ", sizeof(Pos));
    U32ToStr(Ed->CurCol + 1, TmpBuf, sizeof(TmpBuf));
    EStrCat(Pos, TmpBuf, sizeof(Pos));
    EStrCat(Pos, " ", sizeof(Pos));

    UINT32 PosLen = EStrLen(Pos);
    UINT32 PosX   = Ed->Fb->Width > PosLen * FONT_W
                  ? Ed->Fb->Width - PosLen * FONT_W : 0;

    FontDrawString(Ed->Fb, 0, StatusY, Status, C_STATUS_FG, C_STATUS_BG);
    FontDrawString(Ed->Fb, PosX, StatusY, Pos, C_STATUS_FG, C_STATUS_BG);

    /* Bottom status row: key hints or message */
    UINT32 HintY = StatusY + FONT_H;
    GfxFillRect(Ed->Fb, 0, HintY, Ed->Fb->Width, FONT_H, C_STATUS_BG);

    if (Ed->StatusMsgTimer > 0) {
        FontDrawString(Ed->Fb, 4, HintY, Ed->StatusMsg, C_MSG, C_STATUS_BG);
        Ed->StatusMsgTimer--;
    } else {
        FontDrawString(Ed->Fb, 4, HintY,
            "^S Save  ^Q Quit  ^K Kill Line  ^N New  Arrows Move",
            C_GUTTER_FG, C_STATUS_BG);
    }
}

STATIC VOID EditorDraw(EDITOR *Ed) {
    EditorCalcLayout(Ed);
    EditorScrollToCursor(Ed);

    for (UINT32 vr = 0; vr < Ed->VisRows; vr++) {
        UINT32 Row = Ed->ScrollRow + vr;
        UINT32 PyY = Ed->TextY + vr * FONT_H;
        EditorDrawGutter(Ed, Row, PyY);
        EditorDrawLine  (Ed, Row, PyY);
    }

    EditorDrawStatusBar(Ed);
    GfxFlip(Ed->Fb);
}

/* ?? Text manipulation ????????????????????????????????????????????? */

STATIC VOID EditorInsertChar(EDITOR *Ed, CHAR8 Ch) {
    if (Ed->CurRow >= EDITOR_MAX_LINES) return;
    EDITOR_LINE *L = &Ed->Lines[Ed->CurRow];
    if (L->Len >= EDITOR_MAX_LINE_LEN - 1) return;

    /* Shift right */
    for (UINT32 i = L->Len; i > Ed->CurCol; i--)
        L->Buf[i] = L->Buf[i-1];
    L->Buf[Ed->CurCol] = Ch;
    L->Buf[++L->Len]   = '\0';
    Ed->CurCol++;
    Ed->Modified = TRUE;
}

STATIC VOID EditorInsertNewline(EDITOR *Ed) {
    if (Ed->NumLines >= EDITOR_MAX_LINES) return;
    EDITOR_LINE *Cur = &Ed->Lines[Ed->CurRow];

    /* Shift lines down */
    for (UINT32 i = Ed->NumLines; i > Ed->CurRow + 1; i--)
        CopyMem(&Ed->Lines[i], &Ed->Lines[i-1], sizeof(EDITOR_LINE));
    Ed->NumLines++;

    EDITOR_LINE *Next = &Ed->Lines[Ed->CurRow + 1];

    /* Move tail of current line to next line */
    UINT32 TailLen = Cur->Len - Ed->CurCol;
    CopyMem(Next->Buf, Cur->Buf + Ed->CurCol, TailLen);
    Next->Buf[TailLen] = '\0';
    Next->Len          = TailLen;

    /* Truncate current line */
    Cur->Len       = Ed->CurCol;
    Cur->Buf[Cur->Len] = '\0';

    Ed->CurRow++;
    Ed->CurCol  = 0;
    Ed->Modified = TRUE;
}

STATIC VOID EditorBackspace(EDITOR *Ed) {
    if (Ed->CurCol > 0) {
        EDITOR_LINE *L = &Ed->Lines[Ed->CurRow];
        for (UINT32 i = Ed->CurCol - 1; i < L->Len - 1; i++)
            L->Buf[i] = L->Buf[i+1];
        L->Buf[--L->Len] = '\0';
        Ed->CurCol--;
        Ed->Modified = TRUE;
    } else if (Ed->CurRow > 0) {
        /* Merge with previous line */
        EDITOR_LINE *Prev = &Ed->Lines[Ed->CurRow - 1];
        EDITOR_LINE *Cur  = &Ed->Lines[Ed->CurRow];
        UINT32 PrevLen = Prev->Len;
        if (PrevLen + Cur->Len < EDITOR_MAX_LINE_LEN - 1) {
            CopyMem(Prev->Buf + PrevLen, Cur->Buf, Cur->Len);
            Prev->Len              = PrevLen + Cur->Len;
            Prev->Buf[Prev->Len]   = '\0';

            /* Shift lines up */
            for (UINT32 i = Ed->CurRow; i < Ed->NumLines - 1; i++)
                CopyMem(&Ed->Lines[i], &Ed->Lines[i+1], sizeof(EDITOR_LINE));
            Ed->NumLines--;
            Ed->CurRow--;
            Ed->CurCol  = PrevLen;
            Ed->Modified = TRUE;
        }
    }
}

STATIC VOID EditorDeleteChar(EDITOR *Ed) {
    EDITOR_LINE *L = &Ed->Lines[Ed->CurRow];
    if (Ed->CurCol < L->Len) {
        for (UINT32 i = Ed->CurCol; i < L->Len - 1; i++)
            L->Buf[i] = L->Buf[i+1];
        L->Buf[--L->Len] = '\0';
        Ed->Modified = TRUE;
    } else if (Ed->CurRow + 1 < Ed->NumLines) {
        /* Merge next line */
        EDITOR_LINE *Next = &Ed->Lines[Ed->CurRow + 1];
        if (L->Len + Next->Len < EDITOR_MAX_LINE_LEN - 1) {
            CopyMem(L->Buf + L->Len, Next->Buf, Next->Len);
            L->Len            += Next->Len;
            L->Buf[L->Len]     = '\0';
            for (UINT32 i = Ed->CurRow + 1; i < Ed->NumLines - 1; i++)
                CopyMem(&Ed->Lines[i], &Ed->Lines[i+1], sizeof(EDITOR_LINE));
            Ed->NumLines--;
            Ed->Modified = TRUE;
        }
    }
}

STATIC VOID EditorKillLine(EDITOR *Ed) {
    EDITOR_LINE *L = &Ed->Lines[Ed->CurRow];
    if (Ed->CurCol < L->Len) {
        /* Kill from cursor to end */
        L->Buf[Ed->CurCol] = '\0';
        L->Len = Ed->CurCol;
    } else if (Ed->CurRow + 1 < Ed->NumLines) {
        /* Kill newline ? merge lines */
        EDITOR_LINE *Next = &Ed->Lines[Ed->CurRow + 1];
        if (L->Len + Next->Len < EDITOR_MAX_LINE_LEN - 1) {
            CopyMem(L->Buf + L->Len, Next->Buf, Next->Len);
            L->Len           += Next->Len;
            L->Buf[L->Len]    = '\0';
            for (UINT32 i = Ed->CurRow + 1; i < Ed->NumLines - 1; i++)
                CopyMem(&Ed->Lines[i], &Ed->Lines[i+1], sizeof(EDITOR_LINE));
            Ed->NumLines--;
        }
    }
    Ed->Modified = TRUE;
}

/* ?? Status message ???????????????????????????????????????????????? */

STATIC VOID EditorSetMsg(EDITOR *Ed, const CHAR8 *Msg) {
    EStrCopy(Ed->StatusMsg, Msg, sizeof(Ed->StatusMsg));
    Ed->StatusMsgTimer = 120; /* ~4 sec at 30fps */
}

/* ?? Save ?????????????????????????????????????????????????????????? */

EFI_STATUS EditorSaveToVfs(EDITOR *Ed) {
    if (!Ed->Filename[0]) {
        EditorSetMsg(Ed, "No filename. Use Ctrl+N to set.");
        return EFI_INVALID_PARAMETER;
    }

    /* Build flat text from lines */
    VFS_FILE *File = VfsOpen(Ed->Filename);
    if (!File) File = VfsCreate(Ed->Filename);
    if (!File) {
        EditorSetMsg(Ed, "VFS full ? cannot save.");
        return EFI_OUT_OF_RESOURCES;
    }

    /* Reset file */
    File->Size = 0;

    for (UINT32 i = 0; i < Ed->NumLines; i++) {
        VfsAppend(File,
                  (const UINT8*)Ed->Lines[i].Buf,
                  Ed->Lines[i].Len);
        if (i + 1 < Ed->NumLines)
            VfsAppend(File, (const UINT8*)"\n", 1);
    }

    Ed->Modified = FALSE;
    EditorSetMsg(Ed, "Saved.");
    return EFI_SUCCESS;
}

/* ?? Load ?????????????????????????????????????????????????????????? */

VOID EditorLoadFromVfs(EDITOR *Ed, VFS_FILE *File) {
    SetMem(Ed->Lines, sizeof(Ed->Lines), 0);
    Ed->NumLines = 1;
    Ed->CurRow   = 0;
    Ed->CurCol   = 0;

    if (!File || File->Size == 0) return;

    UINT32 Row = 0;
    UINT32 Col = 0;
    for (UINT32 i = 0; i < File->Size && Row < EDITOR_MAX_LINES; i++) {
        CHAR8 Ch = (CHAR8)File->Data[i];
        if (Ch == '\n') {
            Ed->Lines[Row].Buf[Col] = '\0';
            Ed->Lines[Row].Len      = Col;
            Row++;
            Col = 0;
            Ed->NumLines = Row + 1;
            if (Ed->NumLines >= EDITOR_MAX_LINES) break;
        } else if (Col < EDITOR_MAX_LINE_LEN - 1) {
            Ed->Lines[Row].Buf[Col++] = Ch;
        }
    }
    Ed->Lines[Row].Buf[Col] = '\0';
    Ed->Lines[Row].Len      = Col;
    Ed->NumLines = Row + 1;
}

/* ?? Init ?????????????????????????????????????????????????????????? */

VOID EditorInit(EDITOR *Ed, GPU_FB *Fb,
                EFI_SIMPLE_TEXT_INPUT_PROTOCOL *TextIn,
                const CHAR8 *Filename) {
    SetMem(Ed, sizeof(EDITOR), 0);
    Ed->Fb     = Fb;
    Ed->TextIn = TextIn;

    /* Start with one empty line */
    Ed->NumLines = 1;
    Ed->Lines[0].Len = 0;
    Ed->Lines[0].Buf[0] = '\0';

    if (Filename && Filename[0]) {
        EStrCopy(Ed->Filename, Filename, EDITOR_FILENAME_MAX);
        VFS_FILE *F = VfsOpen(Filename);
        if (F) EditorLoadFromVfs(Ed, F);
    }

    EditorCalcLayout(Ed);
}

/* ?? Main event loop ??????????????????????????????????????????????? */

VOID EditorRun(EDITOR *Ed) {
    EditorDraw(Ed);

    for (;;) {
        EFI_INPUT_KEY Key;
        EFI_STATUS    Status;

        /* Spin-wait for keypress (no timer services post-ExitBootServices) */
        do {
            Status = Ed->TextIn->ReadKeyStroke(Ed->TextIn, &Key);
        } while (Status == EFI_NOT_READY);

        CHAR16 Uni  = Key.UnicodeChar;
        UINT16 Scan = Key.ScanCode;

        /* ?? Scan codes ?????????????????????????????????????? */
        if (Scan == SCAN_UP) {
            if (Ed->CurRow > 0) Ed->CurRow--;
            EditorClampCursor(Ed);
        } else if (Scan == SCAN_DOWN) {
            if (Ed->CurRow + 1 < Ed->NumLines) Ed->CurRow++;
            EditorClampCursor(Ed);
        } else if (Scan == SCAN_LEFT) {
            if (Ed->CurCol > 0) {
                Ed->CurCol--;
            } else if (Ed->CurRow > 0) {
                Ed->CurRow--;
                Ed->CurCol = Ed->Lines[Ed->CurRow].Len;
            }
        } else if (Scan == SCAN_RIGHT) {
            if (Ed->CurCol < Ed->Lines[Ed->CurRow].Len) {
                Ed->CurCol++;
            } else if (Ed->CurRow + 1 < Ed->NumLines) {
                Ed->CurRow++;
                Ed->CurCol = 0;
            }
        } else if (Scan == SCAN_HOME) {
            Ed->CurCol = 0;
        } else if (Scan == SCAN_END) {
            Ed->CurCol = Ed->Lines[Ed->CurRow].Len;
        } else if (Scan == SCAN_PGUP) {
            Ed->CurRow = (Ed->CurRow > Ed->VisRows) ? Ed->CurRow - Ed->VisRows : 0;
            EditorClampCursor(Ed);
        } else if (Scan == SCAN_PGDN) {
            Ed->CurRow += Ed->VisRows;
            if (Ed->CurRow >= Ed->NumLines) Ed->CurRow = Ed->NumLines - 1;
            EditorClampCursor(Ed);
        } else if (Scan == SCAN_DELETE) {
            EditorDeleteChar(Ed);
        } else if (Scan == SCAN_ESC) {
            /* ESC: if modified, warn; else quit */
            if (Ed->Modified)
                EditorSetMsg(Ed, "Unsaved changes! Use ^S to save or ^Q to force quit.");
            else
                break;

        /* ?? Unicode / control chars ????????????????????????? */
        } else if (Uni == CTRL_Q) {
            break;
        } else if (Uni == CTRL_S) {
            EditorSaveToVfs(Ed);
        } else if (Uni == CTRL_K) {
            EditorKillLine(Ed);
        } else if (Uni == CTRL_A) {
            Ed->CurCol = 0;
        } else if (Uni == CTRL_E) {
            Ed->CurCol = Ed->Lines[Ed->CurRow].Len;
        } else if (Uni == CHAR_BS) {
            EditorBackspace(Ed);
        } else if (Uni == CHAR_CR || Uni == CHAR_LF) {
            EditorInsertNewline(Ed);
        } else if (Uni >= 0x20 && Uni <= 0x7E) {
            EditorInsertChar(Ed, (CHAR8)Uni);
        }

        EditorDraw(Ed);
    }
}
