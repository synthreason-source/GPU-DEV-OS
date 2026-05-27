#include <Uefi.h>
#include <Protocol/SimplePointer.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include "Graphics.h"
#include "Font.h"

#define MAX_WINDOWS 8
#define TITLE_H 22
#define TASKBAR_H 32

typedef struct {
  BOOLEAN Active;
  BOOLEAN Focused;
  UINT32 X,Y,W,H;
  CHAR8 Title[32];
  CHAR8 Buffer[1024];
  UINTN BufferLen;
} TERMINAL_WINDOW;

STATIC TERMINAL_WINDOW gWindows[MAX_WINDOWS];
STATIC UINTN gWindowCount = 0;
STATIC INT32 gMouseX = 100;
STATIC INT32 gMouseY = 100;
STATIC EFI_SIMPLE_POINTER_PROTOCOL *gMouse = NULL;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL *gKeyboard = NULL;

STATIC VOID AppendText(TERMINAL_WINDOW *Win, CONST CHAR8 *Txt) {
  while (*Txt && Win->BufferLen < sizeof(Win->Buffer)-1) {
    Win->Buffer[Win->BufferLen++] = *Txt++;
  }
  Win->Buffer[Win->BufferLen] = 0;
}

STATIC VOID RunCommand(TERMINAL_WINDOW *Win, CHAR8 Cmd) {
  if (Cmd == 't') AppendText(Win, "\nCurrent time command invoked");
  else if (Cmd == 'v') AppendText(Win, "\nGpuOS version 0.2 desktop preview");
  else if (Cmd == 'e') AppendText(Win, "\nText editor opened (placeholder)");
  else if (Cmd == 'h') AppendText(Win, "\nCommands: t=time v=version e=editor h=help");
}

STATIC VOID SpawnTerminal(VOID) {
  if (gWindowCount >= MAX_WINDOWS) return;
  TERMINAL_WINDOW *W = &gWindows[gWindowCount];
  SetMem(W, sizeof(*W), 0);
  W->Active = TRUE;
  W->Focused = TRUE;
  W->X = 120 + (gWindowCount * 24);
  W->Y = 80 + (gWindowCount * 24);
  W->W = 520;
  W->H = 280;
  AsciiSPrint(W->Title, sizeof(W->Title), "Terminal %d", (int)(gWindowCount + 1));
  AppendText(W, "GpuOS shell ready\nType: h(help) t(time) v(version) e(editor)");

  for (UINTN i=0;i<gWindowCount;i++) gWindows[i].Focused = FALSE;
  gWindowCount++;
}

STATIC VOID DrawWindow(GPU_FB *Fb, TERMINAL_WINDOW *W) {
  if (!W->Active) return;

  UINT32 Border = W->Focused ? 0xFF00FF88 : 0xFF506070;
  GfxFillRect(Fb, W->X, W->Y, W->W, W->H, 0xFF111111);
  GfxDrawRect(Fb, W->X, W->Y, W->W, W->H, Border);
  GfxFillRect(Fb, W->X, W->Y, W->W, TITLE_H, 0xFF1A1A1A);
  FontDrawString(Fb, W->X + 8, W->Y + 6, W->Title, 0xFFFFFFFF, 0xFF1A1A1A);

  GfxFillRect(Fb, W->X + W->W - 22, W->Y + 3, 18, 16, 0xFFAA2222);
  FontDrawString(Fb, W->X + W->W - 17, W->Y + 6, "X", 0xFFFFFFFF, 0xFFAA2222);

  UINT32 tx = W->X + 8;
  UINT32 ty = W->Y + 30;
  CHAR8 line[64];
  UINTN li = 0;
  for (UINTN i=0;i<W->BufferLen;i++) {
    if (W->Buffer[i] == '\n' || li >= 62) {
      line[li] = 0;
      FontDrawString(Fb, tx, ty, line, 0xFF00FF88, 0xFF111111);
      ty += 16;
      li = 0;
      if (W->Buffer[i] == '\n') continue;
    }
    line[li++] = W->Buffer[i];
  }
  line[li] = 0;
  FontDrawString(Fb, tx, ty, line, 0xFF00FF88, 0xFF111111);
}

STATIC VOID DrawDesktop(GPU_FB *Fb) {
  GfxGradientBackground(Fb, 0xFF0A0A12, 0xFF102040);

  GfxFillRect(Fb, 0, Fb->Height - TASKBAR_H, Fb->Width, TASKBAR_H, 0xFF080808);
  GfxFillRect(Fb, 10, Fb->Height - 26, 120, 20, 0xFF202020);
  GfxDrawRect(Fb, 10, Fb->Height - 26, 120, 20, 0xFF00FF88);
  FontDrawString(Fb, 26, Fb->Height - 21, "Terminal", 0xFFFFFFFF, 0xFF202020);

  for (UINTN i=0;i<gWindowCount;i++) DrawWindow(Fb, &gWindows[i]);

  GfxFillRect(Fb, gMouseX, gMouseY, 8, 14, 0xFFFFFFFF);
}

STATIC VOID HandleMouse(VOID) {
  if (!gMouse) return;

  EFI_SIMPLE_POINTER_STATE State;
  if (EFI_ERROR(gMouse->GetState(gMouse, &State))) return;

  gMouseX += State.RelativeMovementX / 8;
  gMouseY -= State.RelativeMovementY / 8;

  if (gMouseX < 0) gMouseX = 0;
  if (gMouseY < 0) gMouseY = 0;

  if (State.LeftButton) {
    if (gMouseY > 0) {
      if (gMouseX >= 10 && gMouseX <= 130) {
        SpawnTerminal();
      }

      for (INTN i=(INTN)gWindowCount-1;i>=0;i--) {
        TERMINAL_WINDOW *W = &gWindows[i];
        if (!W->Active) continue;

        if (gMouseX >= (INT32)(W->X + W->W - 22) &&
            gMouseX <= (INT32)(W->X + W->W - 4) &&
            gMouseY >= (INT32)(W->Y + 3) &&
            gMouseY <= (INT32)(W->Y + 19)) {
          W->Active = FALSE;
        }

        if (gMouseX >= (INT32)W->X && gMouseX <= (INT32)(W->X + W->W) &&
            gMouseY >= (INT32)W->Y && gMouseY <= (INT32)(W->Y + TITLE_H)) {
          for (UINTN k=0;k<gWindowCount;k++) gWindows[k].Focused = FALSE;
          W->Focused = TRUE;
          W->X = gMouseX - 40;
          W->Y = gMouseY - 10;
          break;
        }
      }
    }
  }
}

STATIC VOID HandleKeyboard(VOID) {
  EFI_INPUT_KEY Key;
  if (!gKeyboard) return;
  if (EFI_ERROR(gKeyboard->ReadKeyStroke(gKeyboard, &Key))) return;

  for (UINTN i=0;i<gWindowCount;i++) {
    TERMINAL_WINDOW *W = &gWindows[i];
    if (W->Focused && W->Active) {
      CHAR8 c = (CHAR8)Key.UnicodeChar;
      if (c >= 32 && c <= 126) {
        AppendText(W, "\n>");
        W->Buffer[W->BufferLen++] = c;
        W->Buffer[W->BufferLen] = 0;
        RunCommand(W, c);
      }
    }
  }
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  GPU_FB Fb;
  SetMem(&Fb, sizeof(Fb), 0);

  if (EFI_ERROR(GfxInit(gBS, &Fb))) return EFI_ABORTED;

  gKeyboard = SystemTable->ConIn;

  EFI_GUID MouseGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
  gBS->LocateProtocol(&MouseGuid, NULL, (VOID**)&gMouse);
  if (gMouse) gMouse->Reset(gMouse, TRUE);

  SpawnTerminal();

  while (1) {
    HandleMouse();
    HandleKeyboard();
    DrawDesktop(&Fb);
    gBS->Stall(16000);
  }

  return EFI_SUCCESS;
}
