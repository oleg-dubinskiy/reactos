
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

#undef ExAllocatePoolWithQuota
#undef ExAllocatePoolWithQuotaTag

#define POOL_BIG_TABLE_ENTRY_FREE 0x1

/* GLOBALS ********************************************************************/

ULONG ExpNumberOfPagedPools;
POOL_DESCRIPTOR NonPagedPoolDescriptor;
PPOOL_DESCRIPTOR ExpPagedPoolDescriptor[16 + 1];
PPOOL_DESCRIPTOR PoolVector[2];
PPOOL_TRACKER_TABLE PoolTrackTable;
PPOOL_TRACKER_BIG_PAGES PoolBigPageTable;
KSPIN_LOCK ExpTaggedPoolLock;
SIZE_T PoolTrackTableSize;
SIZE_T PoolTrackTableMask;
SIZE_T PoolBigPageTableSize;
SIZE_T PoolBigPageTableHash;

extern SIZE_T MmSizeOfNonPagedPoolInBytes;

/* FUNCTIONS ******************************************************************/

FORCEINLINE
ULONG
ExpComputeHashForTag(
    _In_ ULONG Tag,
    _In_ SIZE_T BucketMask)
{
    /* Compute the hash by multiplying with a large prime number and then XORing with the HIDWORD of the result.
       Finally, AND with the bucket mask to generate a valid index/bucket into the table
    */
    ULONGLONG Result = ((ULONGLONG)0x9E5F * Tag); // 40543
    return ((ULONG)BucketMask & ((ULONG)Result ^ (Result >> 32)));
}

INIT_FUNCTION
VOID
NTAPI
ExpSeedHotTags(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExpInsertPoolTracker(
    _In_ ULONG Key,
    _In_ SIZE_T NumberOfBytes,
    _In_ POOL_TYPE PoolType)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
ExInitializePoolDescriptor(
    _In_ PPOOL_DESCRIPTOR PoolDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG PoolIndex,
    _In_ ULONG Threshold,
    _In_ PVOID PoolLock)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
ExGetPoolTagInfo(
    _In_ PSYSTEM_POOLTAG_INFORMATION SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Inout_ PULONG ReturnLength OPTIONAL)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
ExQueryPoolUsage(
    _Out_ PULONG PagedPoolPages,
    _Out_ PULONG NonPagedPoolPages,
    _Out_ PULONG PagedPoolAllocs,
    _Out_ PULONG PagedPoolFrees,
    _Out_ PULONG PagedPoolLookasideHits,
    _Out_ PULONG NonPagedPoolAllocs,
    _Out_ PULONG NonPagedPoolFrees,
    _Out_ PULONG NonPagedPoolLookasideHits)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExReturnPoolQuota(
    _In_ PVOID P)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExpCheckPoolAllocation(
    PVOID P,
    POOL_TYPE PoolType,
    ULONG Tag)
{
    UNIMPLEMENTED_DBGBREAK();
}

INIT_FUNCTION
VOID
NTAPI
InitializePool(
    _In_ POOL_TYPE PoolType,
    _In_ ULONG Threshold)
{
    //PPOOL_DESCRIPTOR Descriptor;
    SIZE_T TableSize;
    SIZE_T Size;
    ULONG ix;

    /* Check what kind of pool this is */
    if (PoolType == NonPagedPool)
    {
        /* Compute the track table size and convert it from a power of two to an actual byte size

           NOTE: On checked builds, we'll assert if the registry table size was invalid,
                 while on retail builds we'll just break out of the loop at that point.
        */
        TableSize = min(PoolTrackTableSize, MmSizeOfNonPagedPoolInBytes >> 8);

        for (ix = 0; ix < 0x20; ix++)
        {
            if (TableSize & 1)
            {
                ASSERT((TableSize & ~1) == 0);

                if (!(TableSize & ~1))
                    break;
            }

            TableSize >>= 1;
        }

        /* If we hit bit 0x20, than no size was defined in the registry, so we'll use the default size of 0x800 entries.
           Otherwise, use the size from the registry, as long as it's not smaller than 0x40 entries.
        */
        if (ix == 0x20)
            PoolTrackTableSize = 0x800;
        else
            PoolTrackTableSize = max(1 << ix, 0x40);

        /* Loop trying with the biggest specified size first,
           and cut it down by a power of two each iteration in case not enough memory exist
        */
        while (TRUE)
        {
            /* Do not allow overflow */
            if ((PoolTrackTableSize + 1) > (MAXULONG_PTR / sizeof(POOL_TRACKER_TABLE)))
            {
                PoolTrackTableSize >>= 1;
                continue;
            }

            /* Allocate the tracker table and exit the loop if this worked */
            Size = ((PoolTrackTableSize + 1) * sizeof(POOL_TRACKER_TABLE));

            PoolTrackTable = MiAllocatePoolPages(NonPagedPool, Size);
            if (PoolTrackTable)
                break;

            /* Otherwise, as long as we're not down to the last bit, keep iterating*/
            if (PoolTrackTableSize == 1)
            {
                KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, TableSize, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
            }

            PoolTrackTableSize >>= 1;
        }

        /* Add one entry, compute the hash, and zero the table */
        PoolTrackTableSize++;
        PoolTrackTableMask = (PoolTrackTableSize - 2);

        RtlZeroMemory(PoolTrackTable, (PoolTrackTableSize * sizeof(POOL_TRACKER_TABLE)));

        /* Finally, add the most used tags to speed up those allocations */
        ExpSeedHotTags();

        /* We now do the exact same thing with the tracker table for big pages*/
        TableSize = min(PoolBigPageTableSize, MmSizeOfNonPagedPoolInBytes >> 8);

        for (ix = 0; ix < 0x20; ix++)
        {
            if (TableSize & 1)
            {
                ASSERT((TableSize & ~1) == 0);

                if (!(TableSize & ~1))
                    break;
            }

            TableSize >>= 1;
        }

        /* For big pages, the default tracker table is 0x1000 entries, while the minimum is still 0x40 */
        if (ix == 0x20)
            PoolBigPageTableSize = 0x1000;
        else
            PoolBigPageTableSize = max(1 << ix, 0x40);

        /* Again, run the exact same loop we ran earlier, but this time for the big pool tracker instead*/
        while (TRUE)
        {
            if ((PoolBigPageTableSize + 1) > (MAXULONG_PTR / sizeof(POOL_TRACKER_BIG_PAGES)))
            {
                PoolBigPageTableSize >>= 1;
                continue;
            }

            Size = (PoolBigPageTableSize * sizeof(POOL_TRACKER_BIG_PAGES));

            PoolBigPageTable = MiAllocatePoolPages(NonPagedPool, Size);
            if (PoolBigPageTable)
                break;

            if (PoolBigPageTableSize == 1)
            {
                KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, TableSize, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
            }

            PoolBigPageTableSize >>= 1;
        }

        /* An extra entry is not needed for for the big pool tracker, so just compute the hash and zero it */
        PoolBigPageTableHash = (PoolBigPageTableSize - 1);

        RtlZeroMemory(PoolBigPageTable, (PoolBigPageTableSize * sizeof(POOL_TRACKER_BIG_PAGES)));

        for (ix = 0; ix < PoolBigPageTableSize; ix++)
            PoolBigPageTable[ix].Va = (PVOID)POOL_BIG_TABLE_ENTRY_FREE;

        /* During development, print this out so we can see what's happening */
        DPRINT("EXPOOL: Pool Tracker Table at: 0x%p with 0x%lx bytes\n",
                PoolTrackTable, (PoolTrackTableSize * sizeof(POOL_TRACKER_TABLE)));

        DPRINT("EXPOOL: Big Pool Tracker Table at: 0x%p with 0x%lx bytes\n",
                PoolBigPageTable, (PoolBigPageTableSize * sizeof(POOL_TRACKER_BIG_PAGES)));

        /* Insert the generic tracker for all of big pool */
        Size = ROUND_TO_PAGES(PoolBigPageTableSize * sizeof(POOL_TRACKER_BIG_PAGES));
        ExpInsertPoolTracker('looP', Size, NonPagedPool);

        /* No support for NUMA systems at this time */
        ASSERT(KeNumberNodes == 1);

        /* Initialize the tag spinlock */
        KeInitializeSpinLock(&ExpTaggedPoolLock);

        /* Initialize the nonpaged pool descriptor*/
        PoolVector[NonPagedPool] = &NonPagedPoolDescriptor;
        ExInitializePoolDescriptor(PoolVector[NonPagedPool], NonPagedPool, 0, Threshold, NULL);
    }
    else
    {
        UNIMPLEMENTED_DBGBREAK();
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
ExAllocatePool(
    POOL_TYPE PoolType,
    SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithQuota(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithQuotaTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithTag(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithTagPriority(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ EX_POOL_PRIORITY Priority)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
ExFreePool(
    PVOID P)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
ExFreePoolWithTag(
    _In_ PVOID P,
    _In_ ULONG TagToFree)
{
    UNIMPLEMENTED_DBGBREAK();
}

SIZE_T
NTAPI
ExQueryPoolBlockSize(
    _In_ PVOID PoolBlock,
    _Out_ PBOOLEAN QuotaCharged)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

/* EOF */
