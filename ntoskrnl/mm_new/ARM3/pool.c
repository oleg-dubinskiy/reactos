
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

MM_PAGED_POOL_INFO MmPagedPoolInfo;
KGUARDED_MUTEX MmPagedPoolMutex;
SIZE_T MmAllocatedNonPagedPool;
ULONG MmSpecialPoolTag;
ULONG MmConsumedPoolPercentage;
LIST_ENTRY MmNonPagedPoolFreeListHead[MI_MAX_FREE_PAGE_LISTS];
PFN_COUNT MmNumberOfFreeNonPagedPool;
PFN_COUNT MiExpansionPoolPagesInitialCharge;
PVOID MmNonPagedPoolEnd0;
PFN_NUMBER MiStartOfInitialPoolFrame;
PFN_NUMBER MiEndOfInitialPoolFrame;
SLIST_HEADER MiNonPagedPoolSListHead;
ULONG MiNonPagedPoolSListMaximum = 4;
SLIST_HEADER MiPagedPoolSListHead;
ULONG MiPagedPoolSListMaximum = 8;
BOOLEAN MmProtectFreedNonPagedPool;

extern PVOID MmNonPagedPoolStart;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern PVOID MmNonPagedPoolExpansionStart;
extern SIZE_T MmMaximumNonPagedPoolInBytes;
extern PFN_NUMBER MmMaximumNonPagedPoolInPages;
extern PFN_NUMBER MiLowNonPagedPoolThreshold;
extern PFN_NUMBER MiHighNonPagedPoolThreshold;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MiInitializeNonPagedPool(VOID)
{
    PMMFREE_POOL_ENTRY FreeEntry;
    PMMFREE_POOL_ENTRY FirstEntry;
    PFN_COUNT PoolPages;
    PMMPTE Pte;
    ULONG ix;

    PAGED_CODE();

    /* Initialize the pool S-LISTs as well as their maximum count.
       In general, we'll allow 8 times the default on a 2GB system, and two times the default on a 1GB system.
    */
    InitializeSListHead(&MiPagedPoolSListHead);
    InitializeSListHead(&MiNonPagedPoolSListHead);

    if (MmNumberOfPhysicalPages >= ((2 * _1GB) /PAGE_SIZE))
    {
        MiNonPagedPoolSListMaximum *= 8;
        MiPagedPoolSListMaximum *= 8;
    }
    else if (MmNumberOfPhysicalPages >= (_1GB /PAGE_SIZE))
    {
        MiNonPagedPoolSListMaximum *= 2;
        MiPagedPoolSListMaximum *= 2;
    }

    /* However if debugging options for the pool are enabled,
       turn off the S-LIST to reduce the risk of messing things up even more
    */
    if (MmProtectFreedNonPagedPool)
    {
        MiNonPagedPoolSListMaximum = 0;
        MiPagedPoolSListMaximum = 0;
    }

    /* We keep 4 lists of free pages (4 lists help avoid contention) */
    for (ix = 0; ix < MI_MAX_FREE_PAGE_LISTS; ix++)
        /* Initialize each of them */
        InitializeListHead(&MmNonPagedPoolFreeListHead[ix]);

    /* Calculate how many pages the initial nonpaged pool has */
    PoolPages = (PFN_COUNT)BYTES_TO_PAGES(MmSizeOfNonPagedPoolInBytes);
    MmNumberOfFreeNonPagedPool = PoolPages;

    /* Initialize the first free entry */
    FreeEntry = MmNonPagedPoolStart;
    FirstEntry = FreeEntry;
    FreeEntry->Size = PoolPages;
    FreeEntry->Signature = MM_FREE_POOL_SIGNATURE;
    FreeEntry->Owner = FirstEntry;

    /* Insert it into the last list */
    InsertHeadList(&MmNonPagedPoolFreeListHead[MI_MAX_FREE_PAGE_LISTS - 1], &FreeEntry->List);

    /* Now create free entries for every single other page */
    while (PoolPages-- > 1)
    {
        /* Link them all back to the original entry */
        FreeEntry = (PMMFREE_POOL_ENTRY)((ULONG_PTR)FreeEntry + PAGE_SIZE);
        FreeEntry->Owner = FirstEntry;
        FreeEntry->Signature = MM_FREE_POOL_SIGNATURE;
    }

    /* Validate and remember first allocated pool page */
    Pte = MiAddressToPte(MmNonPagedPoolStart);
    ASSERT(Pte->u.Hard.Valid == 1);
    MiStartOfInitialPoolFrame = PFN_FROM_PTE(Pte);

    /* Keep track of where initial nonpaged pool ends */
    MmNonPagedPoolEnd0 = (PVOID)((ULONG_PTR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes);

    /* Validate and remember last allocated pool page */
    Pte = MiAddressToPte((PVOID)((ULONG_PTR)MmNonPagedPoolEnd0 - 1));
    ASSERT(Pte->u.Hard.Valid == 1);
    MiEndOfInitialPoolFrame = PFN_FROM_PTE(Pte);

    /* Validate the first nonpaged pool expansion page (which is a guard page) */
    Pte = MiAddressToPte(MmNonPagedPoolExpansionStart);
    ASSERT(Pte->u.Hard.Valid == 0);

    /* Calculate the size of the expansion region alone */
    MiExpansionPoolPagesInitialCharge = (PFN_COUNT)
    BYTES_TO_PAGES(MmMaximumNonPagedPoolInBytes - MmSizeOfNonPagedPoolInBytes);

    /* Remove 2 pages, since there's a guard page on top and on the bottom */
    MiExpansionPoolPagesInitialCharge -= 2;

    /* Now initialize the nonpaged pool expansion PTE space.
       Remember there's a guard page on top so make sure to skip it.
       The bottom guard page will be guaranteed by the fact our size is off by one.
    */
    MiInitializeSystemPtes((Pte + 1), MiExpansionPoolPagesInitialCharge, NonPagedPoolExpansion);
}

INIT_FUNCTION
VOID
NTAPI
MiInitializeNonPagedPoolThresholds(VOID)
{
    PFN_NUMBER Size = MmMaximumNonPagedPoolInPages;

    /* Default low threshold of 8MB or one third of nonpaged pool */
    MiLowNonPagedPoolThreshold = ((8 * _1MB) >> PAGE_SHIFT);
    MiLowNonPagedPoolThreshold = min(MiLowNonPagedPoolThreshold, (Size / 3));

    /* Default high threshold of 20MB or 50% */
    MiHighNonPagedPoolThreshold = ((20 * _1MB) >> PAGE_SHIFT);
    MiHighNonPagedPoolThreshold = min(MiHighNonPagedPoolThreshold, (Size / 2));

    ASSERT(MiLowNonPagedPoolThreshold < MiHighNonPagedPoolThreshold);
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmAllocateMappingAddress(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG PoolTag)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreeMappingAddress(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
