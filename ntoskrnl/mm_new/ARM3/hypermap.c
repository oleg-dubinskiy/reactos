
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

PVOID
NTAPI
MiMapPagesInZeroSpace(
    _In_ PMMPFN Pfn,
    _In_ PFN_NUMBER NumberOfPages)
{
    PMMPTE Pte;
    MMPTE TempPte;
    PFN_NUMBER Offset;
    PFN_NUMBER PageFrameIndex;

    /* Sanity checks */
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(NumberOfPages != 0);
    ASSERT(NumberOfPages <= (MI_ZERO_PTES - 1));

    /* Pick the first zeroing PTE */
    Pte = MiFirstReservedZeroingPte;

    /* Now get the first free PTE */
    Offset = PFN_FROM_PTE(Pte);

    if (NumberOfPages > Offset)
    {
        /* Reset the PTEs */
        Offset = (MI_ZERO_PTES - 1);
        Pte->u.Hard.PageFrameNumber = Offset;

        KeFlushProcessTb();
    }

    /* Prepare the next PTE */
    Pte->u.Hard.PageFrameNumber = (Offset - NumberOfPages);

    /* Choose the correct PTE to use, and which template */
    Pte += (Offset + 1);
    TempPte = ValidKernelPte;

    /* Make sure the list isn't empty and loop it */
    ASSERT(Pfn != (PVOID)LIST_HEAD);

    while (Pfn != (PVOID)LIST_HEAD)
    {
        /* Get the page index for this PFN */
        PageFrameIndex = MiGetPfnEntryIndex(Pfn);

        /* Write the PFN */
        TempPte.u.Hard.PageFrameNumber = PageFrameIndex;

        /* Set the correct PTE to write to, and set its new value */
        Pte--;
        MI_WRITE_VALID_PTE(Pte, TempPte);

        /* Move to the next PFN */
        Pfn = (PMMPFN)Pfn->u1.Flink;
    }

    /* Return the address */
    return MiPteToAddress(Pte);
}

VOID
NTAPI
MiUnmapPagesInZeroSpace(
    _In_ PVOID VirtualAddress,
    _In_ PFN_NUMBER NumberOfPages)
{
    PMMPTE Pte;

    /* Sanity checks */
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT (NumberOfPages != 0);
    ASSERT (NumberOfPages <= (MI_ZERO_PTES - 1));

    /* Get the first PTE for the mapped zero VA */
    Pte = MiAddressToPte(VirtualAddress);

    /* Blow away the mapped zero PTEs */
    RtlZeroMemory(Pte, (NumberOfPages * sizeof(MMPTE)));
}

/* EOF */
