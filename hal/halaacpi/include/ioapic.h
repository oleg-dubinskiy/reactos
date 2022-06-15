
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

/* EOF */
