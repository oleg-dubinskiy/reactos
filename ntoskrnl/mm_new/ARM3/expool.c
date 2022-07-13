
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
ULONG PoolHitTag;
BOOLEAN ExStopBadTags;

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
    PPOOL_TRACKER_TABLE TrackTable = PoolTrackTable;
    ULONG Key;
    ULONG Hash;
    ULONG Index;
    ULONG ix;
    ULONG TagList[] =
    {
        '  oI',
        ' laH',
        'PldM',
        'LooP',
        'tSbO',
        ' prI',
        'bdDN',
        'LprI',
        'pOoI',
        ' ldM',
        'eliF',
        'aVMC',
        'dSeS',
        'CFtN',
        'looP',
        'rPCT',
        'bNMC',
        'dTeS',
        'sFtN',
        'TPCT',
        'CPCT',
        ' yeK',
        'qSbO',
        'mNoI',
        'aEoI',
        'cPCT',
        'aFtN',
        '0ftN',
        'tceS',
        'SprI',
        'ekoT',
        '  eS',
        'lCbO',
        'cScC',
        'lFtN',
        'cAeS',
        'mfSF',
        'kWcC',
        'miSF',
        'CdfA',
        'EdfA',
        'orSF',
        'nftN',
        'PRIU',
        'rFpN',
        'RFpN',
        'aPeS',
        'sUeS',
        'FpcA',
        'MpcA',
        'cSeS',
        'mNbO',
        'sFpN',
        'uLeS',
        'DPcS',
        'nevE',
        'vrqR',
        'ldaV',
        '  pP',
        'SdaV',
        ' daV',
        'LdaV',
        'FdaV',
        ' GIB',
    };

    /* Loop all 0x40 hot tags */
    ASSERT((sizeof(TagList) / sizeof(ULONG)) == 0x40);

    for (ix = 0; ix < (sizeof(TagList) / sizeof(ULONG)); ix++)
    {
        /* Get the current tag, and compute its hash in the tracker table*/
        Key = TagList[ix];
        Hash = ExpComputeHashForTag(Key, PoolTrackTableMask);

        /* Loop all the hashes in this index/bucket */
        Index = Hash;

        do
        {
            /* Find an empty entry, and make sure this isn't the last hash that can fit.
               On checked builds, also make sure this is the first time we are seeding this tag.
            */
            ASSERT(TrackTable[Hash].Key != Key);

            if (!TrackTable[Hash].Key && Hash != (PoolTrackTableSize - 1))
            {
                /* It has been seeded, move on to the next tag */
                TrackTable[Hash].Key = Key;
                break;
            }

            /* This entry was already taken,
               compute the next possible hash while making sure we're not back at our initial index.
            */
            ASSERT(TrackTable[Hash].Key != Key);

            Hash = ((Hash + 1) & PoolTrackTableMask);
        }
        while (Hash != Index);
    }
}

VOID
NTAPI
ExpInsertPoolTracker(
    _In_ ULONG Key,
    _In_ SIZE_T NumberOfBytes,
    _In_ POOL_TYPE PoolType)
{
    PPOOL_TRACKER_TABLE TableEntry;
    PPOOL_TRACKER_TABLE Table;
    SIZE_T TableSize;
    SIZE_T TableMask;
    ULONG Index;
    ULONG Hash;
    KIRQL OldIrql;

    /* Remove the PROTECTED_POOL flag which is not part of the tag */
    Key &= ~PROTECTED_POOL;

    /* With WinDBG you can set a tag you want to break on when an allocation is attempted */
    if (Key == PoolHitTag)
        DbgBreakPoint();

    /* There is also an internal flag you can set to break on malformed tags */
    if (ExStopBadTags)
        ASSERT(Key & 0xFFFFFF00);

    /* ASSERT on ReactOS features not yet supported */
    ASSERT(!(PoolType & SESSION_POOL_MASK));
    ASSERT(KeGetCurrentProcessorNumber() == 0);

    /* Why the double indirection?
       Because normally this function is also used when doing session pool allocations,
       which has another set of tables, sizes, and masks that live in session pool.
       Now we don't support session pool so we only ever use the regular tables,
       but I'm keeping the code this way so that the day we DO support session pool,
       it won't require that many changes.
    */
    Table = PoolTrackTable;
    TableMask = PoolTrackTableMask;
    TableSize = PoolTrackTableSize;

    DBG_UNREFERENCED_LOCAL_VARIABLE(TableSize);

    /* Compute the hash for this key, and loop all the possible buckets */
    Hash = ExpComputeHashForTag(Key, TableMask);
    Index = Hash;

    do
    {
        /* Do we already have an entry for this tag? */
        TableEntry = &Table[Hash];
        if (TableEntry->Key == Key)
        {
            /* Increment the counters depending on if this was paged or nonpaged pool */
            if ((PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool)
            {
                InterlockedIncrement(&TableEntry->NonPagedAllocs);
                InterlockedExchangeAddSizeT(&TableEntry->NonPagedBytes, NumberOfBytes);
                return;
            }

            InterlockedIncrement(&TableEntry->PagedAllocs);
            InterlockedExchangeAddSizeT(&TableEntry->PagedBytes, NumberOfBytes);
            return;
        }

        /* We don't have an entry yet, but we've found a free bucket for it */
        if (!TableEntry->Key && Hash != (PoolTrackTableSize - 1))
        {
            /* We need to hold the lock while creating a new entry,
               since other processors might be in this code path as well.
            */
            ExAcquireSpinLock(&ExpTaggedPoolLock, &OldIrql);

            if (!PoolTrackTable[Hash].Key)
            {
                /* We've won the race, so now create this entry in the bucket */
                ASSERT(Table[Hash].Key == 0);

                PoolTrackTable[Hash].Key = Key;
                TableEntry->Key = Key;
            }

            ExReleaseSpinLock(&ExpTaggedPoolLock, OldIrql);

            /* Now we force the loop to run again,
               and we should now end up in the code path above which does the interlocked increments...
            */
            continue;
        }

        /* This path is hit when we don't have an entry, and the current bucket is full,
           so we simply try the next one.
        */
        Hash = ((Hash + 1) & TableMask);
    }
    while (Hash != Index);

    /* And finally this path is hit when all the buckets are full, and we need some expansion.
       This path is not yet supported in ReactOS and so we'll ignore the tag
    */
    DPRINT1("Out of pool tag space, ignoring...\n");
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
