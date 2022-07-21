
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

extern MI_PFN_CACHE_ATTRIBUTE MiPlatformCacheAttributes[2][MmMaximumCacheType];
extern PPHYSICAL_MEMORY_DESCRIPTOR MmPhysicalMemoryBlock;
extern PVOID MmNonPagedPoolStart;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern PVOID MmNonPagedPoolExpansionStart;

/* FUNCTIONS ******************************************************************/

PVOID
NTAPI
MiCheckForContiguousMemory(
    _In_ PVOID BaseAddress,
    _In_ PFN_NUMBER BaseAddressPages,
    _In_ PFN_NUMBER SizeInPages,
    _In_ PFN_NUMBER LowestPfn,
    _In_ PFN_NUMBER HighestPfn,
    _In_ PFN_NUMBER BoundaryPfn,
    _In_ MI_PFN_CACHE_ATTRIBUTE CacheAttribute)
{
    PMMPTE StartPte;
    PMMPTE EndPte;
    PFN_NUMBER PreviousPage = 0;
    PFN_NUMBER Page;
    PFN_NUMBER HighPage;
    PFN_NUMBER BoundaryMask;
    PFN_NUMBER Pages = 0;

    /* Okay, first of all check if the PFNs match our restrictions */
    if (LowestPfn > HighestPfn)
        return NULL;

    if ((LowestPfn + SizeInPages) <= LowestPfn)
        return NULL;

    if ((LowestPfn + SizeInPages - 1) > HighestPfn)
        return NULL;

    if (BaseAddressPages < SizeInPages)
        return NULL;

    /* This is the last page we need to get to and the boundary requested */
    HighPage = (HighestPfn + 1 - SizeInPages);
    BoundaryMask = ~(BoundaryPfn - 1);

    /* And here's the PTEs for this allocation. Let's go scan them. */
    StartPte = MiAddressToPte(BaseAddress);
    EndPte = (StartPte + BaseAddressPages);

    for (; StartPte < EndPte; StartPte++)
    {
        /* Get this PTE's page number */
        ASSERT(StartPte->u.Hard.Valid == 1);
        Page = PFN_FROM_PTE(StartPte);

        /* Is this the beginning of our adventure? */
        if (!Pages)
        {
            /* Check if this PFN is within our range */
            if (Page >= LowestPfn && Page <= HighPage)
            {
                /* It is! Do you care about boundary (alignment)? */
                if (!BoundaryPfn || !((Page ^ (Page + SizeInPages - 1)) & BoundaryMask))
                    /* You don't care, or you do care but we deliver */
                    Pages++;
            }

            /* Have we found all the pages we need by now?
               Incidently, this means you only wanted one page.
            */
            if (Pages == SizeInPages)
                /* Mission complete */
                return MiPteToAddress(StartPte);
        }
        else
        {
            /* Have we found a page that doesn't seem to be contiguous? */
            if (Page != (PreviousPage + 1))
            {
                /* Ah crap, we have to start over */
                Pages = 0;
                continue;
            }

            /* Otherwise, we're still in the game. Do we have all our pages? */
            if (++Pages == SizeInPages)
                /* We do! This entire range was contiguous, so we'll return it! */
                return MiPteToAddress(StartPte - Pages + 1);
        }

        /* Try with the next PTE, remember this PFN */
        PreviousPage = Page;
    }

    /* All good returns are within the loop... */
    return NULL;
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
MmAllocateContiguousMemory(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmAllocateContiguousMemorySpecifyCache(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress OPTIONAL,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS BoundaryAddressMultiple OPTIONAL,
    _In_ MEMORY_CACHING_TYPE CacheType OPTIONAL)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreeContiguousMemory(
    _In_ PVOID BaseAddress)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmFreeContiguousMemorySpecifyCache(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
