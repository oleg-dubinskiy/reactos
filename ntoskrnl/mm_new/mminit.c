
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include "ARM3/miarm.h"

/* GLOBALS ********************************************************************/

PMMSUPPORT MmKernelAddressSpace;
PMMPTE MmSharedUserDataPte;

static GENERIC_MAPPING MmpSectionMapping =
{
    STANDARD_RIGHTS_READ | SECTION_MAP_READ | SECTION_QUERY,
    STANDARD_RIGHTS_WRITE | SECTION_MAP_WRITE,
    STANDARD_RIGHTS_EXECUTE | SECTION_MAP_EXECUTE,
    SECTION_ALL_ACCESS
};

extern PVOID MmNonPagedSystemStart;
extern PVOID MmNonPagedPoolStart;
extern PVOID MmNonPagedPoolExpansionStart;
extern PVOID MmSystemCacheStart;
extern PVOID MmSystemCacheEnd;
extern PVOID MiSystemViewStart;
extern SIZE_T MmBootImageSize;
extern SIZE_T MmSystemViewSize;
extern SIZE_T MmSizeOfNonPagedPoolInBytes;
extern SIZE_T MmSizeOfPagedPoolInBytes;
extern ULONG_PTR MxPfnAllocation;

extern MM_AVL_TABLE MmSectionBasedRoot;
extern KGUARDED_MUTEX MmSectionBasedMutex;
extern PMMPTE MmSharedUserDataPte;
extern MMPTE ValidKernelPte;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MiDbgDumpAddressSpace(VOID)
{
    ULONG_PTR _BootImageEnd        = ((ULONG_PTR)KSEG0_BASE                   +            MmBootImageSize);
    SIZE_T    _PfnDatabaseSize     = (MxPfnAllocation << PAGE_SHIFT);
    ULONG_PTR _PfnDatabaseEnd      = ((ULONG_PTR)MmPfnDatabase                +           _PfnDatabaseSize);
    ULONG_PTR _NPagedPoolEnd       = ((ULONG_PTR)MmNonPagedPoolStart          +            MmSizeOfNonPagedPoolInBytes);
    ULONG_PTR _SystemViewEnd       = ((ULONG_PTR)MiSystemViewStart            +            MmSystemViewSize);
    SIZE_T    _SessionSize         = ((ULONG_PTR)MiSessionSpaceEnd            - (ULONG_PTR)MmSessionBase);
    SIZE_T    _PageTablesSize      = ((ULONG_PTR)PTE_TOP                      - (ULONG_PTR)PTE_BASE);
    SIZE_T    _PageDirectoriesSize = ((ULONG_PTR)PDE_TOP                      - (ULONG_PTR)PDE_BASE);
    SIZE_T    _HyperspaceSize      = ((ULONG_PTR)HYPER_SPACE_END              - (ULONG_PTR)HYPER_SPACE);
    SIZE_T    _SystemCacheSize     = ((ULONG_PTR)MmSystemCacheEnd             - (ULONG_PTR)MmSystemCacheStart);
    ULONG_PTR _PagedPoolEnd        = ((ULONG_PTR)MmPagedPoolStart             +            MmSizeOfPagedPoolInBytes);
    SIZE_T    _SystemPteSize       = ((ULONG_PTR)MmNonPagedPoolExpansionStart - (ULONG_PTR)MmNonPagedSystemStart);
    SIZE_T    _NPagedPoolExtSize   = ((ULONG_PTR)MmNonPagedPoolEnd            - (ULONG_PTR)MmNonPagedPoolExpansionStart);

#ifdef _M_AMD64

    DPRINT1("MiDbgDumpAddressSpace: FIXME _M_AMD64\n");
    ASSERT(FALSE);

#else /* _X86_ */

/* Print the memory layout */

DPRINT1("%p - %p (%X) %s\n", KSEG0_BASE,          _BootImageEnd,       MmBootImageSize,             "Boot Loaded Image");
DPRINT1("%p - %p (%X) %s\n", MmPfnDatabase,       _PfnDatabaseEnd,    _PfnDatabaseSize,             "PFN Database");
DPRINT1("%p - %p (%X) %s\n", MmNonPagedPoolStart, _NPagedPoolEnd,      MmSizeOfNonPagedPoolInBytes, "ARM3 Non Paged Pool");
DPRINT1("%p - %p (%X) %s\n", MiSystemViewStart,   _SystemViewEnd,      MmSystemViewSize,            "System View Space");
DPRINT1("%p - %p (%X) %s\n", MmSessionBase,        MiSessionSpaceEnd, _SessionSize,                 "Session Space");
DPRINT1("%p - %p (%X) %s\n", PTE_BASE,             PTE_TOP,           _PageTablesSize,              "Page Tables");
DPRINT1("%p - %p (%X) %s\n", PDE_BASE,             PDE_TOP,           _PageDirectoriesSize,         "Page Directories");
DPRINT1("%p - %p (%X) %s\n", HYPER_SPACE,          HYPER_SPACE_END,   _HyperspaceSize,              "Hyperspace");
DPRINT1("%p - %p (%X) %s\n", MmSystemCacheStart,   MmSystemCacheEnd,  _SystemCacheSize,             "System Cache");
DPRINT1("%p - %p (%X) %s\n", MmPagedPoolStart,    _PagedPoolEnd,       MmSizeOfPagedPoolInBytes,    "ARM3 Paged Pool");

DPRINT1("%p - %p (%X) %s\n", MmNonPagedSystemStart, MmNonPagedPoolExpansionStart, _SystemPteSize,   "System PTE Space");
DPRINT1("%p - %p (%X) %s\n", MmNonPagedPoolExpansionStart, MmNonPagedPoolEnd, _NPagedPoolExtSize,   "Non Paged Pool Expansion PTE Space");

#endif

}

VOID
NTAPI
MmpDeleteSection(
    _In_ PVOID ObjectBody)
{
    PSECTION Section = ObjectBody;
    //PCONTROL_AREA ControlArea;

    DPRINT("MmpDeleteSection: Section %p\n", Section);

    if (!Section->Segment)
    {
        DPRINT("MmpDeleteSection: Segment is NULL\n");
        return;
    }

    DPRINT1("MmpDeleteSection: FIXME! Section %p\n", Section);
    ASSERT(FALSE);
#if 0
    ControlArea = Section->Segment->ControlArea;

    if (Section->Address.StartingVpn)
    {
        KeAcquireGuardedMutex(&MmSectionBasedMutex);
        MiRemoveNode(&Section->Address, &MmSectionBasedRoot);
        KeReleaseGuardedMutex(&MmSectionBasedMutex);
    }

    if (Section->u.Flags.UserWritable &&
        !ControlArea->u.Flags.Image &&
        ControlArea->FilePointer)
    {
        ASSERT(Section->InitialPageProtection & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
        InterlockedDecrement((PLONG)&ControlArea->WritableUserReferences);
    }

    MiDereferenceControlAreaBySection(ControlArea, Section->u.Flags.UserReference);
#endif
}

VOID
NTAPI
MmpCloseSection(
    _In_ PEPROCESS Process OPTIONAL,
    _In_ PVOID Object,
    _In_ ACCESS_MASK GrantedAccess,
    _In_ ULONG ProcessHandleCount,
    _In_ ULONG SystemHandleCount)
{
    DPRINT("MmpCloseSection(OB %p, HC %lu)\n", Object, ProcessHandleCount);
}

INIT_FUNCTION
NTSTATUS
NTAPI
MmCreatePhysicalMemorySection(VOID)
{
    UNICODE_STRING PhysicalMemoryName = RTL_CONSTANT_STRING(L"\\Device\\PhysicalMemory");
    OBJECT_ATTRIBUTES ObjectAttributes;
    PCONTROL_AREA ControlArea;
    PSECTION PhysSection;
    PSEGMENT Segment;
    HANDLE Handle;
    NTSTATUS Status;

    DPRINT("MmCreatePhysicalMemorySection()\n");

    /* Create the section mapping physical memory */
    Segment = ExAllocatePoolWithTag(PagedPool, sizeof(*Segment), 'gSmM');
    if (!Segment)
    {
        DPRINT1("MmCreatePhysicalMemorySection: allocate failed!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ControlArea = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ControlArea), 'aCmM');
    if (!ControlArea)
    {
        DPRINT1("MmCreatePhysicalMemorySection: allocate failed!\n");
        ExFreePoolWithTag(Segment, 'gSmM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Segment, sizeof(*Segment));
    RtlZeroMemory(ControlArea, sizeof(*ControlArea));

    ControlArea->Segment = Segment;
    ControlArea->NumberOfSectionReferences = 1;
    ControlArea->u.Flags.PhysicalMemory = 1;

    Segment->ControlArea = ControlArea;
    Segment->SegmentPteTemplate.u.Long = 0;

    InitializeObjectAttributes(&ObjectAttributes,
                               &PhysicalMemoryName,
                               (OBJ_KERNEL_EXCLUSIVE | OBJ_PERMANENT),
                               NULL,
                               NULL);

    Status = ObCreateObject(KernelMode,
                            MmSectionObjectType,
                            &ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(*PhysSection),
                            sizeof(*PhysSection),
                            0,
                            (PVOID *)&PhysSection);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmCreatePhysicalMemorySection: Status %X\n", Status);
        ExFreePoolWithTag(ControlArea, 'aCmM');
        ExFreePoolWithTag(Segment, 'gSmM');
        return Status;
    }

  #ifdef _WIN64
    #error FIXME
  #else
    PhysSection->SizeOfSection.QuadPart = 0xFFFFFFFF;
  #endif

    PhysSection->Segment = Segment;
    PhysSection->u.LongFlags = 0;
    PhysSection->InitialPageProtection = PAGE_EXECUTE_READWRITE;

    Status = ObInsertObject(PhysSection,
                            NULL,
                            SECTION_MAP_READ,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmCreatePhysicalMemorySection: Status %X\n", Status);
        return Status;
    }

    Status = NtClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmCreatePhysicalMemorySection: Status %X\n", Status);
    }

    return Status;
}

INIT_FUNCTION
BOOLEAN
NTAPI
MmInitSystem(_In_ ULONG Phase,
             _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNIMPLEMENTED_DBGBREAK();
    return FALSE;
}

/* EOF */
