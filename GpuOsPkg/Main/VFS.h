/**
 * VFS.h — Bare-metal virtual filesystem for GpuOS
 * Provides a simple RAM-based filesystem that persists for the session.
 * Files are stored in EfiRuntimeServicesData memory (survives ExitBootServices).
 *
 * Design: flat file list, each file is a contiguous RAM buffer.
 * No directories beyond a single root namespace (sufficient for a hobby OS).
 */
#ifndef VFS_H
#define VFS_H

#include <Uefi.h>

/* ── Limits ──────────────────────────────────────────────────────── */
#define VFS_MAX_FILES       64
#define VFS_MAX_NAME        64          /* bytes including NUL         */
#define VFS_MAX_FILE_SIZE   (256*1024)  /* 256 KiB per file            */
#define VFS_MAGIC           0x47504653  /* 'GPFS' */

/* ── File entry ──────────────────────────────────────────────────── */
typedef struct {
    UINT32  Magic;
    CHAR8   Name[VFS_MAX_NAME];
    UINT8  *Data;       /* Pointer into allocated pool */
    UINT32  Size;       /* Used bytes                  */
    UINT32  Capacity;   /* Allocated bytes             */
    BOOLEAN InUse;
} VFS_FILE;

/* ── Filesystem state ────────────────────────────────────────────── */
typedef struct {
    VFS_FILE Files[VFS_MAX_FILES];
    UINT32   Count;     /* number of InUse entries     */
    EFI_BOOT_SERVICES *BS; /* stashed for AllocatePool */
} VFS;

/* Global filesystem instance — valid after VfsInit() */
extern VFS *GVfs;

/**
 * VfsInit — Initialise the virtual filesystem.
 * Must be called before ExitBootServices (needs AllocatePool).
 * Allocates the VFS control structure itself from pool.
 * @param BS  Boot services table
 * @return EFI_SUCCESS or allocation failure
 */
EFI_STATUS VfsInit(EFI_BOOT_SERVICES *BS);

/**
 * VfsCreate — Create a new empty file, or return existing entry.
 * @param Name  NUL-terminated filename (max VFS_MAX_NAME-1 chars)
 * @return pointer to VFS_FILE, or NULL on failure
 */
VFS_FILE *VfsCreate(const CHAR8 *Name);

/**
 * VfsOpen — Find an existing file by name.
 * @return pointer to VFS_FILE, or NULL if not found
 */
VFS_FILE *VfsOpen(const CHAR8 *Name);

/**
 * VfsWrite — Overwrite a file's content with new data.
 * Grows the buffer if needed (up to VFS_MAX_FILE_SIZE).
 * @param File   File entry from VfsCreate/VfsOpen
 * @param Data   Source buffer
 * @param Size   Byte count
 * @return EFI_SUCCESS, EFI_OUT_OF_RESOURCES, or EFI_BUFFER_TOO_SMALL
 */
EFI_STATUS VfsWrite(VFS_FILE *File, const UINT8 *Data, UINT32 Size);

/**
 * VfsAppend — Append data to an existing file.
 */
EFI_STATUS VfsAppend(VFS_FILE *File, const UINT8 *Data, UINT32 Size);

/**
 * VfsDelete — Mark a file slot as free.
 */
VOID VfsDelete(VFS_FILE *File);

/**
 * VfsListFiles — Fill an array of name pointers for all live files.
 * @param Out      Array of CHAR8* to populate
 * @param MaxOut   Capacity of Out[]
 * @return number of entries written
 */
UINT32 VfsListFiles(const CHAR8 **Out, UINT32 MaxOut);

#endif /* VFS_H */
