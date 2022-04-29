#pragma once

/* INCLUDES ******************************************************************/

/* Helper Macros */
#define PASTE2(x,y)     x ## y
#define POINTER_TO_(x)  PASTE2(P,x)
#define READ_FROM(x)    PASTE2(READ_PORT_, x)
#define WRITE_TO(x)     PASTE2(WRITE_PORT_, x)

/* Declares a PCI Register Read/Write Routine */
#define TYPE_DEFINE(x, y)                                               \
    ULONG                                                               \
    NTAPI                                                               \
    x(                                                                  \
        IN PPCIPBUSDATA BusData,                                        \
        IN y PciCfg,                                                    \
        IN PUCHAR Buffer,                                               \
        IN ULONG Offset                                                 \
    )
#define TYPE1_DEFINE(x) TYPE_DEFINE(x, PPCI_TYPE1_CFG_BITS);
#define TYPE2_DEFINE(x) TYPE_DEFINE(x, PPCI_TYPE2_ADDRESS_BITS);

/* Defines a PCI Register Read/Write Type 1 Routine Prologue and Epilogue */
#define TYPE1_START(x, y)                                               \
    TYPE_DEFINE(x, PPCI_TYPE1_CFG_BITS)                                 \
{                                                                       \
    ULONG i = Offset % sizeof(ULONG);                                   \
    PciCfg->u.bits.RegisterNumber = Offset / sizeof(ULONG);             \
    WRITE_PORT_ULONG(BusData->Config.Type1.Address, PciCfg->u.AsULONG);
#define TYPE1_END(y)                                                    \
    return sizeof(y); }
#define TYPE2_END       TYPE1_END

/* PCI Register Read Type 1 Routine */
#define TYPE1_READ(x, y)                                                       \
    TYPE1_START(x, y)                                                          \
    *((POINTER_TO_(y))Buffer) =                                                \
    READ_FROM(y)((POINTER_TO_(y))(ULONG_PTR)(BusData->Config.Type1.Data + i)); \
    TYPE1_END(y)

/* PCI Register Write Type 1 Routine */
#define TYPE1_WRITE(x, y)                                                      \
    TYPE1_START(x, y)                                                          \
    WRITE_TO(y)((POINTER_TO_(y))(ULONG_PTR)(BusData->Config.Type1.Data + i),   \
                *((POINTER_TO_(y))Buffer));                                    \
    TYPE1_END(y)

typedef NTSTATUS
(NTAPI * PciIrqRange)(
    IN PBUS_HANDLER BusHandler,
    IN PBUS_HANDLER RootHandler,
    IN PCI_SLOT_NUMBER PciSlot,
    OUT PSUPPORTED_RANGE * Interrupt
);

typedef struct _PCIPBUSDATA
{
    PCIBUSDATA CommonData;
    union
    {
        struct
        {
            PULONG Address;
            ULONG Data;
        } Type1;
        struct
        {
            PUCHAR CSE;
            PUCHAR Forward;
            ULONG Base;
        } Type2;
    } Config;
    ULONG MaxDevice;
    PciIrqRange GetIrqRange;
    BOOLEAN BridgeConfigRead;
    UCHAR ParentBus;
    UCHAR Subtractive;
    UCHAR reserved[1];
    UCHAR SwizzleIn[4];
    RTL_BITMAP DeviceConfigured;
    ULONG ConfiguredBits[PCI_MAX_DEVICES * PCI_MAX_FUNCTION / 32];
} PCIPBUSDATA, *PPCIPBUSDATA;

typedef ULONG
(NTAPI * FncConfigIO)(
    IN PPCIPBUSDATA BusData,
    IN PVOID State,
    IN PUCHAR Buffer,
    IN ULONG Offset
);

typedef VOID
(NTAPI * FncSync)(
    IN PBUS_HANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    IN PKIRQL Irql,
    IN PVOID State
);

typedef VOID
(NTAPI * FncReleaseSync)(
    IN PBUS_HANDLER BusHandler,
    IN KIRQL Irql
);

typedef struct _PCI_CONFIG_HANDLER
{
    FncSync Synchronize;
    FncReleaseSync ReleaseSynchronzation;
    FncConfigIO ConfigRead[3];
    FncConfigIO ConfigWrite[3];
} PCI_CONFIG_HANDLER, *PPCI_CONFIG_HANDLER;

typedef struct _PCI_REGISTRY_INFO_INTERNAL
{
    UCHAR MajorRevision;
    UCHAR MinorRevision;
    UCHAR NoBuses; // Number Of Buses
    UCHAR HardwareMechanism;
    ULONG ElementCount;
    PCI_CARD_DESCRIPTOR CardList[ANYSIZE_ARRAY];
} PCI_REGISTRY_INFO_INTERNAL, *PPCI_REGISTRY_INFO_INTERNAL;

typedef struct _PCI_TYPE1_CFG_BITS
{
    union
    {
        struct
        {
            ULONG Reserved1:2;
            ULONG RegisterNumber:6;
            ULONG FunctionNumber:3;
            ULONG DeviceNumber:5;
            ULONG BusNumber:8;
            ULONG Reserved2:7;
            ULONG Enable:1;
        } bits;

        ULONG AsULONG;
    } u;
} PCI_TYPE1_CFG_BITS, *PPCI_TYPE1_CFG_BITS;


/* FUNCTIONS *****************************************************************/

PPCI_REGISTRY_INFO_INTERNAL
NTAPI
HalpQueryPciRegistryInfo(
    VOID
);

VOID
NTAPI
HalpPCISynchronizeType1(
    IN PBUS_HANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    IN PKIRQL Irql,
    IN PPCI_TYPE1_CFG_BITS PciCfg
);

VOID
NTAPI
HalpPCIReleaseSynchronzationType1(
    IN PBUS_HANDLER BusHandler,
    IN KIRQL Irql
);

TYPE1_DEFINE(HalpPCIReadUcharType1);
TYPE1_DEFINE(HalpPCIReadUshortType1);
TYPE1_DEFINE(HalpPCIReadUlongType1);

TYPE1_DEFINE(HalpPCIWriteUcharType1);
TYPE1_DEFINE(HalpPCIWriteUshortType1);
TYPE1_DEFINE(HalpPCIWriteUlongType1);

/* EOF */
