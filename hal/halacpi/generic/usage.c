
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#if defined(ALLOC_PRAGMA) && !defined(_MINIHAL_)
  #pragma alloc_text(INIT, HalpRegisterVector)
  #pragma alloc_text(INIT, HalpReportResourceUsage)
#endif

/* GLOBALS ********************************************************************/

PUCHAR KdComPortInUse;

IDTUsageFlags HalpIDTUsageFlags[MAXIMUM_IDTVECTOR + 1];
IDTUsage HalpIDTUsage[MAXIMUM_IDTVECTOR + 1];

extern KAFFINITY HalpActiveProcessors;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
HalpRegisterVector(IN UCHAR Flags,
                   IN ULONG BusVector,
                   IN ULONG SystemVector,
                   IN KIRQL Irql)
{
    DPRINT("HalpRegisterVector: %X, %X, %X, %X\n", Flags, BusVector, SystemVector, Irql);

    /* Save the vector flags */
    HalpIDTUsageFlags[SystemVector].Flags = Flags;

    /* Save the vector data */
    HalpIDTUsage[SystemVector].Irql  = Irql;
    HalpIDTUsage[SystemVector].BusReleativeVector = (UCHAR)BusVector;
}

INIT_FUNCTION
VOID
NTAPI
HalpBuildPartialFromIdt(IN ULONG Entry,
                        IN PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDescriptor,
                        IN PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedDescriptor)
{
    /* Exclusive interrupt entry */
    RawDescriptor->Type = CmResourceTypeInterrupt;
    RawDescriptor->ShareDisposition = CmResourceShareDriverExclusive;

    /* Check the interrupt type */
    if (HalpIDTUsageFlags[Entry].Flags & IDT_LATCHED)
        RawDescriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
    else
        RawDescriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;

    /* Get vector and level from IDT usage */
    RawDescriptor->u.Interrupt.Vector = HalpIDTUsage[Entry].BusReleativeVector;
    RawDescriptor->u.Interrupt.Level = HalpIDTUsage[Entry].BusReleativeVector;

    /* Affinity is all the CPUs */
    RawDescriptor->u.Interrupt.Affinity = HalpActiveProcessors;

    /* The translated copy is identical */
    RtlCopyMemory(TranslatedDescriptor, RawDescriptor, sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

    /* But the vector and IRQL must be set correctly */
    TranslatedDescriptor->u.Interrupt.Vector = Entry;
    TranslatedDescriptor->u.Interrupt.Level = HalpIDTUsage[Entry].Irql;
}

INIT_FUNCTION
VOID
NTAPI
HalpReportResourceUsage(IN PUNICODE_STRING HalName,
                        IN INTERFACE_TYPE InterfaceType)
{
    DPRINT1("HalpReportResourceUsage: %wZ Detected\n", HalName);
    ASSERT(0);// HalpDbgBreakPointEx();
}

/* EOF */

