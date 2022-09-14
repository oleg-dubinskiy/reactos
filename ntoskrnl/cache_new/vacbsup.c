
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

/* EOF */
