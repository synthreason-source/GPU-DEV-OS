/**
 * PciGpu.c — AMD GPU detection and BAR0 MMIO mapping for bare-metal compute
 * Maps GPU MMIO register space before ExitBootServices.
 * After ExitBootServices, writes PM4 command packets directly to shader dispatch.
 */
#include "PciGpu.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <IndustryStandard/Pci.h>

/* Global GPU MMIO base — valid after FindAndMapGpu(), used post-ExitBootServices */
volatile UINT32 *GGpuMmio     = NULL;
UINT64           GGpuBar0Base  = 0;
UINT64           GGpuBar0Size  = 0;
UINT16           GGpuVendorId  = 0;
UINT16           GGpuDeviceId  = 0;

/* AMD GPU PCIe Vendor ID */
#define AMD_VENDOR_ID   0x1002
#define NVIDIA_VENDOR_ID 0x10DE

/* Known AMD RDNA 3 Device IDs (RX 7000 series) */
static const UINT16 KnownAmdRdna3[] = {
    0x744C, /* RX 7900 XTX */
    0x7480, /* RX 7900 XT  */
    0x7483, /* RX 7900 GRE */
    0x7471, /* RX 7800 XT  */
    0x747E, /* RX 7700 XT  */
    0      /* sentinel     */
};

/* Known AMD RDNA 2 Device IDs (RX 6000 series) */
static const UINT16 KnownAmdRdna2[] = {
    0x73AF, /* RX 6900 XT  */
    0x73BF, /* RX 6800 XT  */
    0x73A5, /* RX 6800      */
    0x73DF, /* RX 6700 XT  */
    0x73FF, /* RX 6600 XT  */
    0      /* sentinel     */
};

STATIC BOOLEAN IsKnownAmdDevice(UINT16 DeviceId) {
    for (UINT32 i = 0; KnownAmdRdna3[i]; i++)
        if (KnownAmdRdna3[i] == DeviceId) return TRUE;
    for (UINT32 i = 0; KnownAmdRdna2[i]; i++)
        if (KnownAmdRdna2[i] == DeviceId) return TRUE;
    return FALSE;
}

EFI_STATUS FindAndMapGpu(EFI_BOOT_SERVICES *BS, GPU_INFO *Info) {
    EFI_GUID PciIoGuid = EFI_PCI_IO_PROTOCOL_GUID;
    UINTN     HandleCount = 0;
    EFI_HANDLE *Handles   = NULL;

    EFI_STATUS Status = BS->LocateHandleBuffer(
        ByProtocol, &PciIoGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return EFI_NOT_FOUND;

    for (UINTN i = 0; i < HandleCount; i++) {
        EFI_PCI_IO_PROTOCOL *PciIo;
        Status = BS->HandleProtocol(Handles[i], &PciIoGuid, (VOID**)&PciIo);
        if (EFI_ERROR(Status)) continue;

        UINT16 VendorId = 0, DeviceId = 0;
        PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16,
                        PCI_VENDOR_ID_OFFSET, 1, &VendorId);
        PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16,
                        PCI_DEVICE_ID_OFFSET,  1, &DeviceId);

        /* Found AMD GPU — any AMD VEN ID qualifies; known IDs are preferred */
        if (VendorId == AMD_VENDOR_ID) {
            /* Read class code to confirm display controller */
            UINT8 ClassCode[3];
            PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8,
                            PCI_CLASSCODE_OFFSET, 3, ClassCode);
            /* Class 0x03 = Display controller */
            if (ClassCode[2] != 0x03) continue;

            /* Get BAR0 — GPU MMIO register space */
            UINT64 Bar0Base = 0, Bar0Len = 0;
            UINT8  Bar0Width = 0;
            EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Desc = NULL;

            Status = PciIo->GetBarAttributes(PciIo, 0, NULL, (VOID**)&Desc);
            if (!EFI_ERROR(Status) && Desc != NULL) {
                Bar0Base = Desc->AddrRangeMin;
                Bar0Len  = Desc->AddrLen;
                BS->FreePool(Desc);
            }

            /* Store results */
            Info->VendorId   = VendorId;
            Info->DeviceId   = DeviceId;
            Info->Bar0Base   = Bar0Base;
            Info->Bar0Size   = Bar0Len;
            Info->IsKnownRdna = IsKnownAmdDevice(DeviceId);

            /* Map BAR0 to a CPU-accessible address */
            VOID *MappedBar0 = (VOID*)(UINTN)Bar0Base;
            Info->MmioBase   = (volatile UINT32*)MappedBar0;

            /* Cache in globals for post-ExitBootServices use */
            GGpuMmio    = Info->MmioBase;
            GGpuBar0Base = Bar0Base;
            GGpuBar0Size = Bar0Len;
            GGpuVendorId = VendorId;
            GGpuDeviceId = DeviceId;

            BS->FreePool(Handles);
            return EFI_SUCCESS;
        }
    }

    BS->FreePool(Handles);
    return EFI_NOT_FOUND;
}

/**
 * GpuMmioRead32 / GpuMmioWrite32
 * Safe MMIO register accessors for post-ExitBootServices bare-metal use.
 * These compile to a single 32-bit memory-mapped I/O read/write.
 */
UINT32 GpuMmioRead32(UINT32 RegOffset) {
    if (!GGpuMmio) return 0xFFFFFFFF;
    return GGpuMmio[RegOffset / 4];
}

VOID GpuMmioWrite32(UINT32 RegOffset, UINT32 Value) {
    if (!GGpuMmio) return;
    GGpuMmio[RegOffset / 4] = Value;
}

/**
 * GpuWaitIdle — Poll GPU status register until idle.
 * AMD CP_STAT register: bit 31 = busy when set.
 * Timeout prevents infinite hang on unresponsive hardware.
 */
BOOLEAN GpuWaitIdle(UINT32 TimeoutMs) {
    if (!GGpuMmio) return FALSE;
    /* AMD CP_STAT MMIO offset — varies by ASIC; RDNA3 default: */
    #define AMD_CP_STAT_OFFSET  0x8210
    #define AMD_CP_STAT_BUSY    (1U << 31)

    UINT64 Timeout = (UINT64)TimeoutMs * 1000000ULL; /* rough cycle count */
    for (UINT64 t = 0; t < Timeout; t++) {
        UINT32 Stat = GpuMmioRead32(AMD_CP_STAT_OFFSET);
        if (!(Stat & AMD_CP_STAT_BUSY)) return TRUE;
        /* Tiny delay */
        for (volatile UINT32 d = 0; d < 100; d++) {}
    }
    return FALSE; /* timed out */
}
