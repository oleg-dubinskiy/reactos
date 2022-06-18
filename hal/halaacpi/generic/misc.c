
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS  *******************************************************************/


/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalpOpenRegistryKey(
    _In_ PHANDLE KeyHandle,
    _In_ HANDLE RootKey,
    _In_ PUNICODE_STRING KeyName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Disposition;
    NTSTATUS Status;

    /* Setup the attributes we received */
    InitializeObjectAttributes(&ObjectAttributes, KeyName, OBJ_CASE_INSENSITIVE, RootKey, NULL);

    /* What to do? */
    if (!Create)
    {
        /* Open the key */
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
        return Status;
    }

    /* Create the key */
    Status = ZwCreateKey(KeyHandle, DesiredAccess, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, &Disposition);
    return Status;
}

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

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    // Not implemented on x86
    return;
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
NTAPI
HalHandleNMI(
    _In_ PVOID NmiInfo)
{
    UNIMPLEMENTED;
    ASSERT(FALSE); // HalpDbgBreakPointEx();

    /* Freeze the system */
    while (TRUE);
}

/* EOF */
