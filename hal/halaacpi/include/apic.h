
#pragma once

#include "apic.h"

#define MAX_IOAPICS      64
#define MAX_CPUS         32
#define MAX_INT_VECTORS  256
#define MAX_INTI         (32 * MAX_IOAPICS)

typedef struct _HALP_MP_INFO_TABLE
{
    ULONG LocalApicversion;
    ULONG ProcessorCount;
    ULONG ActiveProcessorCount;
    ULONG Reserved1;
    ULONG IoApicCount;
    ULONG Reserved2;
    ULONG Reserved3;
    BOOLEAN ImcrPresent;              // When the IMCR presence bit is set, the IMCR is present and PIC Mode is implemented; otherwise, Virtual Wire Mode is implemented.
    UCHAR Pad[3];
    ULONG LocalApicPA;                // The 32-bit physical address at which each processor can access its local interrupt controller
    ULONG IoApicVA[MAX_IOAPICS];
    ULONG IoApicPA[MAX_IOAPICS];
    ULONG IoApicIrqBase[MAX_IOAPICS]; // Global system interrupt base 

} HALP_MP_INFO_TABLE, *PHALP_MP_INFO_TABLE;

typedef struct _IO_APIC_REGISTERS
{
    volatile ULONG IoRegisterSelect;
    volatile ULONG Reserved[3];
    volatile ULONG IoWindow;

} IO_APIC_REGISTERS, *PIO_APIC_REGISTERS;

typedef union _APIC_INTI_INFO
{
    struct
    {
        UCHAR Enabled      :1;
        UCHAR Type         :3;
        UCHAR TriggerMode  :2;
        UCHAR Polarity     :2;
        UCHAR Destinations;
        USHORT Entry;
    };
    ULONG AsULONG;

} APIC_INTI_INFO, *PAPIC_INTI_INFO;

/* apic.c */
INIT_FUNCTION
BOOLEAN
NTAPI 
DetectAcpiMP(
    _Out_ PBOOLEAN OutIsMpSystem,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
);

/* apicacpi.c */
INIT_FUNCTION
VOID
NTAPI 
HalpInitMpInfo(
    _In_ PACPI_TABLE_MADT ApicTable,
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
);

/* EOF */
