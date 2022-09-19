
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
//#include "miarm.h"

/* GLOBALS ********************************************************************/

KEVENT MmZeroingPageEvent;

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
MiFreeInitializationCode(
    _In_ PVOID InitStart,
    _In_ PVOID InitEnd)
{
    PMMPTE Pte;
    PFN_NUMBER PagesFreed;

    DPRINT("MiFreeInitializationCode: ... \n");

    /* Get the start PTE */
    Pte = MiAddressToPte(InitStart);

    if (MI_IS_PHYSICAL_ADDRESS(InitStart))
    {
        DPRINT1("MiFreeInitializationCode: FIXME\n");
        ASSERT(MI_IS_PHYSICAL_ADDRESS(InitStart) == FALSE);
    }

    /*  Compute the number of pages we expect to free */
    PagesFreed = (PFN_NUMBER)(MiAddressToPte(InitEnd) - Pte);

    /* Try to actually free them */
    PagesFreed = MiDeleteSystemPageableVm(Pte, PagesFreed, 0, NULL);
}

VOID
NTAPI
MmZeroPageThread(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
