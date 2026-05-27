/**
 * VFS.c — Bare-metal RAM virtual filesystem implementation
 * Simple flat namespace, pool-allocated file buffers.
 * Designed for use before AND after ExitBootServices.
 *
 * Post-ExitBootServices: AllocatePool is unavailable, so all allocation
 * must happen before that boundary. VfsInit() and VfsCreate() MUST be
 * called before ExitBootServices. After that, VfsWrite/VfsAppend/VfsOpen
 * work fine using already-allocated buffers.
 */
#include "VFS.h"
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

/* Global filesystem */
VFS *GVfs = NULL;

/* ── Internal helpers ────────────────────────────────────────────── */

STATIC VOID VfsStrCopy(CHAR8 *Dst, const CHAR8 *Src, UINTN MaxLen) {
    UINTN i = 0;
    while (i + 1 < MaxLen && Src[i]) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = '\0';
}

STATIC BOOLEAN VfsStrEq(const CHAR8 *A, const CHAR8 *B) {
    while (*A && *B) {
        if (*A != *B) return FALSE;
        A++; B++;
    }
    return (*A == '\0' && *B == '\0');
}

/* ── Public API ──────────────────────────────────────────────────── */

EFI_STATUS VfsInit(EFI_BOOT_SERVICES *BS) {
    EFI_STATUS Status;

    Status = BS->AllocatePool(EfiRuntimeServicesData,
                              sizeof(VFS),
                              (VOID**)&GVfs);
    if (EFI_ERROR(Status)) return Status;

    SetMem(GVfs, sizeof(VFS), 0);
    GVfs->BS    = BS;
    GVfs->Count = 0;

    return EFI_SUCCESS;
}

VFS_FILE *VfsCreate(const CHAR8 *Name) {
    if (!GVfs || !Name) return NULL;

    /* Return existing file if name matches */
    for (UINT32 i = 0; i < VFS_MAX_FILES; i++) {
        if (GVfs->Files[i].InUse && VfsStrEq(GVfs->Files[i].Name, Name))
            return &GVfs->Files[i];
    }

    /* Find a free slot */
    VFS_FILE *Slot = NULL;
    for (UINT32 i = 0; i < VFS_MAX_FILES; i++) {
        if (!GVfs->Files[i].InUse) {
            Slot = &GVfs->Files[i];
            break;
        }
    }
    if (!Slot) return NULL; /* filesystem full */

    /* Allocate initial buffer (4 KiB) */
    UINT32 InitCap = 4096;
    EFI_STATUS Status = GVfs->BS->AllocatePool(EfiRuntimeServicesData,
                                               InitCap,
                                               (VOID**)&Slot->Data);
    if (EFI_ERROR(Status)) return NULL;

    Slot->Magic    = VFS_MAGIC;
    Slot->Capacity = InitCap;
    Slot->Size     = 0;
    Slot->InUse    = TRUE;
    VfsStrCopy(Slot->Name, Name, VFS_MAX_NAME);
    SetMem(Slot->Data, InitCap, 0);

    GVfs->Count++;
    return Slot;
}

VFS_FILE *VfsOpen(const CHAR8 *Name) {
    if (!GVfs || !Name) return NULL;
    for (UINT32 i = 0; i < VFS_MAX_FILES; i++) {
        if (GVfs->Files[i].InUse && VfsStrEq(GVfs->Files[i].Name, Name))
            return &GVfs->Files[i];
    }
    return NULL;
}

/* Grow or reuse an existing buffer. Called before ExitBootServices only. */
STATIC EFI_STATUS VfsEnsureCapacity(VFS_FILE *File, UINT32 Needed) {
    if (Needed <= File->Capacity) return EFI_SUCCESS;
    if (Needed > VFS_MAX_FILE_SIZE) return EFI_BUFFER_TOO_SMALL;
    if (!GVfs || !GVfs->BS) return EFI_NOT_READY;

    /* Round up to next 4 KiB boundary */
    UINT32 NewCap = (Needed + 4095) & ~4095U;
    if (NewCap > VFS_MAX_FILE_SIZE) NewCap = VFS_MAX_FILE_SIZE;

    UINT8 *NewBuf = NULL;
    EFI_STATUS Status = GVfs->BS->AllocatePool(EfiRuntimeServicesData,
                                               NewCap,
                                               (VOID**)&NewBuf);
    if (EFI_ERROR(Status)) return EFI_OUT_OF_RESOURCES;

    /* Copy old content, free old buffer */
    CopyMem(NewBuf, File->Data, File->Size);
    GVfs->BS->FreePool(File->Data);

    File->Data     = NewBuf;
    File->Capacity = NewCap;
    return EFI_SUCCESS;
}

EFI_STATUS VfsWrite(VFS_FILE *File, const UINT8 *Data, UINT32 Size) {
    if (!File || !File->InUse) return EFI_INVALID_PARAMETER;
    EFI_STATUS Status = VfsEnsureCapacity(File, Size);
    if (EFI_ERROR(Status)) return Status;
    CopyMem(File->Data, Data, Size);
    File->Size = Size;
    return EFI_SUCCESS;
}

EFI_STATUS VfsAppend(VFS_FILE *File, const UINT8 *Data, UINT32 Size) {
    if (!File || !File->InUse) return EFI_INVALID_PARAMETER;
    UINT32 NewSize = File->Size + Size;
    EFI_STATUS Status = VfsEnsureCapacity(File, NewSize);
    if (EFI_ERROR(Status)) return Status;
    CopyMem(File->Data + File->Size, Data, Size);
    File->Size = NewSize;
    return EFI_SUCCESS;
}

VOID VfsDelete(VFS_FILE *File) {
    if (!File || !File->InUse) return;
    /* We can only free pool before ExitBootServices, so just mark as free */
    if (GVfs && GVfs->BS && File->Data)
        GVfs->BS->FreePool(File->Data);
    SetMem(File, sizeof(VFS_FILE), 0);
    if (GVfs->Count > 0) GVfs->Count--;
}

UINT32 VfsListFiles(const CHAR8 **Out, UINT32 MaxOut) {
    if (!GVfs || !Out) return 0;
    UINT32 n = 0;
    for (UINT32 i = 0; i < VFS_MAX_FILES && n < MaxOut; i++) {
        if (GVfs->Files[i].InUse)
            Out[n++] = GVfs->Files[i].Name;
    }
    return n;
}
