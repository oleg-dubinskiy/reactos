
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
HalpReboot(VOID)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    UCHAR Data;
    PVOID ZeroPageMapping;

    DPRINT("HalpReboot()\n");

    /* Map the first physical page */
    PhysicalAddress.QuadPart = 0;
    ZeroPageMapping = HalpMapPhysicalMemoryWriteThrough64(PhysicalAddress, 1);

    /* Enable warm reboot */
    ((PUSHORT)ZeroPageMapping)[0x239] = 0x1234;

    /* Lock CMOS Access (and disable interrupts) */
    HalpAcquireCmosSpinLock();

    /* Setup control register B */
    WRITE_PORT_UCHAR(CMOS_CONTROL_PORT, 0x0B);
    KeStallExecutionProcessor(1);

    /* Read periodic register and clear the interrupt enable */
    Data = READ_PORT_UCHAR(CMOS_DATA_PORT);
    KeStallExecutionProcessor(1);
    WRITE_PORT_UCHAR(CMOS_DATA_PORT, Data & ~0x40);
    KeStallExecutionProcessor(1);

    /* Setup control register A */
    WRITE_PORT_UCHAR(CMOS_CONTROL_PORT, 0x0A);
    KeStallExecutionProcessor(1);

    /* Read divider rate and reset it */
    Data = READ_PORT_UCHAR(CMOS_DATA_PORT);
    KeStallExecutionProcessor(1);
    WRITE_PORT_UCHAR(CMOS_DATA_PORT, (Data & ~0x9) | 0x06);
    KeStallExecutionProcessor(1);

    /* Reset neutral CMOS address */
    WRITE_PORT_UCHAR(CMOS_CONTROL_PORT, 0x15);
    KeStallExecutionProcessor(1);

    /* Flush write buffers and send the reset command */
    KeFlushWriteBuffer();

    DPRINT("HalpReboot: FIXME HalpResetAllProcessors()\n");
    //HalpResetAllProcessors();

    HalpWriteResetCommand();

    /* Halt the CPU */
    __halt();
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalReturnToFirmware(IN FIRMWARE_REENTRY Action)
{
    DPRINT("HalReturnToFirmware: Action %X\n", Action);

    /* Check what kind of action this is */
    switch (Action)
    {
        /* All recognized actions */
        case HalHaltRoutine:
        case HalPowerDownRoutine:
        case HalRestartRoutine:
        case HalRebootRoutine:
            /* Call the internal reboot function */
            HalpReboot();

        default:
            DPRINT1("HalReturnToFirmware: Unknown Action %X\n", Action);
            ASSERT(FALSE); // HalpDbgBreakPointEx();
    }
}

/* EOF */
