
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

#define SPECIAL_POOL_PAGED_PTE    0x2000
#define SPECIAL_POOL_NONPAGED_PTE 0x4000
#define SPECIAL_POOL_PAGED        0x8000

PVOID MmSpecialPoolStart;
PVOID MmSpecialPoolEnd;
PVOID MiSpecialPoolExtra;
ULONG MiSpecialPoolExtraCount;

PMMPTE MiSpecialPoolFirstPte;
PMMPTE MiSpecialPoolLastPte;

PFN_COUNT MmSpecialPagesInUse;
PFN_COUNT MmSpecialPagesInUsePeak;
PFN_COUNT MiSpecialPagesPagable;
PFN_COUNT MiSpecialPagesPagablePeak;
PFN_COUNT MiSpecialPagesNonPaged;
PFN_COUNT MiSpecialPagesNonPagedPeak;
PFN_COUNT MiSpecialPagesNonPagedMaximum;

BOOLEAN MmSpecialPoolCatchOverruns = TRUE;

extern PFN_NUMBER MmAvailablePages;
extern PMMPTE MmSystemPteBase;
extern ULONG MmSecondaryColorMask;

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
MmIsSpecialPoolAddress(
    PVOID P)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

BOOLEAN
NTAPI
MmIsSpecialPoolAddressFree(
    PVOID P)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

NTSTATUS
NTAPI
MmExpandSpecialPool(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PVOID
NTAPI
MmAllocateSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG SpecialType)
{
    KIRQL Irql;
    MMPTE TempPte = ValidKernelPte;
    PMMPTE PointerPte;
    PFN_NUMBER PageFrameNumber;
    LARGE_INTEGER TickCount;
    PVOID Entry;
    PPOOL_HEADER Header;
    PFN_COUNT PagesInUse;

    DPRINT("MmAllocateSpecialPool(%x %x %x %x)\n", NumberOfBytes, Tag, PoolType, SpecialType);

    /* Check if the pool is initialized and quit if it's not */
    if (!MiSpecialPoolFirstPte)
        return NULL;

    /* Get the pool type */
    PoolType &= BASE_POOL_TYPE_MASK;

    /* Check whether current IRQL matches the pool type */
    Irql = KeGetCurrentIrql();

    if ((PoolType == PagedPool && Irql > APC_LEVEL) ||
        (PoolType != PagedPool && Irql > DISPATCH_LEVEL))
    {
        /* Bad caller */
        KeBugCheckEx(SPECIAL_POOL_DETECTED_MEMORY_CORRUPTION,
                     Irql,
                     PoolType,
                     NumberOfBytes,
                     0x30);
    }

    /* Some allocations from Mm must never use special pool */
    if (Tag == 'tSmM')
        /* Reject and let normal pool handle it */
        return NULL;

    /* TODO: Take into account various limitations */

    /* Heed the maximum limit of nonpaged pages */
    if (PoolType == NonPagedPool && MiSpecialPagesNonPaged > MiSpecialPagesNonPagedMaximum)
        return NULL;

    /* Lock PFN database */
    Irql = MiAcquirePfnLock();

    /* Reject allocation in case amount of available pages is too small */
    if (MmAvailablePages < 0x100)
    {
        /* Release the PFN database lock */
        MiReleasePfnLock(Irql);
        DPRINT1("Special pool: MmAvailablePages 0x%x is too small\n", MmAvailablePages);
        return NULL;
    }

    /* Check if special pool PTE list is exhausted */
    if (MiSpecialPoolFirstPte->u.List.NextEntry == MM_EMPTY_PTE_LIST)
    {
        /* Try to expand it */
        if (!NT_SUCCESS(MmExpandSpecialPool()))
        {
            /* No reserves left, reject this allocation */
            static int once;

            MiReleasePfnLock(Irql);

            if (!once++)
            {
                DPRINT1("Special pool: No PTEs left!\n");
            }

            return NULL;
        }

        ASSERT(MiSpecialPoolFirstPte->u.List.NextEntry != MM_EMPTY_PTE_LIST);
    }

    /* Save allocation time */
    KeQueryTickCount(&TickCount);

    /* Get a pointer to the first PTE */
    PointerPte = MiSpecialPoolFirstPte;

    /* Set the first PTE pointer to the next one in the list */
    MiSpecialPoolFirstPte = (MmSystemPteBase + PointerPte->u.List.NextEntry);

    /* Allocate a physical page */
    if (PoolType == PagedPool)
        MI_SET_USAGE(MI_USAGE_PAGED_POOL);
    else
        MI_SET_USAGE(MI_USAGE_NONPAGED_POOL);

    MI_SET_PROCESS2("Kernel-Special");
    PageFrameNumber = MiRemoveAnyPage(MI_GET_NEXT_COLOR());

    /* Initialize PFN and make it valid */
    TempPte.u.Hard.PageFrameNumber = PageFrameNumber;
    MiInitializePfnAndMakePteValid(PageFrameNumber, PointerPte, TempPte);

    /* Release the PFN database lock */
    MiReleasePfnLock(Irql);

    /* Increase page counter */
    PagesInUse = InterlockedIncrementUL(&MmSpecialPagesInUse);
    if (PagesInUse > MmSpecialPagesInUsePeak)
        MmSpecialPagesInUsePeak = PagesInUse;

    /* Put some content into the page. Low value of tick count would do */
    Entry = MiPteToAddress(PointerPte);
    RtlFillMemory(Entry, PAGE_SIZE, TickCount.LowPart);

    /* Calculate header and entry addresses */
    if (SpecialType != 0 &&
        (SpecialType == 1 || !MmSpecialPoolCatchOverruns))
    {
        /* We catch underruns. Data is at the beginning of the page */
        Header = (PPOOL_HEADER)((PUCHAR)Entry + PAGE_SIZE - sizeof(POOL_HEADER));
    }
    else
    {
        /* We catch overruns. Data is at the end of the page */
        Header = (PPOOL_HEADER)Entry;
        Entry = (PVOID)((ULONG_PTR)((PUCHAR)Entry - NumberOfBytes + PAGE_SIZE) & ~((LONG_PTR)sizeof(POOL_HEADER) - 1));
    }

    /* Initialize the header */
    RtlZeroMemory(Header, sizeof(POOL_HEADER));

    /* Save allocation size there */
    Header->Ulong1 = (ULONG)NumberOfBytes;

    /* Make sure it's all good */
    ASSERT((NumberOfBytes <= PAGE_SIZE - sizeof(POOL_HEADER)) && (PAGE_SIZE <= (32 * _1KB)));

    /* Mark it as paged or nonpaged */
    if (PoolType == PagedPool)
    {
        /* Add pagedpool flag into the pool header too */
        Header->Ulong1 |= SPECIAL_POOL_PAGED;

        /* Also mark the next PTE as special-pool-paged */
        PointerPte[1].u.Soft.PageFileHigh |= SPECIAL_POOL_PAGED_PTE;

        /* Increase pagable counter */
        PagesInUse = InterlockedIncrementUL(&MiSpecialPagesPagable);
        if (PagesInUse > MiSpecialPagesPagablePeak)
            MiSpecialPagesPagablePeak = PagesInUse;
    }
    else
    {
        /* Mark the next PTE as special-pool-nonpaged */
        PointerPte[1].u.Soft.PageFileHigh |= SPECIAL_POOL_NONPAGED_PTE;

        /* Increase nonpaged counter */
        PagesInUse = InterlockedIncrementUL(&MiSpecialPagesNonPaged);
        if (PagesInUse > MiSpecialPagesNonPagedPeak)
            MiSpecialPagesNonPagedPeak = PagesInUse;
    }

    /* Finally save tag and put allocation time into the header's blocksize.
       That time will be used to check memory consistency within the allocated page.
    */
    Header->PoolTag = Tag;
    Header->BlockSize = (UCHAR)TickCount.LowPart;

    DPRINT("%p\n", Entry);
    return Entry;
}

/* EOF */
