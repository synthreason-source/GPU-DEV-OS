/**
 * Main.c ? GpuOS UEFI entry point v2
 *
 * Changes from v1:
 *   - Double-buffered rendering via GfxFlip() ? eliminates all flicker
 *   - Virtual filesystem (VFS) initialised before ExitBootServices
 *   - Text editor (press E) backed by VFS with Ctrl+S save
 *   - Key: E=editor  O=open  L=list files  R=redraw  W=open welcome.txt
 */
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Protocol/SimpleTextIn.h>

#include "Graphics.h"
#include "Font.h"
#include "PciGpu.h"
#include "VFS.h"
#include "Editor.h"

#define GPUOS_VERSION "GpuOS v2.0 | Bare-Metal UEFI"

/* EFI scan codes we use in Main */


STATIC GPU_FB   GFb;
STATIC GPU_INFO GGpuInfo;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL *GTextIn = NULL;

/* ?? Helpers ??????????????????????????????????????????????????????? */

STATIC VOID SpinDelay(UINT32 N) {
    for (volatile UINT32 i = 0; i < N; i++) {}
}

/* Uint32 -> decimal ASCII */
STATIC VOID U32ToStr(UINT32 V, CHAR8 *Buf, UINT32 BufSz) {
    if (BufSz < 2) { if (BufSz) Buf[0]='\0'; return; }
    CHAR8 Tmp[12]; INT32 i = 0;
    if (V == 0) { Tmp[i++]='0'; }
    while (V > 0 && i < 11) { Tmp[i++]=(CHAR8)('0'+V%10); V/=10; }
    UINT32 out=0;
    while (i>0 && out+1<BufSz) Buf[out++]=Tmp[--i];
    Buf[out]='\0';
}

STATIC UINT32 StrLen8(const CHAR8 *S) {
    UINT32 n=0; while(S[n]) n++; return n;
}

/* Build a string from segments without sprintf */
STATIC CHAR8 *AppendStr(CHAR8 *Dst, const CHAR8 *Src, CHAR8 *End) {
    while (*Src && Dst < End-1) *Dst++ = *Src++;
    *Dst='\0'; return Dst;
}
STATIC CHAR8 *AppendU32(CHAR8 *Dst, UINT32 V, CHAR8 *End) {
    CHAR8 Tmp[12]; U32ToStr(V,Tmp,sizeof(Tmp));
    return AppendStr(Dst, Tmp, End);
}

/* ?? Screens ??????????????????????????????????????????????????????? */

STATIC VOID DrawHomeScreen(VOID) {
    UINT32 W = GFb.Width, H = GFb.Height;

    GfxGradientBackground(&GFb, COLOR_BGRA(8,8,24), COLOR_BGRA(2,2,10));

    /* Title bar */
    UINT32 TH = FONT_H * 3;
    GfxFillRect(&GFb, 0, 0, W, TH, COLOR_TITLE_BAR);
    GfxFillRect(&GFb, 0, TH, W, 2, COLOR_ACCENT);
    FontDrawStringScaled(&GFb, 16, 8, "GpuOS", COLOR_ACCENT, COLOR_TITLE_BAR, 3);
    FontDrawString(&GFb, 16+5*FONT_W*3+8, 8+FONT_H,
        "Bare-Metal UEFI GPU Pipeline", COLOR_WHITE, COLOR_TITLE_BAR);

    /* Info panel */
    UINT32 PX=24, PY=TH+20, PW=W-48, PH=FONT_H*8+16;
    GfxFillRect(&GFb, PX, PY, PW, PH, COLOR_PANEL);
    GfxDrawRect (&GFb, PX, PY, PW, PH, COLOR_ACCENT);

    UINT32 TX=PX+12, TY=PY+10, LS=FONT_H+4;
    FontDrawString(&GFb, TX, TY, "System", COLOR_ACCENT, COLOR_PANEL); TY+=LS+4;
    GfxFillRect(&GFb, TX, TY, PW-24, 1, COLOR_DIM); TY+=8;

    /* Resolution */
    CHAR8 Buf[128]; CHAR8 *End=Buf+sizeof(Buf); CHAR8 *p=Buf;
    p=AppendStr(p,"Resolution:  ",End);
    p=AppendU32(p,GFb.Width,End);
    p=AppendStr(p," x ",End);
    p=AppendU32(p,GFb.Height,End);
    FontDrawString(&GFb, TX, TY, Buf, COLOR_WHITE, COLOR_PANEL); TY+=LS;

    /* GPU */
    if (GGpuInfo.VendorId==0x1002)
        FontDrawString(&GFb, TX, TY, "GPU:         AMD display controller detected", COLOR_GREEN, COLOR_PANEL);
    else
        FontDrawString(&GFb, TX, TY, "GOP:         Framebuffer active (generic)", COLOR_WHITE, COLOR_PANEL);
    TY+=LS;

    /* VFS */
    p=Buf;
    p=AppendStr(p,"VFS:         ",End);
    if (GVfs) {
        p=AppendU32(p,GVfs->Count,End);
        p=AppendStr(p," file(s) in RAM filesystem",End);
    } else {
        p=AppendStr(p,"not initialised",End);
    }
    FontDrawString(&GFb, TX, TY, Buf, COLOR_CYAN, COLOR_PANEL); TY+=LS;

    FontDrawString(&GFb, TX, TY,
        "Boot:        ExitBootServices OK ? OS permanently blocked",
        COLOR_WHITE, COLOR_PANEL); TY+=LS;
    FontDrawString(&GFb, TX, TY,
        "Renderer:    Double-buffered (GfxFlip) ? zero flicker",
        COLOR_WHITE, COLOR_PANEL);

    /* Key bindings panel */
    UINT32 KY=PY+PH+16, KH=FONT_H*8+16;
    GfxFillRect(&GFb, PX, KY, PW, KH, COLOR_PANEL);
    GfxDrawRect (&GFb, PX, KY, PW, KH, COLOR_DIM);

    UINT32 KTY=KY+10;
    FontDrawString(&GFb, TX, KTY, "Keys", COLOR_ACCENT, COLOR_PANEL); KTY+=LS+4;
    GfxFillRect(&GFb, TX, KTY, PW-24, 1, COLOR_DIM); KTY+=8;
    FontDrawString(&GFb, TX, KTY, "  E   New file in editor",        COLOR_WHITE, COLOR_PANEL); KTY+=LS;
    FontDrawString(&GFb, TX, KTY, "  O   Open existing VFS file",    COLOR_WHITE, COLOR_PANEL); KTY+=LS;
    FontDrawString(&GFb, TX, KTY, "  W   Open welcome.txt",          COLOR_WHITE, COLOR_PANEL); KTY+=LS;
    FontDrawString(&GFb, TX, KTY, "  L   List all VFS files",        COLOR_WHITE, COLOR_PANEL); KTY+=LS;
    FontDrawString(&GFb, TX, KTY, "  R   Redraw home screen",        COLOR_WHITE, COLOR_PANEL);

    /* Footer */
    UINT32 FY=H-FONT_H*2;
    GfxFillRect(&GFb, 0, FY, W, 2, COLOR_DIM);
    FontDrawString(&GFb, 16, FY+8,
        GPUOS_VERSION "  |  No Windows. No Linux. No kernel.",
        COLOR_DIM, COLOR_DARK_BG);

    GfxFlip(&GFb);
}

STATIC VOID DrawVfsList(VOID) {
    GfxClear(&GFb, COLOR_DARK_BG);
    GfxFillRect(&GFb, 0, 0, GFb.Width, FONT_H*2, COLOR_TITLE_BAR);
    FontDrawString(&GFb, 12, 8,
        "VFS ? Virtual Filesystem  (any key to return)",
        COLOR_ACCENT, COLOR_TITLE_BAR);

    UINT32 Y=FONT_H*3;
    if (!GVfs || GVfs->Count==0) {
        FontDrawString(&GFb, 24, Y, "No files in VFS.", COLOR_DIM, COLOR_DARK_BG);
    } else {
        const CHAR8 *Names[VFS_MAX_FILES];
        UINT32 N = VfsListFiles(Names, VFS_MAX_FILES);
        for (UINT32 i=0; i<N && Y<GFb.Height-FONT_H*2; i++) {
            VFS_FILE *F = VfsOpen(Names[i]);
            CHAR8 Row[128]; CHAR8 *End=Row+sizeof(Row); CHAR8 *rp=Row;
            rp=AppendStr(rp,"  [",End);
            rp=AppendU32(rp,i+1,End);
            rp=AppendStr(rp,"]  ",End);
            rp=AppendStr(rp,Names[i],End);
            rp=AppendStr(rp,"  (",End);
            rp=AppendU32(rp, F ? F->Size : 0, End);
            rp=AppendStr(rp," bytes)",End);
            FontDrawString(&GFb, 24, Y, Row,
                (i%2==0)?COLOR_WHITE:COLOR_CYAN, COLOR_DARK_BG);
            Y+=FONT_H+4;
        }
    }
    GfxFlip(&GFb);
    EFI_INPUT_KEY K; while(GTextIn->ReadKeyStroke(GTextIn,&K)==EFI_NOT_READY){}
}

/* Prompt for a filename; returns TRUE if user confirmed a non-empty name */
STATIC BOOLEAN PromptFilename(CHAR8 *Out, UINT32 MaxLen) {
    GfxClear(&GFb, COLOR_DARK_BG);
    GfxFillRect(&GFb, 0, 0, GFb.Width, FONT_H*2, COLOR_TITLE_BAR);
    FontDrawString(&GFb, 12, 8,
        "Enter filename  (Enter=confirm  Esc=cancel):",
        COLOR_ACCENT, COLOR_TITLE_BAR);

    UINT32 BoxX=24, BoxY=FONT_H*4, BoxW=64*FONT_W, BoxH=FONT_H+8;
    GfxFillRect(&GFb, BoxX, BoxY, BoxW, BoxH, COLOR_PANEL);
    GfxDrawRect (&GFb, BoxX, BoxY, BoxW, BoxH, COLOR_ACCENT);
    GfxFlip(&GFb);

    UINT32 Len=0; Out[0]='\0';
    for (;;) {
        EFI_INPUT_KEY Key;
        while (GTextIn->ReadKeyStroke(GTextIn,&Key)==EFI_NOT_READY) {}

        if (Key.ScanCode==SCAN_ESC) return FALSE;
        CHAR16 Ch=Key.UnicodeChar;
        if (Ch==0x0D) { Out[Len]='\0'; return Len>0; }
        if (Ch==0x08 && Len>0) Out[--Len]='\0';
        else if (Ch>=0x20 && Ch<=0x7E && Len+1<MaxLen) {
            Out[Len++]=(CHAR8)Ch; Out[Len]='\0';
        }
        GfxFillRect(&GFb, BoxX+4, BoxY+4, BoxW-8, FONT_H, COLOR_PANEL);
        FontDrawString(&GFb, BoxX+4, BoxY+4, Out, COLOR_WHITE, COLOR_PANEL);
        UINT32 CurX=BoxX+4+Len*FONT_W;
        GfxFillRect(&GFb, CurX, BoxY+4, 2, FONT_H, COLOR_ACCENT);
        GfxFlip(&GFb);
    }
}

STATIC VOID OpenEditor(const CHAR8 *Filename) {
    EDITOR Ed;
    EditorInit(&Ed, &GFb, GTextIn, Filename);
    EditorRun(&Ed);
}

/* ?? UEFI entry point ?????????????????????????????????????????????? */
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

    /* 1. Keyboard */
    {
        EFI_GUID Guid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
        if (EFI_ERROR(BS->LocateProtocol(&Guid,NULL,(VOID**)&GTextIn)))
            GTextIn = SystemTable->ConIn;
    }

    /* 2. Framebuffer + back-buffer */
    if (EFI_ERROR(GfxInit(BS, &GFb))) return EFI_DEVICE_ERROR;

    /* 3. VFS ? must happen before ExitBootServices */
    if (!EFI_ERROR(VfsInit(BS))) {
        VFS_FILE *F = VfsCreate("welcome.txt");
        if (F) {
            const CHAR8 *Msg =
                "Welcome to GpuOS!\r\n"
                "\r\n"
                "This file lives entirely in RAM.\r\n"
                "Press Ctrl+S in the editor to save your changes.\r\n"
                "Press Ctrl+Q to return to the home screen.\r\n"
                "\r\n"
                "Keybindings:\r\n"
                "  Arrow keys   move cursor\r\n"
                "  Home / End   start / end of line\r\n"
                "  PgUp / PgDn  scroll viewport\r\n"
                "  Ctrl+K       kill (delete) current line\r\n"
                "  Ctrl+S       save to VFS\r\n"
                "  Ctrl+Q       quit editor\r\n";
            VfsWrite(F, (const UINT8*)Msg, StrLen8(Msg));
        }
    }

    /* 4. AMD GPU detection (optional) */
    FindAndMapGpu(BS, &GGpuInfo);

    /* 5. Exit boot services */
    {
        UINTN MapSz=0, MapKey=0, DescSz=0; UINT32 DescVer=0;
        EFI_MEMORY_DESCRIPTOR *Map=NULL;
        BS->GetMemoryMap(&MapSz,Map,&MapKey,&DescSz,&DescVer);
        MapSz+=2*DescSz;
        BS->AllocatePool(EfiLoaderData,MapSz,(VOID**)&Map);
        BS->GetMemoryMap(&MapSz,Map,&MapKey,&DescSz,&DescVer);
        BS->ExitBootServices(ImageHandle,MapKey);
        /* ??? Boot services gone ??? */
    }

    /* 6. Home screen */
    DrawHomeScreen();

    /* 7. Event loop */
    for (;;) {
        EFI_INPUT_KEY Key;
        while (GTextIn->ReadKeyStroke(GTextIn,&Key)==EFI_NOT_READY)
            SpinDelay(500);

        CHAR16 Ch = Key.UnicodeChar;

        if (Ch=='e'||Ch=='E') {
            CHAR8 Name[EDITOR_FILENAME_MAX];
            if (PromptFilename(Name,sizeof(Name))) OpenEditor(Name);
            DrawHomeScreen();
        } else if (Ch=='o'||Ch=='O') {
            CHAR8 Name[EDITOR_FILENAME_MAX];
            if (PromptFilename(Name,sizeof(Name))) OpenEditor(Name);
            DrawHomeScreen();
        } else if (Ch=='w'||Ch=='W') {
            OpenEditor("welcome.txt");
            DrawHomeScreen();
        } else if (Ch=='l'||Ch=='L') {
            DrawVfsList();
            DrawHomeScreen();
        } else if (Ch=='r'||Ch=='R') {
            DrawHomeScreen();
        }
    }
}
