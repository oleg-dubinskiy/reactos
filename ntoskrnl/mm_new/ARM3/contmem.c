
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

PFN_NUMBER
NTAPI
MiFindContiguousPages(
    _In_ PFN_NUMBER LowestPfn,
    _In_ PFN_NUMBER HighestPfn,
    _In_ PFN_NUMBER BoundaryPfn,
    _In_ PFN_NUMBER SizeInPages,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    PMMPFN Pfn;
    PMMPFN EndPfn;
    PFN_NUMBER Page;
    PFN_NUMBER PageCount;
    PFN_NUMBER LastPage;
    PFN_NUMBER Length;
    PFN_NUMBER BoundaryMask;
    ULONG ix = 0;
    KIRQL OldIrql;

    PAGED_CODE();
    ASSERT(SizeInPages != 0);

    /* Convert the boundary PFN into an alignment mask */
    BoundaryMask = ~(BoundaryPfn - 1);

    /* Disable APCs */
    KeEnterGuardedRegion();

    /* Loop all the physical memory blocks */
    do
    {
        /* Capture the base page and length of this memory block */
        Page = MmPhysicalMemoryBlock->Run[ix].BasePage;
        PageCount = MmPhysicalMemoryBlock->Run[ix].PageCount;

        /* Check how far this memory block will go */
        LastPage = (Page + PageCount);

        /* Trim it down to only the PFNs we're actually interested in */
        if ((LastPage - 1) > HighestPfn)
            LastPage = (HighestPfn + 1);

        if (Page < LowestPfn)
            Page = LowestPfn;

        /* Skip this run if it's empty or fails to contain all the pages we need */
        if (!PageCount || (Page + SizeInPages) > LastPage)
            continue;

        /* Now scan all the relevant PFNs in this run */
        Length = 0;

        for (Pfn = MI_PFN_ELEMENT(Page); Page < LastPage; Page++, Pfn++)
        {
            /* If this PFN is in use, ignore it */
            if (MiIsPfnInUse(Pfn))
            {
                Length = 0;
                continue;
            }

            /* If we haven't chosen a start PFN yet and the caller specified an alignment,
               make sure the page matches the alignment restriction
            */
            if (!Length && BoundaryPfn && ((Page ^ (Page + SizeInPages - 1)) & BoundaryMask))
                /* It does not, so bail out */
                continue;

            /* Increase the number of valid pages, and check if we have enough */
            if (++Length != SizeInPages)
                continue;

            /* It appears we've amassed enough legitimate pages, rollback */
            Pfn -= (Length - 1);
            Page -= (Length - 1);

            /* Acquire the PFN lock */
            OldIrql = MiLockPfnDb(APC_LEVEL);

            /* Things might've changed for us. Is the page still free? */
            for (; !MiIsPfnInUse(Pfn); Pfn++, Page++)
            {
                /* So far so good. Is this the last confirmed valid page? */
                if (--Length)
                {
                    /* Keep going.
                       The purpose of this loop is to reconfirm
                       that after acquiring the PFN lock these pages are still usable.
                    */
                    continue;
                }

                /* Sanity check that we didn't go out of bounds */
                ASSERT(ix != MmPhysicalMemoryBlock->NumberOfRuns);

                /* Loop until all PFN entries have been processed */
                EndPfn = (Pfn - SizeInPages + 1);
                do
                {
                    /* This PFN is now a used page, set it up */
                    MI_SET_USAGE(MI_USAGE_CONTINOUS_ALLOCATION);
                    MI_SET_PROCESS2("Kernel Driver");

                    MiUnlinkFreeOrZeroedPage(Pfn);

                    Pfn->u3.e2.ReferenceCount = 1;
                    Pfn->u2.ShareCount = 1;
                    Pfn->u3.e1.PageLocation = ActiveAndValid;
                    Pfn->u3.e1.StartOfAllocation = 0;
                    Pfn->u3.e1.EndOfAllocation = 0;
                    Pfn->u3.e1.PrototypePte = 0;
                    Pfn->u4.VerifierAllocation = 0;
                    Pfn->PteAddress = (PVOID)(ULONG_PTR)0xBAADF00DBAADF00DULL;

                    /* Check if this is the last PFN, otherwise go on */
                    if (Pfn == EndPfn)
                        break;

                    Pfn--;
                }
                while (TRUE);

                /* Mark the first and last PFN so we can find them later */
                Pfn->u3.e1.StartOfAllocation = 1;
                (Pfn + SizeInPages - 1)->u3.e1.EndOfAllocation = 1;

                /* Now it's safe to let go of the PFN lock */
                MiUnlockPfnDb(OldIrql, APC_LEVEL);

                /* Quick sanity check that the last PFN is consistent */
                EndPfn = (Pfn + SizeInPages);
                ASSERT(EndPfn == MI_PFN_ELEMENT(Page + 1));

                /* Compute the first page, and make sure it's consistent */
                Page = (Page - SizeInPages + 1);

                ASSERT(Pfn == MI_PFN_ELEMENT(Page));
                ASSERT(Page != 0);

                /* Enable APCs and return the page */
                KeLeaveGuardedRegion();
                return Page;
            }

            /* If we got here, something changed while we hadn't acquired the PFN lock yet,
               so we'll have to restart.
            */
            MiUnlockPfnDb(OldIrql, APC_LEVEL);
            Length = 0;
        }
    }
    while (++ix != MmPhysicalMemoryBlock->NumberOfRuns);

    /* And if we get here, it means no suitable physical memory runs were found */
    KeLeaveGuardedRegion();
    return 0;
}

PVOID
NTAPI
MiFindContiguousMemory(
    _In_ PFN_NUMBER LowestPfn,
    _In_ PFN_NUMBER HighestPfn,
    _In_ PFN_NUMBER BoundaryPfn,
    _In_ PFN_NUMBER SizeInPages,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID BaseAddress;
    PMMPFN Pfn;
    PMMPFN EndPfn;
    PMMPTE Pte;
    PFN_NUMBER Page;

    PAGED_CODE();
    ASSERT(SizeInPages != 0);

    /* Our last hope is to scan the free page list for contiguous pages */
    Page = MiFindContiguousPages(LowestPfn, HighestPfn, BoundaryPfn, SizeInPages, CacheType);
    if (!Page)
        return NULL;

    /* We'll just piggyback on the I/O memory mapper */
    PhysicalAddress.QuadPart = (Page << PAGE_SHIFT);

    BaseAddress = MmMapIoSpace(PhysicalAddress, (SizeInPages << PAGE_SHIFT), CacheType);
    ASSERT(BaseAddress);

    /* Loop the PFN entries */
    Pfn = MiGetPfnEntry(Page);
    EndPfn = (Pfn + SizeInPages);

    Pte = MiAddressToPte(BaseAddress);
    do
    {
        /* Write the PTE address */
        Pfn->PteAddress = Pte;
        Pfn->u4.PteFrame = PFN_FROM_PTE(MiAddressToPte(Pte++));
    }
    while (++Pfn < EndPfn);

    /* Return the address */
    return BaseAddress;
}

PVOID
NTAPI
MiAllocateContiguousMemory(
    _In_ SIZE_T NumberOfBytes,
    _In_ PFN_NUMBER LowestAcceptablePfn,
    _In_ PFN_NUMBER HighestAcceptablePfn,
    _In_ PFN_NUMBER BoundaryPfn,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    PVOID BaseAddress;
    PFN_NUMBER SizeInPages;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;

    /* Verify count and cache type */
    ASSERT(NumberOfBytes != 0);
    ASSERT(CacheType <= MmWriteCombined);

    /* Compute size requested */
    SizeInPages = BYTES_TO_PAGES(NumberOfBytes);

    /* Convert the cache attribute and check for cached requests */
    CacheAttribute = MiPlatformCacheAttributes[FALSE][CacheType];
    if (CacheAttribute == MiCached)
    {
        /* Because initial nonpaged pool is supposed to be contiguous,
           go ahead and try making a nonpaged pool allocation first.
        */
        BaseAddress = ExAllocatePoolWithTag(NonPagedPoolCacheAligned, NumberOfBytes, 'mCmM');
        if (BaseAddress)
        {
            /* Now make sure it's actually contiguous (if it came from expansion it might not be). */
            if (MiCheckForContiguousMemory(BaseAddress,
                                           SizeInPages,
                                           SizeInPages,
                                           LowestAcceptablePfn,
                                           HighestAcceptablePfn,
                                           BoundaryPfn,
                                           CacheAttribute))
            {
                /* Sweet, we're in business! */
                return BaseAddress;
            }

            /* No such luck */
            ExFreePoolWithTag(BaseAddress, 'mCmM');
        }
    }

    /* According to MSDN, the system won't try anything else if you're higher than APC level. */
    if (KeGetCurrentIrql() > APC_LEVEL)
        return NULL;

    /* Otherwise, we'll go try to find some */
    BaseAddress = MiFindContiguousMemory(LowestAcceptablePfn,
                                         HighestAcceptablePfn,
                                         BoundaryPfn,
                                         SizeInPages,
                                         CacheType);
    if (!BaseAddress)
    {
        DPRINT1("Unable to allocate contiguous memory for %d bytes (%d pages), out of memory!\n",
                NumberOfBytes, SizeInPages);
    }

    return BaseAddress;
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
