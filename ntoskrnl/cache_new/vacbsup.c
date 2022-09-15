
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include "cc.h"
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PVACB CcVacbs;
PVACB CcBeyondVacbs;
LIST_ENTRY CcVacbLru;
LIST_ENTRY CcVacbFreeList;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
CcInitializeVacbs(VOID)
{
    PVACB CurrentVacb;
    ULONG CcNumberVacbs;
    ULONG SizeOfVacbs;

    CcNumberVacbs = ((MmSizeOfSystemCacheInPages / (VACB_MAPPING_GRANULARITY / PAGE_SIZE)) - 2);
    SizeOfVacbs = (CcNumberVacbs * sizeof(VACB));

    DPRINT("CcInitializeVacbs: MmSizeOfSystemCacheInPages %X, CcNumberVacbs %X\n",
           MmSizeOfSystemCacheInPages, CcNumberVacbs);

    CcVacbs = ExAllocatePoolWithTag(NonPagedPool, SizeOfVacbs, 'aVcC');
    if (!CcVacbs)
    {
        DPRINT1("CcInitializeVacbs: allocate VACBs failed\n");
        return;
    }

    RtlZeroMemory(CcVacbs, SizeOfVacbs);

    CcBeyondVacbs = &CcVacbs[CcNumberVacbs];

    InitializeListHead(&CcVacbLru);
    InitializeListHead(&CcVacbFreeList);

    for (CurrentVacb = CcVacbs; CurrentVacb < CcBeyondVacbs; CurrentVacb++)
        InsertTailList(&CcVacbFreeList, &CurrentVacb->LruList);
}

NTSTATUS
NTAPI
CcCreateVacbArray(
    _In_ PSHARED_CACHE_MAP SharedMap,
    _In_ LARGE_INTEGER AllocationSize)
{
    PVACB* NewVacbs;
    ULONG NewSize;

    DPRINT("CcCreateVacbArray: SharedMap %p AllocationSize %I64X\n", SharedMap, AllocationSize.QuadPart);

    if ((ULONGLONG)AllocationSize.QuadPart >= (4ull * _1TB))
    {
        DPRINT1("CcCreateVacbArray: STATUS_SECTION_TOO_BIG\n");
        return STATUS_SECTION_TOO_BIG;
    }

    if ((ULONGLONG)AllocationSize.QuadPart >= (4ull * _1GB))
    {
        NewSize = 0xFFFFFFFF;
        DPRINT("CcCreateVacbArray: NewSize %X\n", NewSize);
    }
    else if (AllocationSize.LowPart <= (VACB_MAPPING_GRANULARITY * sizeof(PVACB)))
    {
        NewSize = (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB));
        DPRINT("CcCreateVacbArray: NewSize %X\n", NewSize);
    }
    else
    {
        NewSize = ((AllocationSize.LowPart / VACB_MAPPING_GRANULARITY) * sizeof(PVACB));
        DPRINT("CcCreateVacbArray: NewSize %X\n", NewSize);
    }


    if (NewSize == (CC_DEFAULT_NUMBER_OF_VACBS * sizeof(PVACB)))
    {
        NewVacbs = SharedMap->InitialVacbs;
    }
    else
    {
        DPRINT1("CcCreateVacbArray: FIXME! NewSize %X\n", NewSize);
        ASSERT(FALSE);
    }

    RtlZeroMemory(NewVacbs, NewSize);

    SharedMap->SectionSize.QuadPart = AllocationSize.QuadPart;
    SharedMap->Vacbs = NewVacbs;

    return STATUS_SUCCESS;
}

/* EOF */
