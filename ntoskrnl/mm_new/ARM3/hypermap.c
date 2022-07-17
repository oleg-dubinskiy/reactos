
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

PMMPTE MmFirstReservedMappingPte;
PMMPTE MmLastReservedMappingPte;
PMMPTE MiFirstReservedZeroingPte;

extern MMPTE ValidKernelPteLocal;

/* FUNCTIONS ******************************************************************/

PVOID
NTAPI
MiMapPageInHyperSpace(
    _In_ PEPROCESS Process,
    _In_ PFN_NUMBER Page,
    _In_ PKIRQL OldIrql)
{
    MMPTE TempPte;
    PMMPTE Pte;
    PFN_NUMBER Offset;

    /* Never accept page 0 or non-physical pages */
    ASSERT(Page != 0);
    ASSERT(MiGetPfnEntry(Page) != NULL);

    /* Build the PTE */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = Page;

    /* Pick the first hyperspace PTE */
    Pte = MmFirstReservedMappingPte;

    /* Acquire the hyperlock */
    ASSERT(Process == PsGetCurrentProcess());
    KeAcquireSpinLock(&Process->HyperSpaceLock, OldIrql);

    /* Now get the first free PTE */
    Offset = PFN_FROM_PTE(Pte);
    if (!Offset)
    {
        /* Reset the PTEs */
        Offset = MI_HYPERSPACE_PTES;
        KeFlushProcessTb();
    }

    /* Prepare the next PTE */
    Pte->u.Hard.PageFrameNumber = Offset - 1;

    /* Write the current PTE */
    Pte += Offset;
    MI_WRITE_VALID_PTE(Pte, TempPte);

    /* Return the address */
    return MiPteToAddress(Pte);
}

VOID
NTAPI
MiUnmapPageInHyperSpace(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ KIRQL OldIrql)
{
    ASSERT(Process == PsGetCurrentProcess());

    /* Blow away the mapping */
    MiAddressToPte(Address)->u.Long = 0;

    /* Release the hyperlock */
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    KeReleaseSpinLock(&Process->HyperSpaceLock, OldIrql);
}
/* EOF */
