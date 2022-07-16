
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

#undef ExAllocatePoolWithQuota
#undef ExAllocatePoolWithQuotaTag

#define POOL_BIG_TABLE_ENTRY_FREE 0x1

/* GLOBALS ********************************************************************/

/* Pool block/header/list access macros */
#define POOL_ENTRY(x)       (PPOOL_HEADER)((ULONG_PTR)(x) - sizeof(POOL_HEADER))
#define POOL_FREE_BLOCK(x)  (PLIST_ENTRY)((ULONG_PTR)(x)  + sizeof(POOL_HEADER))
#define POOL_BLOCK(x, i)    (PPOOL_HEADER)((ULONG_PTR)(x) + ((i) * POOL_BLOCK_SIZE))
#define POOL_NEXT_BLOCK(x)  POOL_BLOCK((x), (x)->BlockSize)
#define POOL_PREV_BLOCK(x)  POOL_BLOCK((x), -((x)->PreviousSize))

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
ULONG ExpPoolFlags;
ULONG ExPoolFailures;
ULONGLONG MiLastPoolDumpTime;
BOOLEAN ExStopBadTags;

extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern ULONG MmSpecialPoolTag;
extern PMMPTE MiSpecialPoolFirstPte;

/* FUNCTIONS ******************************************************************/

#if DBG
/*
 * FORCEINLINE
 * BOOLEAN
 * ExpTagAllowPrint(CHAR Tag);
 */
#define ExpTagAllowPrint(Tag)   \
    ((Tag) >= 0x20 /* Space */ && (Tag) <= 0x7E /* Tilde */)

#ifdef KDBG
  #define MiDumperPrint(dbg, fmt, ...)        \
      if (dbg) KdbpPrint(fmt, ##__VA_ARGS__); \
      else DPRINT1(fmt, ##__VA_ARGS__)
#else
  #define MiDumperPrint(dbg, fmt, ...)        \
      DPRINT1(fmt, ##__VA_ARGS__)
#endif

VOID
MiDumpPoolConsumers(
    _In_ BOOLEAN CalledFromDbg,
    _In_ ULONG Tag,
    _In_ ULONG Mask,
    _In_ ULONG Flags)
{
    SIZE_T ix;
    BOOLEAN Verbose;

    /* Only print header if called from OOM situation */
    if (!CalledFromDbg)
    {
        DPRINT1("---------------------\n");
        DPRINT1("Out of memory dumper!\n");
    }
  #ifdef KDBG
    else
    {
        KdbpPrint("Pool Used:\n");
    }
  #endif

    /* Remember whether we'll have to be verbose.
       This is the only supported flag!
    */
    Verbose = BooleanFlagOn(Flags, 1);

    /* Print table header */
    if (Verbose)
    {
        MiDumperPrint(CalledFromDbg, "\t\t\t\tNonPaged\t\t\t\t\t\t\tPaged\n");
        MiDumperPrint(CalledFromDbg, "Tag\t\tAllocs\t\tFrees\t\tDiff\t\tUsed\t\tAllocs\t\tFrees\t\tDiff\t\tUsed\n");
    }
    else
    {
        MiDumperPrint(CalledFromDbg, "\t\tNonPaged\t\t\tPaged\n");
        MiDumperPrint(CalledFromDbg, "Tag\t\tAllocs\t\tUsed\t\tAllocs\t\tUsed\n");
    }

    /* We'll extract allocations for all the tracked pools */
    for (ix = 0; ix < PoolTrackTableSize; ix++)
    {
        PPOOL_TRACKER_TABLE TableEntry;

        TableEntry = &PoolTrackTable[ix];

        /* We only care about tags which have allocated memory */
        if (TableEntry->NonPagedBytes || TableEntry->PagedBytes)
        {
            /* If there's a tag, attempt to do a pretty print only if it matches the caller's tag,
               or if any tag is allowed.
               For checking whether it matches caller's tag,
               use the mask to make sure not to mess with the wildcards.
            */
            if (TableEntry->Key != 0 && TableEntry->Key != TAG_NONE &&
                (Tag == 0 || (TableEntry->Key & Mask) == (Tag & Mask)))
            {
                CHAR Tag[4];

                /* Extract each 'component' and check whether they are printable */
                Tag[0] = (TableEntry->Key & 0xFF);
                Tag[1] = ((TableEntry->Key >> 8) & 0xFF);
                Tag[2] = ((TableEntry->Key >> 16) & 0xFF);
                Tag[3] = ((TableEntry->Key >> 24) & 0xFF);

                if (ExpTagAllowPrint(Tag[0]) &&
                    ExpTagAllowPrint(Tag[1]) &&
                    ExpTagAllowPrint(Tag[2]) &&
                    ExpTagAllowPrint(Tag[3]))
                {
                    /* Print in direct order to make !poolused TAG usage easier */
                    if (Verbose)
                    {
                        MiDumperPrint(CalledFromDbg, "'%c%c%c%c'\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\n", Tag[0], Tag[1], Tag[2], Tag[3],
                                      TableEntry->NonPagedAllocs, TableEntry->NonPagedFrees,
                                      (TableEntry->NonPagedAllocs - TableEntry->NonPagedFrees), TableEntry->NonPagedBytes,
                                      TableEntry->PagedAllocs, TableEntry->PagedFrees,
                                      (TableEntry->PagedAllocs - TableEntry->PagedFrees), TableEntry->PagedBytes);
                    }
                    else
                    {
                        MiDumperPrint(CalledFromDbg, "'%c%c%c%c'\t\t%ld\t\t%ld\t\t%ld\t\t%ld\n", Tag[0], Tag[1], Tag[2], Tag[3],
                                      TableEntry->NonPagedAllocs, TableEntry->NonPagedBytes,
                                      TableEntry->PagedAllocs, TableEntry->PagedBytes);
                    }
                }
                else
                {
                    if (Verbose)
                    {
                        MiDumperPrint(CalledFromDbg, "0x%08x\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\n", TableEntry->Key,
                                      TableEntry->NonPagedAllocs, TableEntry->NonPagedFrees,
                                      (TableEntry->NonPagedAllocs - TableEntry->NonPagedFrees), TableEntry->NonPagedBytes,
                                      TableEntry->PagedAllocs, TableEntry->PagedFrees,
                                      (TableEntry->PagedAllocs - TableEntry->PagedFrees), TableEntry->PagedBytes);
                    }
                    else
                    {
                        MiDumperPrint(CalledFromDbg, "0x%08x\t%ld\t\t%ld\t\t%ld\t\t%ld\n", TableEntry->Key,
                                      TableEntry->NonPagedAllocs, TableEntry->NonPagedBytes,
                                      TableEntry->PagedAllocs, TableEntry->PagedBytes);
                    }
                }
            }
            else if (Tag == 0 || (Tag & Mask) == (TAG_NONE & Mask))
            {
                if (Verbose)
                {
                    MiDumperPrint(CalledFromDbg, "Anon\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\t\t%ld\n",
                                  TableEntry->NonPagedAllocs, TableEntry->NonPagedFrees,
                                  (TableEntry->NonPagedAllocs - TableEntry->NonPagedFrees), TableEntry->NonPagedBytes,
                                  TableEntry->PagedAllocs, TableEntry->PagedFrees,
                                  (TableEntry->PagedAllocs - TableEntry->PagedFrees), TableEntry->PagedBytes);
                }
                else
                {
                    MiDumperPrint(CalledFromDbg, "Anon\t\t%ld\t\t%ld\t\t%ld\t\t%ld\n",
                                  TableEntry->NonPagedAllocs, TableEntry->NonPagedBytes,
                                  TableEntry->PagedAllocs, TableEntry->PagedBytes);
                }
            }
        }
    }

    if (!CalledFromDbg)
    {
        DPRINT1("---------------------\n");
    }
}
#endif

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

FORCEINLINE
KIRQL
ExLockPool(
    _In_ PPOOL_DESCRIPTOR Descriptor)
{
    /* Check if this is nonpaged pool */
    if ((Descriptor->PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool)
        /* Use the queued spin lock */
        return KeAcquireQueuedSpinLock(LockQueueNonPagedPoolLock);

    /* Use the guarded mutex */
    KeAcquireGuardedMutex(Descriptor->LockAddress);

    return APC_LEVEL;
}

FORCEINLINE
VOID
ExUnlockPool(
    _In_ PPOOL_DESCRIPTOR Descriptor,
    _In_ KIRQL OldIrql)
{
    /* Check if this is nonpaged pool */
    if ((Descriptor->PoolType & BASE_POOL_TYPE_MASK) == NonPagedPool)
        /* Use the queued spin lock */
        KeReleaseQueuedSpinLock(LockQueueNonPagedPoolLock, OldIrql);
    else
        /* Use the guarded mutex */
        KeReleaseGuardedMutex(Descriptor->LockAddress);
}

PLIST_ENTRY
NTAPI
ExpEncodePoolLink(
    _In_ PLIST_ENTRY Link)
{
    return (PLIST_ENTRY)((ULONG_PTR)Link | 1);
}

PLIST_ENTRY
NTAPI
ExpDecodePoolLink(
    _In_ PLIST_ENTRY Link)
{
    return (PLIST_ENTRY)((ULONG_PTR)Link & ~1);
}

VOID
NTAPI
ExpInitializePoolListHead(
    _In_ PLIST_ENTRY ListHead)
{
    ListHead->Flink = ListHead->Blink = ExpEncodePoolLink(ListHead);
}

BOOLEAN
NTAPI
ExpIsPoolListEmpty(
    _In_ PLIST_ENTRY ListHead)
{
    return (ExpDecodePoolLink(ListHead->Flink) == ListHead);
}

VOID
NTAPI
ExpCheckPoolLinks(
    _In_ PLIST_ENTRY ListHead)
{
    if ((ExpDecodePoolLink(ExpDecodePoolLink(ListHead->Flink)->Blink) != ListHead) ||
        (ExpDecodePoolLink(ExpDecodePoolLink(ListHead->Blink)->Flink) != ListHead))
    {
        KeBugCheckEx(BAD_POOL_HEADER,
                     3,
                     (ULONG_PTR)ListHead,
                     (ULONG_PTR)ExpDecodePoolLink(ExpDecodePoolLink(ListHead->Flink)->Blink),
                     (ULONG_PTR)ExpDecodePoolLink(ExpDecodePoolLink(ListHead->Blink)->Flink));
    }
}

PLIST_ENTRY
NTAPI
ExpRemovePoolHeadList(
    _In_ PLIST_ENTRY ListHead)
{
    PLIST_ENTRY Entry;
    PLIST_ENTRY Flink;

    Entry = ExpDecodePoolLink(ListHead->Flink);
    Flink = ExpDecodePoolLink(Entry->Flink);
    ListHead->Flink = ExpEncodePoolLink(Flink);
    Flink->Blink = ExpEncodePoolLink(ListHead);

    return Entry;
}

VOID
NTAPI
ExpInsertPoolTailList(
    _In_ PLIST_ENTRY ListHead,
    _In_ PLIST_ENTRY Entry)
{
    PLIST_ENTRY Blink;

    ExpCheckPoolLinks(ListHead);

    Blink = ExpDecodePoolLink(ListHead->Blink);
    Entry->Flink = ExpEncodePoolLink(ListHead);
    Entry->Blink = ExpEncodePoolLink(Blink);
    Blink->Flink = ExpEncodePoolLink(Entry);
    ListHead->Blink = ExpEncodePoolLink(Entry);

    ExpCheckPoolLinks(ListHead);
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

    while (TRUE)
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
        //Hash = ((Hash + 1) & TableMask);
        Hash = (Hash + 1) & TableMask;
        if (Hash == Index)
            break;
    }

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
    PLIST_ENTRY NextEntry;

    /* Setup the descriptor based on the caller's request */
    PoolDescriptor->PoolType = PoolType;
    PoolDescriptor->PoolIndex = PoolIndex;
    PoolDescriptor->Threshold = Threshold;
    PoolDescriptor->LockAddress = PoolLock;

    /* Initialize accounting data */
    PoolDescriptor->RunningAllocs = 0;
    PoolDescriptor->RunningDeAllocs = 0;
    PoolDescriptor->TotalPages = 0;
    PoolDescriptor->TotalBytes = 0;
    PoolDescriptor->TotalBigPages = 0;

    /* Nothing pending for now */
    PoolDescriptor->PendingFrees = NULL;
    PoolDescriptor->PendingFreeDepth = 0;

    /* Loop all the descriptor's allocation lists and initialize them */
    for (NextEntry = PoolDescriptor->ListHeads;
         NextEntry < (PoolDescriptor->ListHeads + POOL_LISTS_PER_PAGE);
         NextEntry++)
    {
        ExpInitializePoolListHead(NextEntry);
    }

    /* Note that ReactOS does not support Session Pool Yet */
    ASSERT(PoolType != PagedPoolSession);
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

    DPRINT1("InitializePool: PoolType %X, Threshold %X\n", PoolType, Threshold);

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

FORCEINLINE
VOID
ExpCheckPoolIrqlLevel(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ PVOID Entry)
{
    /* Validate IRQL: It must be APC_LEVEL or lower for Paged Pool,
       and it must be DISPATCH_LEVEL or lower for Non Paged Pool
    */
    if (((PoolType & BASE_POOL_TYPE_MASK) == PagedPool) ?
        (KeGetCurrentIrql() > APC_LEVEL) :
        (KeGetCurrentIrql() > DISPATCH_LEVEL))
    {
        // Take the system down
        KeBugCheckEx(BAD_POOL_CALLER,
                     !Entry ? POOL_ALLOC_IRQL_INVALID : POOL_FREE_IRQL_INVALID,
                     KeGetCurrentIrql(),
                     PoolType,
                     !Entry ? NumberOfBytes : (ULONG_PTR)Entry);
    }
}

BOOLEAN
NTAPI
MmUseSpecialPool(
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    /* Special pool is not suitable for allocations bigger than 1 page */
    if (NumberOfBytes > (PAGE_SIZE - sizeof(POOL_HEADER)))
        return FALSE;

    if (MmSpecialPoolTag == '*')
        return TRUE;

    return (Tag == MmSpecialPoolTag);
}

BOOLEAN
NTAPI
ExpAddTagForBigPages(
    _In_ PVOID Va,
    _In_ ULONG Key,
    _In_ ULONG NumberOfPages,
    _In_ POOL_TYPE PoolType)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

VOID
NTAPI
ExpCheckPoolHeader(
    _In_ PPOOL_HEADER Entry)
{
    PPOOL_HEADER PreviousEntry;
    PPOOL_HEADER NextEntry;

    /* Is there a block before this one? */
    if (Entry->PreviousSize)
    {
        /* Get it */
        PreviousEntry = POOL_PREV_BLOCK(Entry);

        /* The two blocks must be on the same page! */
        if (PAGE_ALIGN(Entry) != PAGE_ALIGN(PreviousEntry))
        {
            /* Something is awry */
            KeBugCheckEx(BAD_POOL_HEADER, 6, (ULONG_PTR)PreviousEntry, __LINE__, (ULONG_PTR)Entry);
        }

        /* This block should also indicate that it's as large as we think it is */
        if (PreviousEntry->BlockSize != Entry->PreviousSize)
        {
            /* Otherwise, someone corrupted one of the sizes */
            DPRINT1("PreviousEntry BlockSize %lu, tag %.4s. Entry PreviousSize %lu, tag %.4s\n",
                    PreviousEntry->BlockSize, (char *)&PreviousEntry->PoolTag, Entry->PreviousSize, (char *)&Entry->PoolTag);

            KeBugCheckEx(BAD_POOL_HEADER, 5, (ULONG_PTR)PreviousEntry, __LINE__, (ULONG_PTR)Entry);
        }
    }
    else if (PAGE_ALIGN(Entry) != Entry)
    {
        /* If there's no block before us, we are the first block, so we should be on a page boundary */
        KeBugCheckEx(BAD_POOL_HEADER, 7, 0, __LINE__, (ULONG_PTR)Entry);
    }

    /* This block must have a size */
    if (!Entry->BlockSize)
    {
        /* Someone must've corrupted this field */
        if (Entry->PreviousSize)
        {
            PreviousEntry = POOL_PREV_BLOCK(Entry);
            DPRINT1("PreviousEntry tag %.4s. Entry tag %.4s\n", (char *)&PreviousEntry->PoolTag, (char *)&Entry->PoolTag);
        }
        else
        {
            DPRINT1("Entry tag %.4s\n", (char *)&Entry->PoolTag);
        }

        KeBugCheckEx(BAD_POOL_HEADER, 8, 0, __LINE__, (ULONG_PTR)Entry);
    }

    /* Okay, now get the next block */
    NextEntry = POOL_NEXT_BLOCK(Entry);

    /* If this is the last block, then we'll be page-aligned, otherwise, check this block */
    if (PAGE_ALIGN(NextEntry) != NextEntry)
    {
        /* The two blocks must be on the same page! */
        if (PAGE_ALIGN(Entry) != PAGE_ALIGN(NextEntry))
        {
            /* Something is messed up */
            KeBugCheckEx(BAD_POOL_HEADER, 9, (ULONG_PTR)NextEntry, __LINE__, (ULONG_PTR)Entry);
        }

        /* And this block should think we are as large as we truly are */
        if (NextEntry->PreviousSize != Entry->BlockSize)
        {
            /* Otherwise, someone corrupted the field */
            DPRINT1("Entry BlockSize %lu, tag %.4s. NextEntry PreviousSize %lu, tag %.4s\n",
                    Entry->BlockSize, (char *)&Entry->PoolTag, NextEntry->PreviousSize, (char *)&NextEntry->PoolTag);

            KeBugCheckEx(BAD_POOL_HEADER, 5, (ULONG_PTR)NextEntry, __LINE__, (ULONG_PTR)Entry);
        }
    }
}

VOID
NTAPI
ExpCheckPoolBlocks(
    _In_ PVOID Block)
{
    BOOLEAN FoundBlock = FALSE;
    SIZE_T Size = 0;
    PPOOL_HEADER Entry;

    /* Get the first entry for this page, make sure it really is the first */
    Entry = PAGE_ALIGN(Block);
    ASSERT(Entry->PreviousSize == 0);

    /* Now scan each entry */
    while (TRUE)
    {
        /* When we actually found our block, remember this */
        if (Entry == Block)
            FoundBlock = TRUE;

        /* Now validate this block header */
        ExpCheckPoolHeader(Entry);

        /* And go to the next one, keeping track of our size */
        Size += Entry->BlockSize;
        Entry = POOL_NEXT_BLOCK(Entry);

        /* If we hit the last block, stop */
        if (Size >= (PAGE_SIZE / POOL_BLOCK_SIZE))
            break;

        /* If we hit the end of the page, stop */
        if (PAGE_ALIGN(Entry) == Entry)
            break;
    }

    /* We must've found our block, and we must have hit the end of the page */
    if (PAGE_ALIGN(Entry) != Entry || !FoundBlock)
    {
        /* Otherwise, the blocks are messed up */
        KeBugCheckEx(BAD_POOL_HEADER, 10, (ULONG_PTR)Block, __LINE__, (ULONG_PTR)Entry);
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
    PKPRCB Prcb = KeGetCurrentPrcb();
    PGENERAL_LOOKASIDE LookasideList;
    PPOOL_DESCRIPTOR PoolDesc;
    PPOOL_HEADER Entry;
    PPOOL_HEADER NextEntry;
    PPOOL_HEADER FragmentEntry;
    PLIST_ENTRY ListHead;
    ULONG OriginalType;
    USHORT BlockSize;
    USHORT ix;
    KIRQL OldIrql;

    /* Some sanity checks */
    ASSERT(Tag != 0);
    ASSERT(Tag != ' GIB');
    ASSERT(NumberOfBytes != 0);

    ExpCheckPoolIrqlLevel(PoolType, NumberOfBytes, NULL);

    /* Not supported in ReactOS */
    ASSERT(!(PoolType & SESSION_POOL_MASK));

    /* Check if verifier or special pool is enabled */
    if (ExpPoolFlags & (POOL_FLAG_VERIFIER | POOL_FLAG_SPECIAL_POOL))
    {
        /* For verifier, we should call the verification routine */
        if (ExpPoolFlags & POOL_FLAG_VERIFIER)
        {
            DPRINT1("ExAllocatePoolWithTag: Driver Verifier is not yet supported\n");
        }

        /* For special pool, we check if this is a suitable allocation and do the special allocation if needed */
        if (ExpPoolFlags & POOL_FLAG_SPECIAL_POOL)
        {
            /* Check if this is a special pool allocation */
            if (MmUseSpecialPool(NumberOfBytes, Tag))
            {
                /* Try to allocate using special pool */
                Entry = MmAllocateSpecialPool(NumberOfBytes, Tag, PoolType, 2);
                if (Entry)
                    return Entry;
            }
        }
    }

    /* Get the pool type and its corresponding vector for this request */
    OriginalType = PoolType;
    PoolType = PoolType & BASE_POOL_TYPE_MASK;
    PoolDesc = PoolVector[PoolType];
    ASSERT(PoolDesc != NULL);

    /* Check if this is a big page allocation */
    if (NumberOfBytes > POOL_MAX_ALLOC)
    {
        /* Allocate pages for it */
        Entry = MiAllocatePoolPages(OriginalType, NumberOfBytes);
        if (!Entry)
        {
#if DBG
            /* Out of memory, display current consumption
               Let's consider that if the caller wanted more than a hundred pages,
               that's a bogus caller and we are not out of memory.
               Dump at most once a second to avoid spamming the log.
            */
            if (NumberOfBytes < (100 * PAGE_SIZE) &&
                KeQueryInterruptTime() >= (MiLastPoolDumpTime + 10000000))
            {
                MiDumpPoolConsumers(FALSE, 0, 0, 0);
                MiLastPoolDumpTime = KeQueryInterruptTime();
            }
#endif

            /* Must succeed pool is deprecated, but still supported.
              These allocation failures must cause an immediate bugcheck.
            */
            if (OriginalType & MUST_SUCCEED_POOL_MASK)
            {
                KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY,
                             NumberOfBytes,
                             NonPagedPoolDescriptor.TotalPages,
                             NonPagedPoolDescriptor.TotalBigPages,
                             0);
            }

            /* Internal debugging */
            ExPoolFailures++;

            /* This flag requests printing failures, and can also further specify breaking on failures */
            if (ExpPoolFlags & POOL_FLAG_DBGPRINT_ON_FAILURE)
            {
                DPRINT1("EX: ExAllocatePool (%lu, 0x%x) returning NULL\n", NumberOfBytes, OriginalType);

                if (ExpPoolFlags & POOL_FLAG_CRASH_ON_FAILURE)
                    DbgBreakPoint();
            }

            /* Finally, this flag requests an exception, which we are more than happy to raise! */
            if (OriginalType & POOL_RAISE_IF_ALLOCATION_FAILURE)
                ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

            return NULL;
        }

        /* Increment required counters */
        InterlockedExchangeAdd((PLONG)&PoolDesc->TotalBigPages, (LONG)BYTES_TO_PAGES(NumberOfBytes));
        InterlockedExchangeAddSizeT(&PoolDesc->TotalBytes, NumberOfBytes);
        InterlockedIncrement((PLONG)&PoolDesc->RunningAllocs);

        /* Add a tag for the big page allocation and switch to the generic "BIG" tag
           if we failed to do so, then insert a tracker for this alloation.
        */
        if (!ExpAddTagForBigPages(Entry, Tag, (ULONG)BYTES_TO_PAGES(NumberOfBytes), OriginalType))
            Tag = ' GIB';

        ExpInsertPoolTracker(Tag, ROUND_TO_PAGES(NumberOfBytes), OriginalType);
        return Entry;
    }

    /* Should never request 0 bytes from the pool, but since so many drivers do it,
       we'll just assume they want 1 byte, based on NT's similar behavior
    */
    if (!NumberOfBytes)
        NumberOfBytes = 1;

    /* A pool allocation is defined by its data, a linked list to connect it to the free list (if necessary),
       and a pool header to store accounting info.
       Calculate this size, then convert it into a block size (units of pool headers).
    
       Note that ix cannot overflow (past POOL_LISTS_PER_PAGE) because any such request would've been treated
       as a POOL_MAX_ALLOC earlier and resulted in the direct allocation of pages.
    */
    ix = (USHORT)((NumberOfBytes + sizeof(POOL_HEADER) + (POOL_BLOCK_SIZE - 1)) / POOL_BLOCK_SIZE);
    ASSERT(ix < POOL_LISTS_PER_PAGE);

    /* Handle lookaside list optimization for both paged and nonpaged pool */
    if (ix <= NUMBER_POOL_LOOKASIDE_LISTS)
    {
        /* Try popping it from the per-CPU lookaside list */
        LookasideList = (PoolType == PagedPool) ?
                         Prcb->PPPagedLookasideList[ix - 1].P :
                         Prcb->PPNPagedLookasideList[ix - 1].P;

        LookasideList->TotalAllocates++;

        Entry = (PPOOL_HEADER)InterlockedPopEntrySList(&LookasideList->ListHead);
        if (!Entry)
        {
            /* We failed, try popping it from the global list */
            LookasideList = (PoolType == PagedPool) ?
                             Prcb->PPPagedLookasideList[ix - 1].L :
                             Prcb->PPNPagedLookasideList[ix - 1].L;

            LookasideList->TotalAllocates++;
            Entry = (PPOOL_HEADER)InterlockedPopEntrySList(&LookasideList->ListHead);
        }

        /* If we were able to pop it, update the accounting and return the block */
        if (Entry)
        {
            LookasideList->AllocateHits++;

            /* Get the real entry, write down its pool type, and track it */
            Entry--;
            Entry->PoolType = (OriginalType + 1);
            ExpInsertPoolTracker(Tag, Entry->BlockSize * POOL_BLOCK_SIZE, OriginalType);

            /* Return the pool allocation */
            Entry->PoolTag = Tag;

            (POOL_FREE_BLOCK(Entry))->Flink = NULL;
            (POOL_FREE_BLOCK(Entry))->Blink = NULL;

            return POOL_FREE_BLOCK(Entry);
        }
    }

    /* Loop in the free lists looking for a block if this size.
       Start with the list optimized for this kind of size lookup
    */
    ListHead = &PoolDesc->ListHeads[ix];
    do
    {
        /* Are there any free entries available on this list? */
        if (!ExpIsPoolListEmpty(ListHead))
        {
            /* Acquire the pool lock now */
            OldIrql = ExLockPool(PoolDesc);

            /* And make sure the list still has entries */
            if (ExpIsPoolListEmpty(ListHead))
            {
                /* Someone raced us (and won) before we had a chance to acquire the lock.
                   Try again!
                */
                ExUnlockPool(PoolDesc, OldIrql);
                continue;
            }

            /* Remove a free entry from the list.
               Note that due to the way we insert free blocks into multiple lists there is a guarantee
               that any block on this list will either be of the correct size, or perhaps larger.
            */
            ExpCheckPoolLinks(ListHead);
            Entry = POOL_ENTRY(ExpRemovePoolHeadList(ListHead));
            ExpCheckPoolLinks(ListHead);
            ExpCheckPoolBlocks(Entry);

            ASSERT(Entry->BlockSize >= ix);
            ASSERT(Entry->PoolType == 0);

            /* Check if this block is larger that what we need.
               The block could not possibly be smaller, due to the reason explained above
               (and we would've asserted on a checked build if this was the case).
            */
            if (Entry->BlockSize != ix)
            {
                /* Is there an entry before this one? */
                if (Entry->PreviousSize == 0)
                {
                    /* There isn't anyone before us, so take the next block and turn it into a fragment
                       that contains the leftover data that we don't need to satisfy the caller's request
                    */
                    FragmentEntry = POOL_BLOCK(Entry, ix);
                    FragmentEntry->BlockSize = Entry->BlockSize - ix;

                    /* And make it point back to us */
                    FragmentEntry->PreviousSize = ix;

                    /* Now get the block that follows the new fragment
                       and check if it's still on the same page as us (and not at the end)
                    */
                    NextEntry = POOL_NEXT_BLOCK(FragmentEntry);
                    if (PAGE_ALIGN(NextEntry) != NextEntry)
                        /* Adjust this next block to point to our newly created fragment block */
                        NextEntry->PreviousSize = FragmentEntry->BlockSize;
                }
                else
                {
                    /* There is a free entry before us,
                       which we know is smaller so we'll make this entry the fragment instead
                    */
                    FragmentEntry = Entry;

                    /* And then we'll remove from it the actual size required.
                       Now the entry is a leftover free fragment.
                    */
                    Entry->BlockSize -= ix;

                    /* Now let's go to the next entry after the fragment (which used to point to our original free entry)
                       and make it reference the new fragment entry instead.

                       This is the entry that will actually end up holding the allocation!
                    */
                    Entry = POOL_NEXT_BLOCK(Entry);
                    Entry->PreviousSize = FragmentEntry->BlockSize;

                    /* And now let's go to the entry after that one
                       and check if it's still on the same page, and not at the end
                    */
                    NextEntry = POOL_BLOCK(Entry, ix);

                    if (PAGE_ALIGN(NextEntry) != NextEntry)
                        /* Make it reference the allocation entry */
                        NextEntry->PreviousSize = ix;
                }

                /* Now our (allocation) entry is the right size */
                Entry->BlockSize = ix;

                /* And the next entry is now the free fragment which contains the remaining difference
                   between how big the original entry was, and the actual size the caller needs/requested.
                */
                FragmentEntry->PoolType = 0;
                BlockSize = FragmentEntry->BlockSize;

                /* Now check if enough free bytes remained for us to have a "full" entry, which contains enough bytes
                   for a linked list and thus can be used for allocations (up to 8 bytes...)
                */
                ExpCheckPoolLinks(&PoolDesc->ListHeads[BlockSize - 1]);
                if (BlockSize != 1)
                {
                    /* Insert the free entry into the free list for this size */
                    ExpInsertPoolTailList(&PoolDesc->ListHeads[BlockSize - 1], POOL_FREE_BLOCK(FragmentEntry));
                    ExpCheckPoolLinks(POOL_FREE_BLOCK(FragmentEntry));
                }
            }

            /* We have found an entry for this allocation,
               so set the pool type and release the lock since we're done
            */
            Entry->PoolType = (OriginalType + 1);
            ExpCheckPoolBlocks(Entry);
            ExUnlockPool(PoolDesc, OldIrql);

            /* Increment required counters */
            InterlockedExchangeAddSizeT(&PoolDesc->TotalBytes, (Entry->BlockSize * POOL_BLOCK_SIZE));
            InterlockedIncrement((PLONG)&PoolDesc->RunningAllocs);

            /* Track this allocation */
            ExpInsertPoolTracker(Tag, (Entry->BlockSize * POOL_BLOCK_SIZE), OriginalType);

            /* Return the pool allocation */
            Entry->PoolTag = Tag;

            (POOL_FREE_BLOCK(Entry))->Flink = NULL;
            (POOL_FREE_BLOCK(Entry))->Blink = NULL;

            return POOL_FREE_BLOCK(Entry);
        }
    }
    while (++ListHead != &PoolDesc->ListHeads[POOL_LISTS_PER_PAGE]);

    /* There were no free entries left, so we have to allocate a new fresh page */
    Entry = MiAllocatePoolPages(OriginalType, PAGE_SIZE);
    if (!Entry)
    {
#if DBG
        /* Out of memory, display current consumption.
           Let's consider that if the caller wanted more than a hundred pages,
           that's a bogus caller and we are not out of memory.
           Dump at most once a second to avoid spamming the log.
        */
        if (NumberOfBytes < (100 * PAGE_SIZE) &&
            KeQueryInterruptTime() >= (MiLastPoolDumpTime + 10000000))
        {
            MiDumpPoolConsumers(FALSE, 0, 0, 0);
            MiLastPoolDumpTime = KeQueryInterruptTime();
        }
#endif

        /* Must succeed pool is deprecated, but still supported.
           These allocation failures must cause an immediate bugcheck
        */
        if (OriginalType & MUST_SUCCEED_POOL_MASK)
        {
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY,
                         PAGE_SIZE,
                         NonPagedPoolDescriptor.TotalPages,
                         NonPagedPoolDescriptor.TotalBigPages,
                         0);
        }

        /* Internal debugging */
        ExPoolFailures++;

        /* This flag requests printing failures, and can also further specify breaking on failures */
        if (ExpPoolFlags & POOL_FLAG_DBGPRINT_ON_FAILURE)
        {
            DPRINT1("EX: ExAllocatePool (%lu, 0x%x) returning NULL\n", NumberOfBytes, OriginalType);
            if (ExpPoolFlags & POOL_FLAG_CRASH_ON_FAILURE)
                DbgBreakPoint();
        }

        /* Finally, this flag requests an exception, which we are more than happy to raise! */
        if (OriginalType & POOL_RAISE_IF_ALLOCATION_FAILURE)
            ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

        /* Return NULL to the caller in all other cases */
        return NULL;
    }

    /* Setup the entry data */
    Entry->Ulong1 = 0;
    Entry->BlockSize = ix;
    Entry->PoolType = OriginalType + 1;

    /* This page will have two entries -- one for the allocation (which we just created above),
       and one for the remaining free bytes, which we're about to create now.
       The free bytes are the whole page minus what was allocated and then converted into units of block headers.
    */
    BlockSize = ((PAGE_SIZE / POOL_BLOCK_SIZE) - ix);

    FragmentEntry = POOL_BLOCK(Entry, ix);
    FragmentEntry->Ulong1 = 0;
    FragmentEntry->BlockSize = BlockSize;
    FragmentEntry->PreviousSize = ix;

    /* Increment required counters */
    InterlockedIncrement((PLONG)&PoolDesc->TotalPages);
    InterlockedExchangeAddSizeT(&PoolDesc->TotalBytes, (Entry->BlockSize * POOL_BLOCK_SIZE));

    /* Now check if enough free bytes remained for us to have a "full" entry,
       which contains enough bytes for a linked list and thus can be used for allocations (up to 8 bytes...)
    */
    if (FragmentEntry->BlockSize != 1)
    {
        /* Excellent -- acquire the pool lock */
        OldIrql = ExLockPool(PoolDesc);

        /* And insert the free entry into the free list for this block size */
        ExpCheckPoolLinks(&PoolDesc->ListHeads[BlockSize - 1]);
        ExpInsertPoolTailList(&PoolDesc->ListHeads[BlockSize - 1], POOL_FREE_BLOCK(FragmentEntry));
        ExpCheckPoolLinks(POOL_FREE_BLOCK(FragmentEntry));

        /* Release the pool lock */
        ExpCheckPoolBlocks(Entry);
        ExUnlockPool(PoolDesc, OldIrql);
    }
    else
    {
        /* Simply do a sanity check */
        ExpCheckPoolBlocks(Entry);
    }

    /* Increment performance counters and track this allocation */
    InterlockedIncrement((PLONG)&PoolDesc->RunningAllocs);
    ExpInsertPoolTracker(Tag, (Entry->BlockSize * POOL_BLOCK_SIZE), OriginalType);

    /* And return the pool allocation */
    ExpCheckPoolBlocks(Entry);
    Entry->PoolTag = Tag;

    return POOL_FREE_BLOCK(Entry);
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
