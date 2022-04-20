
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

ULONG HalpFeatureBits;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpRemoveFences()
{
    DPRINT1("HalpRemoveFences FIXME!\n");
    ASSERT(FALSE);//HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    DPRINT1("HalAllProcessorsStarted: HalpFeatureBits %X\n", HalpFeatureBits);

    if (HalpFeatureBits & 2) {
        HalpRemoveFences();
    }

    return TRUE;
}


/* EOF */
