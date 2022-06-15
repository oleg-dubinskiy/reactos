
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>
#include "apic.h"

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/

VOID
FASTCALL
HalpClockInterruptStubHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    UCHAR CmosData;

    /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);

    /* Read register C, so that the next interrupt can happen */
    CmosData = HalpReadCmos(RTC_REGISTER_C);
    CmosData = HalpReadCmos(RTC_REGISTER_C);

    while (CmosData & RTC_REG_C_IRQ)
    {
        CmosData = HalpReadCmos(RTC_REGISTER_C);
    }

    ApicWrite(APIC_EOI, 0);

  #ifdef __REACTOS__
    KiEoiHelper(TrapFrame);
  #else
    #error FIXME call Kei386EoiHelper()
  #endif
}

/* EOF */
