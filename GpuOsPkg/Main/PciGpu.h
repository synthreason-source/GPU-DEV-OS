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
    UINT64             Bar0Base;
    UINT64             Bar0Size;
    volatile UINT32   *MmioBase;
    BOOLEAN            IsKnownRdna;
} GPU_INFO;

extern volatile UINT32 *GGpuMmio;
extern UINT64           GGpuBar0Base;
extern UINT64           GGpuBar0Size;
extern UINT16           GGpuVendorId;
extern UINT16           GGpuDeviceId;

EFI_STATUS FindAndMapGpu(EFI_BOOT_SERVICES *BS, GPU_INFO *Info);
UINT32     GpuMmioRead32(UINT32 RegOffset);
VOID       GpuMmioWrite32(UINT32 RegOffset, UINT32 Value);
BOOLEAN    GpuWaitIdle(UINT32 TimeoutMs);

#define PM4_HDR_T3(opcode, count) \
    (0xC0000000 | (((count)-1) << 16) | ((opcode) << 8))
#define PM4_NOP             0x10
#define PM4_SET_UCONFIG_REG 0x79
#define PM4_DISPATCH_DIRECT 0x15
#define PM4_RELEASE_MEM     0x28

#endif /* PCIGPU_H */
