/**
 * PciGpu.c - AMD GPU detection and BAR0 MMIO mapping for bare-metal compute
 */
#include "PciGpu.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <IndustryStandard/Pci.h>

volatile UINT32 *GGpuMmio    = NULL;
UINT64           GGpuBar0Base = 0;
UINT64           GGpuBar0Size = 0;
UINT16           GGpuVendorId = 0;
UINT16           GGpuDeviceId = 0;

#define AMD_VENDOR_ID 0x1002

static const UINT16 KnownAmdRdna3[] = {
    0x744C,0x7480,0x7483,0x7471,0x747E,0
};
static const UINT16 KnownAmdRdna2[] = {
    0x73AF,0x73BF,0x73A5,0x73DF,0x73FF,0
};

STATIC BOOLEAN IsKnownAmdDevice(UINT16 DeviceId) {
    for (UINT32 i=0; KnownAmdRdna3[i]; i++) if(KnownAmdRdna3[i]==DeviceId) return TRUE;
    for (UINT32 i=0; KnownAmdRdna2[i]; i++) if(KnownAmdRdna2[i]==DeviceId) return TRUE;
    return FALSE;
}

EFI_STATUS FindAndMapGpu(EFI_BOOT_SERVICES *BS, GPU_INFO *Info) {
    EFI_GUID PciIoGuid = EFI_PCI_IO_PROTOCOL_GUID;
    UINTN HandleCount=0; EFI_HANDLE *Handles=NULL;

    EFI_STATUS Status = BS->LocateHandleBuffer(
        ByProtocol, &PciIoGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status)||HandleCount==0) return EFI_NOT_FOUND;

    for (UINTN i=0; i<HandleCount; i++) {
        EFI_PCI_IO_PROTOCOL *PciIo;
        Status=BS->HandleProtocol(Handles[i],&PciIoGuid,(VOID**)&PciIo);
        if (EFI_ERROR(Status)) continue;

        UINT16 VendorId=0,DeviceId=0;
        PciIo->Pci.Read(PciIo,EfiPciIoWidthUint16,PCI_VENDOR_ID_OFFSET,1,&VendorId);
        PciIo->Pci.Read(PciIo,EfiPciIoWidthUint16,PCI_DEVICE_ID_OFFSET, 1,&DeviceId);

        if (VendorId==AMD_VENDOR_ID) {
            UINT8 ClassCode[3];
            PciIo->Pci.Read(PciIo,EfiPciIoWidthUint8,PCI_CLASSCODE_OFFSET,3,ClassCode);
            if (ClassCode[2]!=0x03) continue;

            UINT64 Bar0Base=0,Bar0Len=0;
            EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Desc=NULL;
            Status=PciIo->GetBarAttributes(PciIo,0,NULL,(VOID**)&Desc);
            if (!EFI_ERROR(Status)&&Desc!=NULL) {
                Bar0Base=Desc->AddrRangeMin; Bar0Len=Desc->AddrLen;
                BS->FreePool(Desc);
            }

            Info->VendorId    = VendorId;
            Info->DeviceId    = DeviceId;
            Info->Bar0Base    = Bar0Base;
            Info->Bar0Size    = Bar0Len;
            Info->IsKnownRdna = IsKnownAmdDevice(DeviceId);
            Info->MmioBase    = (volatile UINT32*)(UINTN)Bar0Base;

            GGpuMmio     = Info->MmioBase;
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

UINT32 GpuMmioRead32(UINT32 RegOffset) {
    if (!GGpuMmio) return 0xFFFFFFFF;
    return GGpuMmio[RegOffset/4];
}

VOID GpuMmioWrite32(UINT32 RegOffset, UINT32 Value) {
    if (!GGpuMmio) return;
    GGpuMmio[RegOffset/4]=Value;
}

BOOLEAN GpuWaitIdle(UINT32 TimeoutMs) {
    if (!GGpuMmio) return FALSE;
    #define AMD_CP_STAT_OFFSET 0x8210
    #define AMD_CP_STAT_BUSY   (1U<<31)
    UINT64 Timeout=(UINT64)TimeoutMs*1000000ULL;
    for (UINT64 t=0; t<Timeout; t++) {
        if (!(GpuMmioRead32(AMD_CP_STAT_OFFSET)&AMD_CP_STAT_BUSY)) return TRUE;
        for (volatile UINT32 d=0; d<100; d++) {}
    }
    return FALSE;
}
