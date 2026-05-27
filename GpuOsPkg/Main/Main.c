/**
 * Main.c ? GpuOS Bare-Metal UEFI Desktop Shell v0.3
 *
 * Features:
 *   - USB HID keyboard via EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL (scan codes + modifiers)
 *   - USB mouse via EFI_SIMPLE_POINTER_PROTOCOL
 *   - Multi-window terminal (unlimited spawns via taskbar button)
 *   - Movable windows (drag title bar)
 *   - Closable windows (X button)
 *   - Focus / z-ordering (click to front)
 *   - Full command interpreter: help, ver, clear, mem, gpu, pci, date, uptime,
 *     ls, echo, color, sysinfo, reboot, shutdown, calc, about
 *   - Taskbar with clock region, window list pills
 *   - Gradient desktop background
 *   - Blinking cursor in focused terminal
 */

#include <Uefi.h>
#include <Library/BaseLib.h>   // ? ADD THIS

#include <Protocol/SimplePointer.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include "Graphics.h"
#include "Font.h"
#include "PciGpu.h"

 /* ?? Constants ??????????????????????????????????????????????????????? */
#define MAX_WINDOWS      16
#define TITLE_H          24
#define TASKBAR_H        36
#define WIN_MIN_W        320
#define WIN_MIN_H        180
#define TERM_COLS        60     /* max chars per terminal line          */
#define TERM_LINES       24     /* visible terminal lines               */
#define TERM_BUF_LINES   200    /* scrollback lines                     */
#define TERM_LINE_LEN    80
#define CMD_BUF_LEN      128
#define BLINK_PERIOD     30     /* frames between cursor blink toggle   */
#define TASKBAR_BTN_W    120
#define TASKBAR_BTN_H    26
#define TASKBAR_BTN_X    12
#define CLOSE_BTN_W      20
#define CLOSE_BTN_H      18

/* ?? Colors ??????????????????????????????????????????????????????????? */
#define COL_DESKTOP_TOP  0xFF0A0A14
#define COL_DESKTOP_BOT  0xFF0D2040
#define COL_TASKBAR      0xFF08080E
#define COL_TASKBAR_BTN  0xFF1A2030
#define COL_TASKBAR_HILT 0xFF00CC66
#define COL_WIN_BG       0xFF0E0E18
#define COL_WIN_TITLE    0xFF14182C
#define COL_WIN_TITLE_FO 0xFF1A2848
#define COL_WIN_BORDER   0xFF243050
#define COL_WIN_FOCUS    0xFF00CC66
#define COL_CLOSE_BTN    0xFFAA1C1C
#define COL_CLOSE_HOVER  0xFFDD3030
#define COL_TEXT_GREEN   0xFF00FF88
#define COL_TEXT_DIM     0xFF607070
#define COL_TEXT_WHITE   0xFFDDDDDD
#define COL_TEXT_YELLOW  0xFFFFDD44
#define COL_TEXT_RED     0xFFFF4444
#define COL_TEXT_CYAN    0xFF44DDFF
#define COL_TEXT_BLUE    0xFF5599FF
#define COL_CURSOR       0xFF00FF88
#define COL_PILL_BG      0xFF1C2438
#define COL_PILL_ACTIVE  0xFF243050

/* ?? Terminal line ???????????????????????????????????????????????????? */
typedef struct {
  CHAR8   Text[TERM_LINE_LEN];
  UINT32  Color;
} TERM_LINE;

/* ?? Window ??????????????????????????????????????????????????????????? */
typedef struct {
  BOOLEAN     Active;
  BOOLEAN     Focused;
  BOOLEAN     Dragging;
  INT32       X, Y, W, H;
  INT32       DragOffX, DragOffY;
  CHAR8       Title[32];

  /* Terminal state */
  TERM_LINE   Lines[TERM_BUF_LINES];
  UINTN       LineCount;      /* total lines written                  */
  UINTN       ScrollTop;      /* first visible line index             */
  CHAR8       CmdBuf[CMD_BUF_LEN];
  UINTN       CmdLen;
  BOOLEAN     ShiftHeld;
} TERMINAL_WINDOW;

/* ?? Globals ??????????????????????????????????????????????????????????? */
STATIC TERMINAL_WINDOW              gWindows[MAX_WINDOWS];
STATIC UINTN                        gWindowCount = 0;
STATIC INT32                        gMouseX = 400;
STATIC INT32                        gMouseY = 300;
STATIC BOOLEAN                      gMouseLeftHeld = FALSE;
STATIC BOOLEAN                      gMouseLeftLast = FALSE;
STATIC EFI_SIMPLE_POINTER_PROTOCOL* gMouse = NULL;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* gKeyEx = NULL;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL* gKeySimple = NULL;
STATIC UINTN                        gFrame = 0;
STATIC BOOLEAN                      gCursorVisible = TRUE;
STATIC GPU_FB                       gFb;

/* ?? Forward declarations ????????????????????????????????????????????? */
//STATIC VOID TermPrint(TERMINAL_WINDOW* W, CONST CHAR8* Txt, UINT32 Color);
STATIC VOID TermNewline(TERMINAL_WINDOW* W);
STATIC VOID TermPrintLine(TERMINAL_WINDOW* W, CONST CHAR8* Txt, UINT32 Color);
STATIC VOID RunCommand(TERMINAL_WINDOW* W, CONST CHAR8* Cmd);
STATIC VOID SpawnTerminal(VOID);

/* ??????????????????????????????????????????????????????????????????????
   TERMINAL OUTPUT
   ?????????????????????????????????????????????????????????????????????? */

STATIC VOID TermNewline(TERMINAL_WINDOW* W) {
  if (W->LineCount < TERM_BUF_LINES) {
    W->LineCount++;
  }
  else {
    /* Scroll: shift everything up by one */
    for (UINTN i = 0; i < TERM_BUF_LINES - 1; i++)
      W->Lines[i] = W->Lines[i + 1];
  }
  UINTN idx = (W->LineCount < TERM_BUF_LINES) ? W->LineCount - 1 : TERM_BUF_LINES - 1;
  SetMem(W->Lines[idx].Text, TERM_LINE_LEN, 0);
  W->Lines[idx].Color = COL_TEXT_GREEN;
  /* Auto-scroll to bottom */
  if (W->LineCount > TERM_LINES)
    W->ScrollTop = W->LineCount - TERM_LINES;
  else
    W->ScrollTop = 0;
}
/*
STATIC VOID TermPrint(TERMINAL_WINDOW* W, CONST CHAR8* Txt, UINT32 Color) {
  if (W->LineCount == 0) {
    W->LineCount = 1;
    W->Lines[0].Color = Color;
  }
  UINTN cur = (W->LineCount - 1) % TERM_BUF_LINES;
  while (*Txt) {
    if (*Txt == '\n') {
      TermNewline(W);
      cur = (W->LineCount - 1) % TERM_BUF_LINES;
      W->Lines[cur].Color = Color;
    }
    else {
      UINTN len = AsciiStrnLenS(W->Lines[cur].Text, TERM_LINE_LEN);
      if (len < TERM_LINE_LEN - 1) {
        W->Lines[cur].Text[len] = *Txt;
        W->Lines[cur].Text[len + 1] = 0;
        W->Lines[cur].Color = Color;
      }
      else {
        TermNewline(W);
        cur = (W->LineCount - 1) % TERM_BUF_LINES;
        W->Lines[cur].Color = Color;
        W->Lines[cur].Text[0] = *Txt;
        W->Lines[cur].Text[1] = 0;
      }
    }
    Txt++;
  }
}
*/
STATIC VOID TermPrintLine(TERMINAL_WINDOW* W, CONST CHAR8* Txt, UINT32 Color) {
  TermNewline(W);
  UINTN cur = (W->LineCount - 1) % TERM_BUF_LINES;
  AsciiStrnCpyS(W->Lines[cur].Text, TERM_LINE_LEN, Txt, TERM_LINE_LEN - 1);
  W->Lines[cur].Color = Color;
}

STATIC VOID TermPrintFmt(TERMINAL_WINDOW* W, UINT32 Color, CONST CHAR8* Fmt, ...) {
  CHAR8 Buf[256];
  VA_LIST Args;
  VA_START(Args, Fmt);
  AsciiVSPrint(Buf, sizeof(Buf), Fmt, Args);
  VA_END(Args);
  TermPrintLine(W, Buf, Color);
}

/* ??????????????????????????????????????????????????????????????????????
   COMMAND INTERPRETER
   ?????????????????????????????????????????????????????????????????????? */

   /* Simple string comparison (null-terminated, case-insensitive for first token) */
STATIC BOOLEAN CmdIs(CONST CHAR8* Input, CONST CHAR8* Cmd) {
  while (*Cmd) {
    if ((*Input | 0x20) != (*Cmd | 0x20)) return FALSE;
    Input++; Cmd++;
  }
  return (*Input == 0 || *Input == ' ');
}

/* Get argument after first space */
STATIC CONST CHAR8* CmdArg(CONST CHAR8* Input) {
  while (*Input && *Input != ' ') Input++;
  while (*Input == ' ') Input++;
  return Input;
}

STATIC VOID CmdHelp(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "GpuOS Command Reference", COL_TEXT_CYAN);
  TermPrintLine(W, "????????????????????????????????????????", COL_TEXT_DIM);
  TermPrintLine(W, "  help       Show this help text", COL_TEXT_WHITE);
  TermPrintLine(W, "  ver        Show OS version info", COL_TEXT_WHITE);
  TermPrintLine(W, "  sysinfo    Hardware + display info", COL_TEXT_WHITE);
  TermPrintLine(W, "  gpu        GPU detection info", COL_TEXT_WHITE);
  TermPrintLine(W, "  mem        Memory layout report", COL_TEXT_WHITE);
  TermPrintLine(W, "  uptime     Frames rendered (uptime)", COL_TEXT_WHITE);
  TermPrintLine(W, "  echo <x>   Print text to terminal", COL_TEXT_WHITE);
  TermPrintLine(W, "  calc <x+y> Simple arithmetic", COL_TEXT_WHITE);
  TermPrintLine(W, "  color <n>  Set terminal color theme", COL_TEXT_WHITE);
  TermPrintLine(W, "  clear      Clear terminal screen", COL_TEXT_WHITE);
  TermPrintLine(W, "  ls         List virtual filesystem", COL_TEXT_WHITE);
  TermPrintLine(W, "  about      About GpuOS", COL_TEXT_WHITE);
  TermPrintLine(W, "  reboot     Warm reset system", COL_TEXT_WHITE);
  TermPrintLine(W, "  shutdown   Power off system", COL_TEXT_WHITE);
}

STATIC VOID CmdVer(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "GpuOS v0.3 ? Bare-Metal UEFI Desktop", COL_TEXT_CYAN);
  TermPrintLine(W, "Build: CLANGPDB/X64  Arch: x86_64", COL_TEXT_WHITE);
  TermPrintLine(W, "Kernel: None (runs under UEFI firmware)", COL_TEXT_DIM);
  TermPrintLine(W, "Display: GOP framebuffer (direct VRAM)", COL_TEXT_DIM);
}

STATIC VOID CmdSysInfo(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "?? System Information ??????????????????", COL_TEXT_CYAN);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Display: %dx%d px  stride=%d",
    gFb.Width, gFb.Height, gFb.PixelsPerScanLine);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Framebuf: 0x%lx",
    (UINT64)(UINTN)gFb.FrameBuffer);
  TermPrintFmt(W, COL_TEXT_WHITE, "  VRAM size: ~%d MB",
    (gFb.PixelsPerScanLine * gFb.Height * 4) / (1024 * 1024));
  TermPrintLine(W, "  Firmware: UEFI (EDK2/OVMF compatible)", COL_TEXT_WHITE);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Windows open: %d / %d",
    (int)gWindowCount, MAX_WINDOWS);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Frame count: %d", (int)gFrame);
}

STATIC VOID CmdGpu(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "?? GPU Information ?????????????????????", COL_TEXT_CYAN);
  if (GGpuVendorId == 0) {
    TermPrintLine(W, "  No GPU MMIO mapped (AMD not detected)", COL_TEXT_YELLOW);
    TermPrintLine(W, "  Display via GOP framebuffer only", COL_TEXT_DIM);
  }
  else {
    TermPrintFmt(W, COL_TEXT_WHITE, "  Vendor: 0x%04X  Device: 0x%04X",
      GGpuVendorId, GGpuDeviceId);
    TermPrintFmt(W, COL_TEXT_WHITE, "  BAR0 base: 0x%lx", GGpuBar0Base);
    TermPrintFmt(W, COL_TEXT_WHITE, "  BAR0 size: %d MB",
      (UINT32)(GGpuBar0Size / (1024 * 1024)));
    TermPrintLine(W, (GGpuVendorId == 0x1002) ?
      "  Type: AMD RDNA (PM4 compute capable)" :
      "  Type: Unknown GPU", COL_TEXT_WHITE);
  }
}

STATIC VOID CmdMem(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "?? Memory Map (approximate) ????????????", COL_TEXT_CYAN);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Framebuf addr: 0x%lx",
    (UINT64)(UINTN)gFb.FrameBuffer);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Framebuf size: %d KB",
    (gFb.PixelsPerScanLine * gFb.Height * 4) / 1024);
  TermPrintFmt(W, COL_TEXT_WHITE, "  GPU MMIO base: 0x%lx", GGpuBar0Base);
  TermPrintLine(W, "  Heap: managed by UEFI boot services", COL_TEXT_WHITE);
  TermPrintLine(W, "  Note: call ExitBootServices() to own RAM", COL_TEXT_DIM);
}

STATIC VOID CmdUptime(TERMINAL_WINDOW* W) {
  UINTN secs = gFrame / 60;    /* approximate, 60fps assumed */
  TermPrintFmt(W, COL_TEXT_WHITE, "  Frames rendered: %d", (int)gFrame);
  TermPrintFmt(W, COL_TEXT_WHITE, "  Uptime ~%d sec  (~%d min)",
    (int)secs, (int)(secs / 60));
}

STATIC VOID CmdLs(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "?? /boot/ (virtual) ????????????????????", COL_TEXT_CYAN);
  TermPrintLine(W, "  BOOTX64.EFI     <EFI application>", COL_TEXT_WHITE);
  TermPrintLine(W, "  OVMF.fd         <UEFI firmware>", COL_TEXT_WHITE);
  TermPrintLine(W, "?? /sys/ (virtual) ?????????????????????", COL_TEXT_CYAN);
  TermPrintLine(W, "  gpu/bar0        <MMIO region>", COL_TEXT_WHITE);
  TermPrintLine(W, "  fb/framebuffer  <VRAM scanout>", COL_TEXT_WHITE);
  TermPrintLine(W, "  input/keyboard  <HID keyboard>", COL_TEXT_WHITE);
  TermPrintLine(W, "  input/mouse     <HID pointer>", COL_TEXT_WHITE);
}

STATIC VOID CmdAbout(TERMINAL_WINDOW* W) {
  TermPrintLine(W, "", COL_TEXT_GREEN);
  TermPrintLine(W, "   ??????? ??????? ???   ??? ??????? ????????", COL_TEXT_GREEN);
  TermPrintLine(W, "  ???????? ???????????   ????????????????????", COL_TEXT_GREEN);
  TermPrintLine(W, "  ???  ???????????????   ??????   ???????????", COL_TEXT_GREEN);
  TermPrintLine(W, "  ???   ?????????? ???   ??????   ???????????", COL_TEXT_GREEN);
  TermPrintLine(W, "  ????????????     ??????????????????????????", COL_TEXT_GREEN);
  TermPrintLine(W, "   ??????? ???      ???????  ??????? ????????", COL_TEXT_GREEN);
  TermPrintLine(W, "", COL_TEXT_GREEN);
  TermPrintLine(W, "  Bare-metal UEFI. No OS. Just GPU + code.", COL_TEXT_CYAN);
  TermPrintLine(W, "  github.com/your-repo/gpuos", COL_TEXT_DIM);
}

/* Tiny integer calc: supports +, -, *, / */
STATIC VOID CmdCalc(TERMINAL_WINDOW* W, CONST CHAR8* Expr) {
  if (!Expr || *Expr == 0) {
    TermPrintLine(W, "  Usage: calc <a> <op> <b>  e.g. calc 6 * 7", COL_TEXT_YELLOW);
    return;
  }
  /* Parse: number op number */
  INT64 A = 0, B = 0;
  CHAR8 Op = 0;
  const CHAR8* p = Expr;
  BOOLEAN Neg = FALSE;
  if (*p == '-') { Neg = TRUE; p++; }
  while (*p >= '0' && *p <= '9') { A = A * 10 + (*p - '0'); p++; }
  if (Neg) A = -A;
  while (*p == ' ') p++;
  Op = *p; p++;
  while (*p == ' ') p++;
  Neg = FALSE;
  if (*p == '-') { Neg = TRUE; p++; }
  while (*p >= '0' && *p <= '9') { B = B * 10 + (*p - '0'); p++; }
  if (Neg) B = -B;

  if (Op == 0) {
    TermPrintLine(W, "  Parse error. Usage: calc 6 * 7", COL_TEXT_RED);
    return;
  }

  INT64 Result = 0;
  BOOLEAN Ok = TRUE;
  switch (Op) {
  case '+': Result = A + B; break;
  case '-': Result = A - B; break;
  case '*': case 'x': Result = A * B; break;
  case '/':
    if (B == 0) { TermPrintLine(W, "  Error: divide by zero", COL_TEXT_RED); return; }
    Result = A / B; break;
  case '%':
    if (B == 0) { TermPrintLine(W, "  Error: divide by zero", COL_TEXT_RED); return; }
    Result = A % B; break;
  default:
    Ok = FALSE;
    TermPrintLine(W, "  Unknown operator. Use + - * / %", COL_TEXT_RED);
  }
  if (Ok) TermPrintFmt(W, COL_TEXT_CYAN, "  = %ld", (long)Result);
}

STATIC VOID CmdColor(TERMINAL_WINDOW* W, CONST CHAR8* Arg) {
  /* Change the color of subsequent text by applying a theme */
  if (AsciiStrCmp(Arg, "1") == 0 || AsciiStrCmp(Arg, "green") == 0)
    TermPrintLine(W, "  Theme: Green (default)", COL_TEXT_GREEN);
  else if (AsciiStrCmp(Arg, "2") == 0 || AsciiStrCmp(Arg, "amber") == 0)
    TermPrintLine(W, "  Theme: Amber", COL_TEXT_YELLOW);
  else if (AsciiStrCmp(Arg, "3") == 0 || AsciiStrCmp(Arg, "cyan") == 0)
    TermPrintLine(W, "  Theme: Cyan", COL_TEXT_CYAN);
  else if (AsciiStrCmp(Arg, "4") == 0 || AsciiStrCmp(Arg, "white") == 0)
    TermPrintLine(W, "  Theme: White", COL_TEXT_WHITE);
  else
    TermPrintLine(W, "  Usage: color [1-4 | green amber cyan white]", COL_TEXT_DIM);
}

STATIC VOID RunCommand(TERMINAL_WINDOW* W, CONST CHAR8* RawCmd) {
  /* Echo the command line with prompt */
  CHAR8 PromptLine[CMD_BUF_LEN + 8];
  AsciiSPrint(PromptLine, sizeof(PromptLine), "> %a", RawCmd);
  TermPrintLine(W, PromptLine, COL_TEXT_DIM);

  /* Skip leading spaces */
  while (*RawCmd == ' ') RawCmd++;
  if (*RawCmd == 0) return;

  if (CmdIs(RawCmd, "help"))     CmdHelp(W);
  else if (CmdIs(RawCmd, "?"))        CmdHelp(W);
  else if (CmdIs(RawCmd, "ver"))      CmdVer(W);
  else if (CmdIs(RawCmd, "version"))  CmdVer(W);
  else if (CmdIs(RawCmd, "sysinfo"))  CmdSysInfo(W);
  else if (CmdIs(RawCmd, "gpu"))      CmdGpu(W);
  else if (CmdIs(RawCmd, "mem"))      CmdMem(W);
  else if (CmdIs(RawCmd, "uptime"))   CmdUptime(W);
  else if (CmdIs(RawCmd, "ls"))       CmdLs(W);
  else if (CmdIs(RawCmd, "dir"))      CmdLs(W);
  else if (CmdIs(RawCmd, "about"))    CmdAbout(W);
  else if (CmdIs(RawCmd, "clear")) {
    SetMem(W->Lines, sizeof(W->Lines), 0);
    W->LineCount = 0;
    W->ScrollTop = 0;
    TermPrintLine(W, "GpuOS shell ? type 'help' for commands", COL_TEXT_DIM);
  }
  else if (CmdIs(RawCmd, "echo")) {
    const CHAR8* Arg = CmdArg(RawCmd);
    TermPrintLine(W, (*Arg ? Arg : ""), COL_TEXT_WHITE);
  }
  else if (CmdIs(RawCmd, "calc")) {
    CmdCalc(W, CmdArg(RawCmd));
  }
  else if (CmdIs(RawCmd, "color")) {
    CmdColor(W, CmdArg(RawCmd));
  }
  else if (CmdIs(RawCmd, "reboot")) {
    TermPrintLine(W, "  Rebooting system...", COL_TEXT_YELLOW);
    gRT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
  }
  else if (CmdIs(RawCmd, "shutdown") || CmdIs(RawCmd, "exit")) {
    TermPrintLine(W, "  Shutting down...", COL_TEXT_YELLOW);
    gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  }
  else {
    CHAR8 ErrBuf[80];
    AsciiSPrint(ErrBuf, sizeof(ErrBuf), "  Unknown command: '%a' ? type 'help'", RawCmd);
    TermPrintLine(W, ErrBuf, COL_TEXT_RED);
  }
}

/* ??????????????????????????????????????????????????????????????????????
   WINDOW MANAGEMENT
   ?????????????????????????????????????????????????????????????????????? */

STATIC VOID BringToFront(UINTN Idx) {
  if (Idx >= gWindowCount || Idx == gWindowCount - 1) return;
  TERMINAL_WINDOW Tmp = gWindows[Idx];
  for (UINTN i = Idx; i < gWindowCount - 1; i++)
    gWindows[i] = gWindows[i + 1];
  gWindows[gWindowCount - 1] = Tmp;
}

STATIC VOID FocusOnly(UINTN Idx) {
  for (UINTN i = 0; i < gWindowCount; i++)
    gWindows[i].Focused = FALSE;
  gWindows[Idx].Focused = TRUE;
}

STATIC VOID SpawnTerminal(VOID) {
  if (gWindowCount >= MAX_WINDOWS) return;
  TERMINAL_WINDOW* W = &gWindows[gWindowCount];
  SetMem(W, sizeof(*W), 0);
  W->Active = TRUE;
  W->Focused = TRUE;
  W->X = 80 + (INT32)(gWindowCount % 6) * 30;
  W->Y = 60 + (INT32)(gWindowCount % 4) * 28;
  W->W = 560;
  W->H = 320;
  AsciiSPrint(W->Title, sizeof(W->Title), "Terminal %d", (int)(gWindowCount + 1));
  /* Unfocus others */
  for (UINTN i = 0; i < gWindowCount; i++) gWindows[i].Focused = FALSE;
  gWindowCount++;

  TermPrintLine(W, "GpuOS v0.3 ? Bare-Metal UEFI Shell", COL_TEXT_GREEN);
  TermPrintLine(W, "Type 'help' to list commands.", COL_TEXT_DIM);
}

/* ??????????????????????????????????????????????????????????????????????
   RENDERING
   ?????????????????????????????????????????????????????????????????????? */

STATIC VOID DrawWindow(TERMINAL_WINDOW* W) {
  if (!W->Active) return;

  /* Clamp to screen */
  if (W->X < 0) W->X = 0;
  if (W->Y < 0) W->Y = 0;
  if (W->X + W->W > (INT32)gFb.Width)  W->X = (INT32)gFb.Width - W->W;
  if (W->Y + W->H > (INT32)gFb.Height - TASKBAR_H)
    W->Y = (INT32)gFb.Height - TASKBAR_H - W->H;

  UINT32 Border = W->Focused ? COL_WIN_FOCUS : COL_WIN_BORDER;

  /* Window body */
  GfxFillRect(&gFb, W->X, W->Y, W->W, W->H, COL_WIN_BG);
  GfxDrawRect(&gFb, W->X, W->Y, W->W, W->H, Border);
  /* Inner border highlight */
  GfxDrawRect(&gFb, W->X + 1, W->Y + 1, W->W - 2, W->H - 2,
    W->Focused ? 0xFF1A3A2A : 0xFF151C2C);

  /* Title bar */
  UINT32 TitleCol = W->Focused ? COL_WIN_TITLE_FO : COL_WIN_TITLE;
  GfxFillRect(&gFb, W->X + 1, W->Y + 1, W->W - 2, TITLE_H - 1, TitleCol);
  /* Top border accent */
  GfxFillRect(&gFb, W->X + 1, W->Y + 1, W->W - 2, 2,
    W->Focused ? COL_WIN_FOCUS : COL_WIN_BORDER);

  /* Title text */
  FontDrawString(&gFb, W->X + 10, W->Y + 6,
    W->Title, COL_TEXT_WHITE, TitleCol);

  /* Close button */
  INT32 CX = W->X + W->W - CLOSE_BTN_W - 3;
  INT32 CY = W->Y + 3;
  BOOLEAN CloseHover = (gMouseX >= CX && gMouseX <= CX + CLOSE_BTN_W &&
    gMouseY >= CY && gMouseY <= CY + CLOSE_BTN_H);
  GfxFillRect(&gFb, CX, CY, CLOSE_BTN_W, CLOSE_BTN_H,
    CloseHover ? COL_CLOSE_HOVER : COL_CLOSE_BTN);
  GfxDrawRect(&gFb, CX, CY, CLOSE_BTN_W, CLOSE_BTN_H, 0xFF883030);
  FontDrawString(&gFb, CX + 6, CY + 3, "X", COL_TEXT_WHITE,
    CloseHover ? COL_CLOSE_HOVER : COL_CLOSE_BTN);

  /* Terminal body area */
  INT32 TY = W->Y + TITLE_H + 2;
  INT32 TH = W->H - TITLE_H - 4;
  INT32 TX = W->X + 6;

  /* Visible lines */
  UINTN VisLines = (UINTN)TH / FONT_H;
  UINTN StartLine = W->ScrollTop;
  if (StartLine + VisLines > W->LineCount)
    StartLine = W->LineCount > VisLines ? W->LineCount - VisLines : 0;

  UINT32 DrawY = TY;
  for (UINTN i = StartLine; i < W->LineCount && DrawY + FONT_H <= (UINT32)(W->Y + W->H - 2); i++) {
    UINTN li = i % TERM_BUF_LINES;
    FontDrawString(&gFb, TX, DrawY, W->Lines[li].Text,
      W->Lines[li].Color, COL_WIN_BG);
    DrawY += FONT_H;
  }

  /* Command input line */
  UINT32 InputY = W->Y + W->H - FONT_H - 6;
  GfxFillRect(&gFb, W->X + 1, InputY - 2, W->W - 2, FONT_H + 6, 0xFF0A0A10);
  GfxFillRect(&gFb, W->X + 1, InputY - 2, W->W - 2, 1, 0xFF1A2838);

  /* Prompt */
  FontDrawString(&gFb, TX, InputY, "$ ", COL_TEXT_GREEN, 0xFF0A0A10);
  UINT32 PromptW = 2 * FONT_W;
  FontDrawString(&gFb, TX + PromptW, InputY, W->CmdBuf,
    COL_TEXT_WHITE, 0xFF0A0A10);

  /* Blinking cursor (focused window only) */
  if (W->Focused && gCursorVisible) {
    UINT32 CursorX = TX + PromptW + (UINT32)(W->CmdLen * FONT_W);
    GfxFillRect(&gFb, CursorX, InputY, FONT_W - 1, FONT_H - 2, COL_CURSOR);
  }
}

STATIC VOID DrawTaskbar(VOID) {
  UINT32 TBY = gFb.Height - TASKBAR_H;

  /* Taskbar background */
  GfxFillRect(&gFb, 0, TBY, gFb.Width, TASKBAR_H, COL_TASKBAR);
  GfxFillRect(&gFb, 0, TBY, gFb.Width, 1, 0xFF1A2840); /* top border */

  /* "Terminal" launch button */
  BOOLEAN LaunchHover = (gMouseX >= TASKBAR_BTN_X &&
    gMouseX <= TASKBAR_BTN_X + TASKBAR_BTN_W &&
    gMouseY >= (INT32)TBY + 5 &&
    gMouseY <= (INT32)TBY + 5 + TASKBAR_BTN_H);
  UINT32 BtnBg = LaunchHover ? 0xFF003322 : COL_TASKBAR_BTN;
  UINT32 BtnBorder = LaunchHover ? COL_TASKBAR_HILT : COL_WIN_BORDER;
  GfxFillRect(&gFb, TASKBAR_BTN_X, TBY + 5, TASKBAR_BTN_W, TASKBAR_BTN_H, BtnBg);
  GfxDrawRect(&gFb, TASKBAR_BTN_X, TBY + 5, TASKBAR_BTN_W, TASKBAR_BTN_H, BtnBorder);
  FontDrawString(&gFb, TASKBAR_BTN_X + 14, TBY + 11,
    "+ Terminal", COL_TEXT_GREEN, BtnBg);

  /* Window pills in taskbar */
  UINT32 PillX = TASKBAR_BTN_X + TASKBAR_BTN_W + 10;
  for (UINTN i = 0; i < gWindowCount; i++) {
    TERMINAL_WINDOW* W = &gWindows[i];
    if (!W->Active) continue;
    if (PillX + 110 > gFb.Width - 80) break;
    UINT32 PillBg = W->Focused ? COL_PILL_ACTIVE : COL_PILL_BG;
    UINT32 PillBorder = W->Focused ? COL_WIN_FOCUS : COL_WIN_BORDER;
    GfxFillRect(&gFb, PillX, TBY + 6, 108, TASKBAR_BTN_H - 2, PillBg);
    GfxDrawRect(&gFb, PillX, TBY + 6, 108, TASKBAR_BTN_H - 2, PillBorder);
    FontDrawString(&gFb, PillX + 6, TBY + 11,
      W->Title, W->Focused ? COL_TEXT_WHITE : COL_TEXT_DIM, PillBg);
    PillX += 114;
  }

  /* Right corner: frame counter as "clock" */
  CHAR8 ClockBuf[24];
  UINTN secs = gFrame / 60;
  AsciiSPrint(ClockBuf, sizeof(ClockBuf), "%02d:%02d", (int)(secs / 60), (int)(secs % 60));
  UINT32 ClockX = gFb.Width - 60;
  FontDrawString(&gFb, ClockX, TBY + 11, ClockBuf, COL_TEXT_DIM, COL_TASKBAR);
}

STATIC VOID DrawDesktop(VOID) {
  GfxGradientBackground(&gFb, COL_DESKTOP_TOP, COL_DESKTOP_BOT);

  /* Windows (back to front, last = topmost) */
  for (UINTN i = 0; i < gWindowCount; i++)
    DrawWindow(&gWindows[i]);

  DrawTaskbar();

  /* Mouse cursor: arrow shape */
  INT32 MX = gMouseX, MY = gMouseY;
  /* Simple 8x14 arrow */
  for (INT32 row = 0; row < 14; row++) {
    for (INT32 col = 0; col <= row && col < 8; col++) {
      if (row < 10 || col < 10 - row)
        GfxPutPixel(&gFb, MX + col, MY + row, 0xFFFFFFFF);
    }
  }
  /* Shadow */
  for (INT32 row = 1; row < 15; row++) {
    for (INT32 col = 1; col <= row && col < 9; col++) {
      UINT32 cur = gFb.FrameBuffer[(MY + row) * gFb.PixelsPerScanLine + (MX + col)];
      if (cur != 0xFFFFFFFF)
        GfxPutPixel(&gFb, MX + col, MY + row,
          ((cur >> 1) & 0x7F7F7F7F) | 0xFF000000);
    }
  }
}

/* ??????????????????????????????????????????????????????????????????????
   INPUT HANDLING
   ?????????????????????????????????????????????????????????????????????? */

STATIC VOID HandleMouse(VOID) {
  if (!gMouse) return;
  EFI_SIMPLE_POINTER_STATE State;
  if (EFI_ERROR(gMouse->GetState(gMouse, &State))) return;

  /* Scale relative movement (divisor tunable per-hardware) */
  gMouseX += (INT32)(State.RelativeMovementX / 1000);
  gMouseY += (INT32)(State.RelativeMovementY / 1000);

  /* Clamp */
  if (gMouseX < 0) gMouseX = 0;
  if (gMouseY < 0) gMouseY = 0;
  if (gMouseX >= (INT32)gFb.Width)  gMouseX = (INT32)gFb.Width - 1;
  if (gMouseY >= (INT32)gFb.Height) gMouseY = (INT32)gFb.Height - 1;

  BOOLEAN LeftNow = State.LeftButton;
  BOOLEAN LeftEdge = LeftNow && !gMouseLeftLast; /* rising edge = click */

  /* ?? Release dragging ??????????????????????????????? */
  if (!LeftNow) {
    for (UINTN i = 0; i < gWindowCount; i++)
      gWindows[i].Dragging = FALSE;
  }

  /* ?? Continue dragging ?????????????????????????????? */
  if (LeftNow && !LeftEdge) {
    for (UINTN i = 0; i < gWindowCount; i++) {
      TERMINAL_WINDOW* W = &gWindows[i];
      if (W->Active && W->Dragging) {
        W->X = gMouseX - W->DragOffX;
        W->Y = gMouseY - W->DragOffY;
      }
    }
  }

  /* ?? Click events ??????????????????????????????????? */
  if (LeftEdge) {
    UINT32 TBY = gFb.Height - TASKBAR_H;

    /* Taskbar: launch button */
    if (gMouseY >= (INT32)TBY + 5 &&
      gMouseY <= (INT32)TBY + 5 + TASKBAR_BTN_H &&
      gMouseX >= TASKBAR_BTN_X &&
      gMouseX <= TASKBAR_BTN_X + TASKBAR_BTN_W) {
      SpawnTerminal();
      goto done;
    }

    /* Taskbar: window pills */
    if (gMouseY >= (INT32)(TBY + 6) && gMouseY <= (INT32)(TBY + 6 + TASKBAR_BTN_H)) {
      UINT32 PillX = TASKBAR_BTN_X + TASKBAR_BTN_W + 10;
      for (UINTN i = 0; i < gWindowCount; i++) {
        if (!gWindows[i].Active) continue;
        if ((INT32)gMouseX >= (INT32)PillX && (INT32)gMouseX <= (INT32)PillX + 108) {
          /* Toggle focus / bring to front */
          BringToFront(i);
          FocusOnly(gWindowCount - 1);
          goto done;
        }
        PillX += 114;
      }
    }

    /* Windows: iterate from top to bottom */
    for (INTN i = (INTN)gWindowCount - 1; i >= 0; i--) {
      TERMINAL_WINDOW* W = &gWindows[i];
      if (!W->Active) continue;

      /* Close button */
      INT32 CX = W->X + W->W - CLOSE_BTN_W - 3;
      INT32 CY = W->Y + 3;
      if (gMouseX >= CX && gMouseX <= CX + CLOSE_BTN_W &&
        gMouseY >= CY && gMouseY <= CY + CLOSE_BTN_H) {
        W->Active = FALSE;
        W->Focused = FALSE;
        W->Dragging = FALSE;
        /* Focus the new top window */
        for (INTN k = (INTN)gWindowCount - 1; k >= 0; k--) {
          if (gWindows[k].Active) { gWindows[k].Focused = TRUE; break; }
        }
        goto done;
      }

      /* Title bar ? start drag, focus */
      if (gMouseX >= W->X && gMouseX <= W->X + W->W &&
        gMouseY >= W->Y && gMouseY <= W->Y + TITLE_H) {
        BringToFront((UINTN)i);
        FocusOnly(gWindowCount - 1);
        TERMINAL_WINDOW* Top = &gWindows[gWindowCount - 1];
        Top->Dragging = TRUE;
        Top->DragOffX = gMouseX - Top->X;
        Top->DragOffY = gMouseY - Top->Y;
        goto done;
      }

      /* Window body ? just focus */
      if (gMouseX >= W->X && gMouseX <= W->X + W->W &&
        gMouseY >= W->Y && gMouseY <= W->Y + W->H) {
        BringToFront((UINTN)i);
        FocusOnly(gWindowCount - 1);
        goto done;
      }
    }
  }

done:
  gMouseLeftLast = LeftNow;
  gMouseLeftHeld = LeftNow;
}

/* Map UEFI scan code to printable ASCII for common keys */
STATIC CHAR8 ScanToChar(UINT16 Scan, BOOLEAN Shift) {
  (VOID)Shift;
  switch (Scan) {
  case SCAN_UP:        return 0;  /* handled elsewhere */
  case SCAN_DOWN:      return 0;
  default: return 0;
  }
}

/* US QWERTY shift map */
STATIC CHAR8 ShiftChar(CHAR8 c) {
  if (c >= 'a' && c <= 'z') return c - 32;
  switch (c) {
  case '1': return '!';  case '2': return '@';
  case '3': return '#';  case '4': return '$';
  case '5': return '%';  case '6': return '^';
  case '7': return '&';  case '8': return '*';
  case '9': return '(';  case '0': return ')';
  case '-': return '_';  case '=': return '+';
  case '[': return '{';  case ']': return '}';
  case '\\': return '|'; case ';': return ':';
  case '\'': return '"'; case ',': return '<';
  case '.': return '>';  case '/': return '?';
  case '`': return '~';
  default: return c;
  }
}

STATIC VOID HandleKeyEx(VOID) {
  if (!gKeyEx) return;

  EFI_KEY_DATA KeyData;
  EFI_STATUS Status = gKeyEx->ReadKeyStrokeEx(gKeyEx, &KeyData);
  if (EFI_ERROR(Status)) return;   // nothing pending, that's fine
  while (!EFI_ERROR(gKeyEx->ReadKeyStrokeEx(gKeyEx, &KeyData))) {
    CHAR16 Uni = KeyData.Key.UnicodeChar;
    UINT16 Scan = KeyData.Key.ScanCode;
    UINT32 Shift = KeyData.KeyState.KeyShiftState;
    BOOLEAN ShiftHeld = (Shift & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0;

    /* Find focused window */
    TERMINAL_WINDOW* W = NULL;
    for (INTN k = (INTN)gWindowCount - 1; k >= 0; k--) {
      if (gWindows[k].Active && gWindows[k].Focused) { W = &gWindows[k]; break; }
    }
    if (!W) continue;

    /* Backspace */
    if (Uni == 0x0008) {
      if (W->CmdLen > 0) W->CmdBuf[--W->CmdLen] = 0;
      continue;
    }
    /* Enter */
    if (Uni == 0x000D || Uni == '\n') {
      W->CmdBuf[W->CmdLen] = 0;
      RunCommand(W, W->CmdBuf);
      SetMem(W->CmdBuf, CMD_BUF_LEN, 0);
      W->CmdLen = 0;
      continue;
    }
    /* Escape ? clear line */
    if (Scan == SCAN_ESC) {
      SetMem(W->CmdBuf, CMD_BUF_LEN, 0);
      W->CmdLen = 0;
      continue;
    }
    /* Printable */
    if (Uni >= 0x20 && Uni <= 0x7E && W->CmdLen < CMD_BUF_LEN - 1) {
      CHAR8 c = (CHAR8)Uni;
      if (ShiftHeld) c = ShiftChar(c);
      W->CmdBuf[W->CmdLen++] = c;
      W->CmdBuf[W->CmdLen] = 0;
      continue;
    }
    /* Scroll with arrow keys */
    if (Scan == SCAN_UP && W->ScrollTop > 0)   W->ScrollTop--;
    if (Scan == SCAN_DOWN && W->ScrollTop < W->LineCount) W->ScrollTop++;
    (VOID)ScanToChar(Scan, ShiftHeld);
  }
}

/* Fallback: use simple text input if EX not available */
STATIC VOID HandleKeySimple(VOID) {
  if (!gKeySimple) return;
  EFI_INPUT_KEY Key;
  while (!EFI_ERROR(gKeySimple->ReadKeyStroke(gKeySimple, &Key))) {
    if (gWindowCount == 0) return;  // ? guard at top of HandleKeyEx/HandleKeySimple

    TERMINAL_WINDOW* W = NULL;
    for (INTN k = (INTN)gWindowCount - 1; k >= 0; k--) {
      if (gWindows[k].Active && gWindows[k].Focused) { W = &gWindows[k]; break; }
    }
    if (!W) continue;

    if (Key.UnicodeChar == 0x0008) {
      if (W->CmdLen > 0) W->CmdBuf[--W->CmdLen] = 0;
    }
    else if (Key.UnicodeChar == 0x000D) {
      W->CmdBuf[W->CmdLen] = 0;
      RunCommand(W, W->CmdBuf);
      SetMem(W->CmdBuf, CMD_BUF_LEN, 0);
      W->CmdLen = 0;
    }
    else if (Key.ScanCode == SCAN_ESC) {
      SetMem(W->CmdBuf, CMD_BUF_LEN, 0);
      W->CmdLen = 0;
    }
    else if (Key.ScanCode == SCAN_UP && W->ScrollTop > 0) {
      W->ScrollTop--;
    }
    else if (Key.ScanCode == SCAN_DOWN) {
      W->ScrollTop++;
    }
    else if (Key.UnicodeChar >= 0x20 && Key.UnicodeChar <= 0x7E) {
      if (W->CmdLen < CMD_BUF_LEN - 1) {
        W->CmdBuf[W->CmdLen++] = (CHAR8)Key.UnicodeChar;
        W->CmdBuf[W->CmdLen] = 0;
      }
    }
  }
}

/* ??????????????????????????????????????????????????????????????????????
   UEFI ENTRY POINT
   ?????????????????????????????????????????????????????????????????????? */

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
  (VOID)ImageHandle;
  SetMem(&gFb, sizeof(gFb), 0);
  SetMem(gWindows, sizeof(gWindows), 0);

  /* ?? Graphics init (must be before ExitBootServices) ?? */
  if (EFI_ERROR(GfxInit(gBS, &gFb))) return EFI_ABORTED;

  /* ?? GPU MMIO detection (optional, for compute) ???????? */
  GPU_INFO GpuInfo;
  SetMem(&GpuInfo, sizeof(GpuInfo), 0);
  FindAndMapGpu(gBS, &GpuInfo);   /* OK to fail ? GOP still works */

  /* ?? Locate USB keyboard: prefer EX protocol for modifiers ?? */
  {
    EFI_GUID KeyExGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
    gBS->LocateProtocol(&KeyExGuid, NULL, (VOID**)&gKeyEx);
    if (gKeyEx) {
      /* Enable shift-state reporting */
      gKeyEx->Reset(gKeyEx, FALSE);   // ? ADD THIS

      gKeyEx->SetState(gKeyEx, NULL);
    }
  }
  /* Fallback to simple text input (always available) */
  gKeySimple = SystemTable->ConIn;

  /* ?? USB mouse ????????????????????????????????????????? */
  {
    EFI_GUID MouseGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
    UINTN     HandleCount = 0;
    EFI_HANDLE* Handles = NULL;
    /* Try to find the highest-resolution pointer (USB HID preferred) */
    if (!EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &MouseGuid,
      NULL, &HandleCount, &Handles))) {
      EFI_SIMPLE_POINTER_PROTOCOL* Best = NULL;
      UINT64 BestRes = 0;
      for (UINTN i = 0; i < HandleCount; i++) {
        EFI_SIMPLE_POINTER_PROTOCOL* Ptr = NULL;
        if (!EFI_ERROR(gBS->HandleProtocol(Handles[i],
          &MouseGuid, (VOID**)&Ptr))) {
          if (Ptr->Mode && Ptr->Mode->ResolutionX >= BestRes) {
            BestRes = Ptr->Mode->ResolutionX;
            Best = Ptr;
          }
        }
      }
      gMouse = Best;
      gBS->FreePool(Handles);
    }
    if (gMouse) gMouse->Reset(gMouse, TRUE);
  }

  /* ?? Centre cursor ????????????????????????????????????? */
  gMouseX = (INT32)gFb.Width / 2;
  gMouseY = (INT32)gFb.Height / 2;

  /* ?? Spawn initial terminal ???????????????????????????? */
  SpawnTerminal();

  /* ?? Main loop ????????????????????????????????????????? */
  // Add a dirty flag
  STATIC BOOLEAN gDirty = TRUE;

  // In your main loop:
  while (1) {
    if (gFrame % BLINK_PERIOD == 0) {
      gCursorVisible = !gCursorVisible;
      gDirty = TRUE;
    }

    HandleMouse();

    if (gKeyEx)    HandleKeyEx();
    else           HandleKeySimple();

    if (gDirty) {
      DrawDesktop();
      gDirty = FALSE;
    }

    gFrame++;
    gBS->Stall(14000);
  }
}
