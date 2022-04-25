
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS  *******************************************************************/


/* PRIVATE FUNCTIONS **********************************************************/

VOID
NTAPI
HalpFlushTLB(VOID)
{
    ULONG_PTR Flags, Cr4;
    INT CpuInfo[4];
    ULONG_PTR PageDirectory;

    /* Disable interrupts */
    Flags = __readeflags();
    _disable();

    /* Get page table directory base */
    PageDirectory = __readcr3();

    /* Check for CPUID support */
    if (KeGetCurrentPrcb()->CpuID)
    {
        /* Check for global bit in CPU features */
        __cpuid(CpuInfo, 1);

        if (CpuInfo[3] & 0x2000)
        {
            /* Get current CR4 value */
            Cr4 = __readcr4();

            /* Disable global bit */
            __writecr4(Cr4 & ~CR4_PGE);

            /* Flush TLB and re-enable global bit */
            __writecr3(PageDirectory);
            __writecr4(Cr4);

            /* Restore interrupts */
            __writeeflags(Flags);
            return;
        }
    }

    /* Legacy: just flush TLB */
    __writecr3(PageDirectory);
    __writeeflags(Flags);
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalHandleNMI(IN PVOID NmiInfo)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();

    /* Freeze the system */
    while (TRUE);
}


/* EOF */
