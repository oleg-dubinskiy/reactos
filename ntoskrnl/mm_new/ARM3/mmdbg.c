
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

PVOID MiDebugMapping = MI_DEBUG_MAPPING;
PMMPTE MmDebugPte = NULL;

/* FUNCTIONS ******************************************************************/

PVOID
NTAPI
MiDbgTranslatePhysicalAddress(
    _In_ ULONG64 PhysicalAddress,
    _In_ ULONG Flags)
{
    PFN_NUMBER Pfn;
    MMPTE TempPte;
    PVOID MappingBaseAddress;

    /* Check if we are called too early */
    if (MmDebugPte == NULL)
    {
        /* The structures we require aren't initialized yet, fail */
        KdpDprintf("MiDbgTranslatePhysicalAddress called too early! Address: 0x%I64x\n", PhysicalAddress);
        return NULL;
    }

    /* FIXME: No support for cache flags yet */
    if ((Flags & (MMDBG_COPY_CACHED | MMDBG_COPY_UNCACHED | MMDBG_COPY_WRITE_COMBINED)) != 0)
    {
        /* Fail */
        KdpDprintf("MiDbgTranslatePhysicalAddress: Cache flags not yet supported. Flags: 0x%lx\n",
                   (Flags & (MMDBG_COPY_CACHED | MMDBG_COPY_UNCACHED | MMDBG_COPY_WRITE_COMBINED)));
        return NULL;
    }

    /* Save the base address of our mapping page */
    MappingBaseAddress = MiPteToAddress(MmDebugPte);

    /* Get the template */
    TempPte = ValidKernelPte;

    /* Convert physical address to PFN */
    Pfn = (PFN_NUMBER)(PhysicalAddress >> PAGE_SHIFT);

    /* Check if this could be an I/O mapping */
    if (!MiGetPfnEntry(Pfn))
    {
        /* FIXME: We don't support this yet */
        KdpDprintf("MiDbgTranslatePhysicalAddress: I/O Space not yet supported. PFN: 0x%I64x\n", (ULONG64)Pfn);
        return NULL;
    }
    else
    {
        /* Set the PFN in the PTE */
        TempPte.u.Hard.PageFrameNumber = Pfn;
    }

    /* Map the PTE and invalidate its TLB entry */
    *MmDebugPte = TempPte;
    KeInvalidateTlbEntry(MappingBaseAddress);

    /* Calculate and return the virtual offset into our mapping page */
    return (PVOID)((ULONG_PTR)MappingBaseAddress + BYTE_OFFSET(PhysicalAddress));
}

NTSTATUS
NTAPI
MmDbgCopyMemory(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _In_ ULONG Flags)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
