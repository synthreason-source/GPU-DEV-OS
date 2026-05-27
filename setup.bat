/**
 * PciGpu.h — AMD GPU PCI detection and MMIO register access
 */
#ifndef PCIGPU_H
#define PCIGPU_H

#include <Uefi.h>
#include <Protocol/PciIo.h>

typedef struct {
    UINT16             VendorId;
    UINT16             DeviceId;
    UINT64             Bar0Base;   /* Physical address of GPU MMIO registers */
    UINT64             Bar0Size;
    volatile UINT32   *MmioBase;   /* Mapped CPU-accessible pointer to BAR0 */
    BOOLEAN            IsKnownRdna;
} GPU_INFO;

/* Globals — populated by FindAndMapGpu(), valid post-ExitBootServices */
extern volatile UINT32 *GGpuMmio;
extern UINT64           GGpuBar0Base;
extern UINT64           GGpuBar0Size;
extern UINT16           GGpuVendorId;
extern UINT16           GGpuDeviceId;

/**
 * FindAndMapGpu — Scan PCI bus, find AMD GPU, map BAR0.
 * Must be called BEFORE ExitBootServices.
 */
EFI_STATUS FindAndMapGpu(EFI_BOOT_SERVICES *BS, GPU_INFO *Info);

/** Direct MMIO register read (32-bit, byte offset) */
UINT32 GpuMmioRead32(UINT32 RegOffset);

/** Direct MMIO register write (32-bit, byte offset) */
VOID   GpuMmioWrite32(UINT32 RegOffset, UINT32 Value);

/** Poll GPU until idle. Returns TRUE if idle, FALSE on timeout. */
BOOLEAN GpuWaitIdle(UINT32 TimeoutMs);

/* ------------------------------------------------------------------ */
/* AMD PM4 Command Packet helpers (post-ExitBootServices compute use)  */
/* ------------------------------------------------------------------ */

/* PM4 Type-3 packet header: OPCODE in bits [15:8], COUNT in [29:16] */
#define PM4_HDR_T3(opcode, count) \
    (0xC0000000 | (((count)-1) << 16) | ((opcode) << 8))

/* Opcodes (partial — RDNA3 PM4 reference) */
#define PM4_NOP                 0x10
#define PM4_SET_UCONFIG_REG     0x79
#define PM4_DISPATCH_DIRECT     0x15
#define PM4_RELEASE_MEM         0x28

/* Ring buffer write helpers — call after ExitBootServices
 * In a real implementation, GpuSubmitRing() would DMA the ring
 * and ring the GPU doorbell register.
 * See: linux/drivers/gpu/drm/amd/amdgpu/amdgpu_ring.c  */

#endif /* PCIGPU_H */
