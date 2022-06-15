
/* INCLUDES ******************************************************************/

#include <hal.h>
#include "dispatch.h"

//#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/


/* PRIVATE FUNCTIONS *********************************************************/

NTSTATUS
NTAPI
HaliInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE* PmHalDispatchTable)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

/* PM DISPATCH FUNCTIONS *****************************************************/


/* EOF */
