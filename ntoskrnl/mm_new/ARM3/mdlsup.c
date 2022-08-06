
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "miarm.h"

/* GLOBALS ********************************************************************/

ULONG MiCacheOverride[MiNotMapped + 1];
BOOLEAN MmTrackPtes;
BOOLEAN MmTrackLockedPages;

extern MI_PFN_CACHE_ATTRIBUTE MiPlatformCacheAttributes[2][MmMaximumCacheType];
extern PMMPTE MmSystemPtesStart[MaximumPtePoolTypes];
extern PMMPTE MmSystemPtesEnd[MaximumPtePoolTypes];

/* FUNCTIONS ******************************************************************/

PVOID
NTAPI
MiMapLockedPagesInUserSpace(
    _In_ PMDL Mdl,
    _In_ PVOID StartVa,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_opt_ PVOID BaseAddress)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    TABLE_SEARCH_RESULT Result;
    MI_PFN_CACHE_ATTRIBUTE CacheAttribute;
    MI_PFN_CACHE_ATTRIBUTE EffectiveCacheAttribute;
    BOOLEAN IsIoMapping;
    KIRQL OldIrql;
    ULONG_PTR StartingVa;
    ULONG_PTR EndingVa;
    PMMADDRESS_NODE Parent;
    PMMVAD_LONG Vad;
    ULONG NumberOfPages;
    PMMPTE Pte;
    PMMPDE Pde;
    MMPTE TempPte;
    PPFN_NUMBER MdlPages;
    PMMPFN Pfn;
    BOOLEAN AddressSpaceLocked = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    DPRINT("MiMapLockedPagesInUserSpace(%p, %p, %X, %p)\n", Mdl, StartVa, CacheType, BaseAddress);

    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(StartVa, MmGetMdlByteCount(Mdl));
    MdlPages = MmGetMdlPfnArray(Mdl);

    ASSERT(CacheType <= MmWriteCombined);

    IsIoMapping = ((Mdl->MdlFlags & MDL_IO_SPACE) != 0);
    CacheAttribute = MiPlatformCacheAttributes[IsIoMapping][CacheType];

    /* Large pages are always cached, make sure we're not asking for those */
    if (CacheAttribute != MiCached)
    {
        DPRINT1("MiMapLockedPagesInUserSpace: FIXME! Need to check for large pages\n");
    }

    /* Allocate a VAD for our mapped region */
    Vad = ExAllocatePoolWithTag(NonPagedPool, sizeof(MMVAD_LONG), 'ldaV');
    if (!Vad)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Error;
    }

    /* Initialize PhysicalMemory VAD */
    RtlZeroMemory(Vad, sizeof(*Vad));

    Vad->u2.VadFlags2.LongVad = 1;
    Vad->u.VadFlags.VadType = VadDevicePhysicalMemory;
    Vad->u.VadFlags.Protection = MM_READWRITE;
    Vad->u.VadFlags.PrivateMemory = 1;

    /* Did the caller specify an address? */
    if (!BaseAddress)
    {
        /* We get to pick the address */
        MmLockAddressSpace(&Process->Vm);
        AddressSpaceLocked = TRUE;

        if (Process->VmDeleted)
        {
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto Error;
        }

        Result = MiFindEmptyAddressRangeInTree((NumberOfPages * PAGE_SIZE),
                                               MM_VIRTMEM_GRANULARITY,
                                               &Process->VadRoot,
                                               &Parent,
                                               &StartingVa);
        if (Result == TableFoundNode)
        {
            Status = STATUS_NO_MEMORY;
            goto Error;
        }

        EndingVa = (StartingVa + (NumberOfPages * PAGE_SIZE) - 1);
        BaseAddress = (PVOID)StartingVa;
    }
    else
    {
        /* Caller specified a base address */
        StartingVa = (ULONG_PTR)BaseAddress;
        EndingVa = (StartingVa + (NumberOfPages * PAGE_SIZE) - 1);

        /* Make sure it's valid */
        if (BYTE_OFFSET(StartingVa) ||
            EndingVa <= StartingVa ||
            EndingVa > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        {
            Status = STATUS_INVALID_ADDRESS;
            goto Error;
        }

        MmLockAddressSpace(&Process->Vm);
        AddressSpaceLocked = TRUE;

        if (Process->VmDeleted)
        {
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto Error;
        }

        DPRINT1("MiMapLockedPagesInUserSpace: FIXME\n");
        ASSERT(FALSE);
    }

    Vad->StartingVpn = (StartingVa / PAGE_SIZE);
    Vad->EndingVpn = (EndingVa / PAGE_SIZE);

    MiLockProcessWorkingSetUnsafe(Process, Thread);

    ASSERT(Vad->EndingVpn >= Vad->StartingVpn);

    MiInsertVad((PMMVAD)Vad, &Process->VadRoot);

    /* Check if this is uncached */
    if (CacheAttribute != MiCached)
    {
        /* Flush all caches */
        KeFlushEntireTb(TRUE, TRUE);
        KeInvalidateAllCaches();
    }

    Pte = MiAddressToPte(BaseAddress);

    while (NumberOfPages && *MdlPages != LIST_HEAD)
    {
        Pde = MiPteToPde(Pte);
        MiMakePdeExistAndMakeValid(Pde, Process, MM_NOIRQL);

        ASSERT(Pte->u.Hard.Valid == 0);

        /* Add a PDE reference for each page */
        MiIncrementPageTableReferences(BaseAddress);

        DPRINT1("MiMapLockedPagesInUserSpace: [%X] Address %p, RefCount %X\n",
                MiAddressToPdeOffset(BaseAddress), BaseAddress, MiQueryPageTableReferences(BaseAddress));

        /* Set up our basic user PTE */
        MI_MAKE_HARDWARE_PTE_USER(&TempPte, Pte, MM_READWRITE, *MdlPages);

        EffectiveCacheAttribute = CacheAttribute;

        /* We need to respect the PFN's caching information in some cases */
        Pfn = MiGetPfnEntry(*MdlPages);
        if (Pfn)
        {
            ASSERT(Pfn->u3.e2.ReferenceCount != 0);

            switch (Pfn->u3.e1.CacheAttribute)
            {
                case MiNonCached:
                    if (CacheAttribute != MiNonCached)
                    {
                        MiCacheOverride[1]++;
                        EffectiveCacheAttribute = MiNonCached;
                    }
                    break;

                case MiCached:
                    if (CacheAttribute != MiCached)
                    {
                        MiCacheOverride[0]++;
                        EffectiveCacheAttribute = MiCached;
                    }
                    break;

                case MiWriteCombined:
                    if (CacheAttribute != MiWriteCombined)
                    {
                        MiCacheOverride[2]++;
                        EffectiveCacheAttribute = MiWriteCombined;
                    }
                    break;

                default:
                    /* We don't support AWE magic (MiNotMapped) */
                    DPRINT1("MiMapLockedPagesInUserSpace: FIXME! MiNotMapped is not supported\n");
                    ASSERT(FALSE);
                    break;
            }
        }

        /* Configure caching */
        switch (EffectiveCacheAttribute)
        {
            case MiNonCached:
                MI_PAGE_DISABLE_CACHE(&TempPte);
                MI_PAGE_WRITE_THROUGH(&TempPte);
                break;

            case MiCached:
                break;

            case MiWriteCombined:
                MI_PAGE_DISABLE_CACHE(&TempPte);
                MI_PAGE_WRITE_COMBINED(&TempPte);
                break;

            default:
                ASSERT(FALSE);
                break;
        }

        /* Make the page valid */
        MI_WRITE_VALID_PTE(Pte, TempPte);

        /* Acquire a share count */
        Pfn = MI_PFN_ELEMENT(Pde->u.Hard.PageFrameNumber);

        OldIrql = MiLockPfnDb(APC_LEVEL);
        Pfn->u2.ShareCount++;
        MiUnlockPfnDb(OldIrql, APC_LEVEL);

        /* Next page */
        MdlPages++;
        Pte++;
        NumberOfPages--;
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE);
    }

    MiUnlockProcessWorkingSetUnsafe(Process, Thread);
    ASSERT(AddressSpaceLocked);
    MmUnlockAddressSpace(&Process->Vm);

    ASSERT(StartingVa != 0);
    return (PVOID)((ULONG_PTR)StartingVa + MmGetMdlByteOffset(Mdl));

Error:

    if (AddressSpaceLocked)
        MmUnlockAddressSpace(&Process->Vm);

    if (Vad)
        ExFreePoolWithTag(Vad, 'ldaV');

    ExRaiseStatus(Status);
}

VOID
NTAPI
MiUnmapLockedPagesInUserSpace(
    _In_ PVOID BaseAddress,
    _In_ PMDL Mdl)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    PMMVAD Vad;
    PMMPTE Pte;
    PMMPDE Pde;
    KIRQL OldIrql;
    ULONG NumberOfPages;
    PPFN_NUMBER MdlPages;
    PFN_NUMBER PageTablePage;

    DPRINT("MiUnmapLockedPagesInUserSpace: %p, %p\n", BaseAddress, Mdl);

    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), MmGetMdlByteCount(Mdl));
    ASSERT(NumberOfPages != 0);

    MdlPages = MmGetMdlPfnArray(Mdl);

    /* Find the VAD */
    MmLockAddressSpace(&Process->Vm);

    Vad = MiLocateAddress(BaseAddress);
    if (!Vad || Vad->u.VadFlags.VadType != VadDevicePhysicalMemory)
    {
        DPRINT1("MiUnmapLockedPagesInUserSpace: invalid for %p\n", BaseAddress);
        MmUnlockAddressSpace(&Process->Vm);
        return;
    }

    MiLockProcessWorkingSetUnsafe(Process, Thread);

    /* Remove it from the process VAD tree */
    ASSERT(Process->VadRoot.NumberGenericTableElements >= 1);
    MiRemoveNode((PMMADDRESS_NODE)Vad, &Process->VadRoot);

    /* MiRemoveNode should have removed us if we were the hint */
    ASSERT(Process->VadRoot.NodeHint != Vad);

    Pte = MiAddressToPte(BaseAddress);
    OldIrql = MiLockPfnDb(APC_LEVEL);

    while (NumberOfPages && *MdlPages != LIST_HEAD)
    {
        ASSERT(MiAddressToPte(Pte)->u.Hard.Valid == 1);
        ASSERT(Pte->u.Hard.Valid == 1);

        /* Dereference the page */
        MiDecrementPageTableReferences(BaseAddress);

        DPRINT1("MiUnmapLockedPagesInUserSpace: [%X] Address %p, RefCount %X\n",
                MiAddressToPdeOffset(BaseAddress), BaseAddress, MiQueryPageTableReferences(BaseAddress));

        /* Invalidate it */
        MI_ERASE_PTE(Pte);

        /* We invalidated this PTE, so dereference the PDE */
        Pde = MiAddressToPde(BaseAddress);
        PageTablePage = Pde->u.Hard.PageFrameNumber;

        MiDecrementShareCount(MiGetPfnEntry(PageTablePage), PageTablePage);

        /* Next page */
        Pte++;
        NumberOfPages--;
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress + PAGE_SIZE);
        MdlPages++;

        /* Moving to a new PDE? */
        if (Pde != MiAddressToPde(BaseAddress))
        {
            /* See if we should delete it */
            KeFlushProcessTb();

            Pde = MiPteToPde(Pte - 1);
            ASSERT(Pde->u.Hard.Valid == 1);

            if (!MiQueryPageTableReferences(BaseAddress))
            {
                ASSERT(Pde->u.Long != 0);

                DPRINT1("MiUnmapLockedPagesInUserSpace: FIXME\n");
                ASSERT(FALSE);
                //MiDeletePte(Pde, MiPteToAddress(Pde), Process, NULL);
            }
        }
    }

    KeFlushProcessTb();

    MiUnlockPfnDb(OldIrql, APC_LEVEL);
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);
    MmUnlockAddressSpace(&Process->Vm);

    ExFreePoolWithTag(Vad, 'ldaV');
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
MmAdvanceMdl(
    _In_ PMDL Mdl,
    _In_ ULONG NumberOfBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

PMDL
NTAPI
MmAllocatePagesForMdl(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PMDL
NTAPI
MmAllocatePagesForMdlEx(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG Flags)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmBuildMdlForNonPagedPool(
    _In_ PMDL Mdl)
{
    PVOID Base;
    PMMPTE Pte;
    PPFN_NUMBER MdlPages;
    PPFN_NUMBER EndPage;
    PFN_NUMBER Pfn;
    PFN_NUMBER PageCount;

    /* Sanity checks */
    ASSERT(Mdl->ByteCount != 0);
    ASSERT((Mdl->MdlFlags & (MDL_PAGES_LOCKED | MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL | MDL_PARTIAL)) == 0);

    /* We know the MDL isn't associated to a process now */
    Mdl->Process = NULL;

    /* Get page and VA information */
    MdlPages = (PPFN_NUMBER)(Mdl + 1);
    Base = Mdl->StartVa;

    /* Set the system address and now get the page count */
    Mdl->MappedSystemVa = (PVOID)((ULONG_PTR)Base + Mdl->ByteOffset);

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Mdl->MappedSystemVa, Mdl->ByteCount);
    ASSERT(PageCount != 0);

    /* Loop the PTEs */
    Pte = MiAddressToPte(Base);
    EndPage = (MdlPages + PageCount);

    do
    {
        /* Write the PFN */
        Pfn = PFN_FROM_PTE(Pte++);
        *MdlPages++ = Pfn;
    }
    while (MdlPages < EndPage);

    /* Set the nonpaged pool flag */
    Mdl->MdlFlags |= MDL_SOURCE_IS_NONPAGED_POOL;

    /* Check if this is an I/O mapping */
    if (!MiGetPfnEntry(Pfn))
        Mdl->MdlFlags |= MDL_IO_SPACE;
}

PMDL
NTAPI
MmCreateMdl(
    _In_ PMDL Mdl,
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmFreePagesFromMdl(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

PVOID
NTAPI
MmMapLockedPages(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmMapLockedPagesSpecifyCache(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ PVOID BaseAddress,
    _In_ ULONG BugCheckOnFailure,
    _In_ MM_PAGE_PRIORITY Priority)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

PVOID
NTAPI
MmMapLockedPagesWithReservedMapping(
    _In_ PVOID MappingAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL MemoryDescriptorList,
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    UNIMPLEMENTED_DBGBREAK();
    return NULL;
}

VOID
NTAPI
MmMapMemoryDumpMdl(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmPrefetchPages(
    _In_ ULONG NumberOfLists,
    _In_ PREAD_LIST* ReadLists)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
MmProbeAndLockPages(
    _In_ PMDL Mdl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmProbeAndLockProcessPages(
    _Inout_ PMDL MemoryDescriptorList,
    _In_ PEPROCESS Process,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmProbeAndLockSelectedPages(
    _Inout_ PMDL MemoryDescriptorList,
    _In_ LARGE_INTEGER PageList[],
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    UNIMPLEMENTED_DBGBREAK();
}

NTSTATUS
NTAPI
MmProtectMdlSystemAddress(
    _In_ PMDL MemoryDescriptorList,
    _In_ ULONG NewProtect)
{
    UNIMPLEMENTED_DBGBREAK();
    return STATUS_NOT_IMPLEMENTED;
}

SIZE_T
NTAPI
MmSizeOfMdl(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    UNIMPLEMENTED_DBGBREAK();
    return 0;
}

VOID
NTAPI
MmUnlockPages(
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmUnmapLockedPages(
    _In_ PVOID BaseAddress,
    _In_ PMDL Mdl)
{
    UNIMPLEMENTED_DBGBREAK();
}

VOID
NTAPI
MmUnmapReservedMapping(
    _In_ PVOID BaseAddress,
    _In_ ULONG PoolTag,
    _In_ PMDL MemoryDescriptorList)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
