
#pragma once

#include "apic.h"

#define MAX_IOAPICS      64
#define MAX_CPUS         32
#define MAX_INT_VECTORS  256
#define MAX_INTI         (32 * MAX_IOAPICS)

#ifdef _M_AMD64
  #define LOCAL_APIC_BASE  0xFFFFFFFFFFFE0000ULL // checkme!
#else
  #define LOCAL_APIC_BASE  0xFFFE0000
#endif

/* APIC Register Address Map */
#define APIC_ID       0x0020 /* Local APIC ID Register (R/W) */
#define APIC_VER      0x0030 /* Local APIC Version Register (R) */
#define APIC_TPR      0x0080 /* Task Priority Register (R/W) */
#define APIC_APR      0x0090 /* Arbitration Priority Register (R) */
#define APIC_PPR      0x00A0 /* Processor Priority Register (R) */
#define APIC_EOI      0x00B0 /* EOI Register (W) */
#define APIC_RRR      0x00C0 /* Remote Read Register () */
#define APIC_LDR      0x00D0 /* Logical Destination Register (R/W) */
#define APIC_DFR      0x00E0 /* Destination Format Register (0-27 R, 28-31 R/W) */
#define APIC_SIVR     0x00F0 /* Spurious Interrupt Vector Register (0-3 R, 4-9 R/W) */
#define APIC_ISR      0x0100 /* Interrupt Service Register 0-255 (R) */
#define APIC_TMR      0x0180 /* Trigger Mode Register 0-255 (R) */
#define APIC_IRR      0x0200 /* Interrupt Request Register 0-255 (r) */
#define APIC_ESR      0x0280 /* Error Status Register (R) */
#define APIC_ICR0     0x0300 /* Interrupt Command Register 0-31 (R/W) */
#define APIC_ICR1     0x0310 /* Interrupt Command Register 32-63 (R/W) */
#define APIC_TMRLVTR  0x0320 /* Timer Local Vector Table (R/W) */
#define	APIC_THRMLVTR 0x0330 /* Thermal Local Vector Table */
#define APIC_PCLVTR   0x0340 /* Performance Counter Local Vector Table (R/W) */
#define APIC_LINT0    0x0350 /* LINT0 Local Vector Table (R/W) */
#define APIC_LINT1    0x0360 /* LINT1 Local Vector Table (R/W) */
#define APIC_ERRLVTR  0x0370 /* Error Local Vector Table (R/W) */
#define APIC_TICR     0x0380 /* Initial Count Register for Timer (R/W) */
#define APIC_TCCR     0x0390 /* Current Count Register for Timer (R) */
#define APIC_TDCR     0x03E0 /* Timer Divide Configuration Register (R/W) */
#define APIC_EAFR     0x0400 /* extended APIC Feature register (R/W) */
#define APIC_EACR     0x0410 /* Extended APIC Control Register (R/W) */
#define APIC_SEOI     0x0420 /* Specific End Of Interrupt Register (W) */
#define APIC_EXT0LVTR 0x0500 /* Extended Interrupt 0 Local Vector Table */
#define APIC_EXT1LVTR 0x0510 /* Extended Interrupt 1 Local Vector Table */
#define APIC_EXT2LVTR 0x0520 /* Extended Interrupt 2 Local Vector Table */
#define APIC_EXT3LVTR 0x0530 /* Extended Interrupt 3 Local Vector Table */

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

FORCEINLINE
ULONG
ApicRead(ULONG Offset)
{
    return *(volatile ULONG *)(APIC_BASE + Offset);
}

FORCEINLINE
VOID
ApicWrite(ULONG Offset, ULONG Value)
{
    *(volatile ULONG *)(APIC_BASE + Offset) = Value;
}

/* apic.c */
INIT_FUNCTION
BOOLEAN
NTAPI 
DetectAcpiMP(
    _Out_ PBOOLEAN OutIsMpSystem,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock
);

INIT_FUNCTION
VOID
NTAPI
HalInitApicInterruptHandlers(
    VOID
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

/* apictrap.S */
VOID __cdecl PicSpuriousService37(VOID);
VOID __cdecl ApicSpuriousService(VOID);

/* EOF */
