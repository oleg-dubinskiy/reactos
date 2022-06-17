
#pragma once

/* I/O APIC Register Address Map */

#define IOAPIC_VER      0x0001  /* IO APIC Version (R) */

#define SET_IOAPIC_ID(x)  ((x) << 24)

#define IOAPIC_MRE_MASK        (0xFF << 16)  /* Maximum Redirection Entry */
#define GET_IOAPIC_MRE(x)      (((x) & IOAPIC_MRE_MASK) >> 16)

#define IOAPIC_TBL_DELMOD  (0x7 << 8)  /* Delivery Mode (see APIC_DM_*) */
#define IOAPIC_TBL_IM      (0x1 << 16) /* Interrupt Mask */
#define IOAPIC_TBL_VECTOR  (0xFF << 0) /* Vector (10h - FEh) */

/* Delivery Modes */
#define IOAPIC_DM_SMI      (0x2 << 8)

#define IOAPIC_SIZE          0x400

FORCEINLINE
ULONG
IoApicRead(
    _In_ PIO_APIC_REGISTERS IoApicRegs,
    _In_ UCHAR RegisterIdx)
{
    ULONG Result;

    WRITE_REGISTER_UCHAR(((PUCHAR)IoApicRegs + IOAPIC_IOREGSEL), RegisterIdx);
    Result = READ_REGISTER_ULONG((PULONG)((ULONG_PTR)IoApicRegs + IOAPIC_IOWIN));

    //DbgPrint("IoApicRead: RegisterIdx %X, Result %X\n", RegisterIdx, Result);
    return Result;
}

FORCEINLINE
VOID
IoApicWrite(
    _In_ PIO_APIC_REGISTERS IoApicRegs,
    _In_ UCHAR RegisterIdx,
    _In_ ULONG Value)
{
    //DbgPrint("IoApicWrite: RegisterIdx %X, Value %X\n", RegisterIdx, Value);

    WRITE_REGISTER_UCHAR(((PUCHAR)IoApicRegs + IOAPIC_IOREGSEL), RegisterIdx);
    WRITE_REGISTER_ULONG((PULONG)((ULONG_PTR)IoApicRegs + IOAPIC_IOWIN), Value);
}

/* EOF */
