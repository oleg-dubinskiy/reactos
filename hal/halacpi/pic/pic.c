
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* IRQL MANAGEMENT ************************************************************/

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    /* Return the IRQL */
    return KeGetPcr()->Irql;
}

KIRQL
FASTCALL
KfRaiseIrql(IN KIRQL NewIrql)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL CurrentIrql;

    /* Read current IRQL */
    CurrentIrql = Pcr->Irql;

#if DBG
    /* Validate correct raise */
    if (CurrentIrql > NewIrql)
    {
        /* Crash system */
        Pcr->Irql = PASSIVE_LEVEL;
        KeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
    }
#endif

    /* Set new IRQL */
    Pcr->Irql = NewIrql;

    /* Return old IRQL */
    return CurrentIrql;
}


/* FUNCTIONS ******************************************************************/


/* EOF */
