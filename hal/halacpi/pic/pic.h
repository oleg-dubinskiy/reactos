
#pragma once

/* See ISA System Architecture 3rd Edition (Tom Shanley, Don Anderson, John Swindle) P. 396, 397
   These ports are controlled by the i8259 Programmable Interrupt Controller (PIC)
*/
#define PIC1_CONTROL_PORT      (PUCHAR)0x0020
#define PIC1_DATA_PORT         (PUCHAR)0x0021
#define PIC2_CONTROL_PORT      (PUCHAR)0x00A0
#define PIC2_DATA_PORT         (PUCHAR)0x00A1

/* Definitions for ICW/OCW Bits */
typedef enum _I8259_ICW1_OPERATING_MODE
{
    Cascade,
    Single
} I8259_ICW1_OPERATING_MODE;

typedef enum _I8259_ICW1_INTERRUPT_MODE
{
    EdgeTriggered,
    LevelTriggered
} I8259_ICW1_INTERRUPT_MODE;

typedef enum _I8259_ICW1_INTERVAL
{
    Interval8,
    Interval4
} I8259_ICW1_INTERVAL;

typedef enum _I8259_ICW4_SYSTEM_MODE
{
    Mcs8085Mode,
    New8086Mode
} I8259_ICW4_SYSTEM_MODE;

typedef enum _I8259_ICW4_EOI_MODE
{
    NormalEoi,
    AutomaticEoi
} I8259_ICW4_EOI_MODE;

typedef enum _I8259_ICW4_BUFFERED_MODE
{
    NonBuffered,
    NonBuffered2,
    BufferedSlave,
    BufferedMaster
} I8259_ICW4_BUFFERED_MODE;

#if 0
typedef enum _I8259_READ_REQUEST
{
    InvalidRequest,
    InvalidRequest2,
    ReadIdr,
    ReadIsr
} I8259_READ_REQUEST;

typedef enum _I8259_EOI_MODE
{
    RotateAutoEoiClear,
    NonSpecificEoi,
    InvalidEoiMode,
    SpecificEoi,
    RotateAutoEoiSet,
    RotateNonSpecific,
    SetPriority,
    RotateSpecific
} I8259_EOI_MODE;
#endif

/* Definitions for ICW Registers */
typedef union _I8259_ICW1
{
    struct
    {
        UCHAR NeedIcw4:1;
        UCHAR OperatingMode:1;
        UCHAR Interval:1;
        UCHAR InterruptMode:1;
        UCHAR Init:1;
        UCHAR InterruptVectorAddress:3;
    };
    UCHAR Bits;
} I8259_ICW1, *PI8259_ICW1;

typedef union _I8259_ICW2
{
    struct
    {
        UCHAR Sbz:3;
        UCHAR InterruptVector:5;
    };
    UCHAR Bits;
} I8259_ICW2, *PI8259_ICW2;

typedef union _I8259_ICW3
{
    union
    {
        struct
        {
            UCHAR SlaveIrq0:1;
            UCHAR SlaveIrq1:1;
            UCHAR SlaveIrq2:1;
            UCHAR SlaveIrq3:1;
            UCHAR SlaveIrq4:1;
            UCHAR SlaveIrq5:1;
            UCHAR SlaveIrq6:1;
            UCHAR SlaveIrq7:1;
        };
        struct
        {
            UCHAR SlaveId:3;
            UCHAR Reserved:5;
        };
    };
    UCHAR Bits;
} I8259_ICW3, *PI8259_ICW3;

typedef union _I8259_ICW4
{
    struct
    {
        UCHAR SystemMode:1;
        UCHAR EoiMode:1;
        UCHAR BufferedMode:2;
        UCHAR SpecialFullyNestedMode:1;
        UCHAR Reserved:3;
    };
    UCHAR Bits;
} I8259_ICW4, *PI8259_ICW4;

#if 0
typedef union _I8259_OCW2
{
    struct
    {
        UCHAR IrqNumber:3;
        UCHAR Sbz:2;
        UCHAR EoiMode:3;
    };
    UCHAR Bits;
} I8259_OCW2, *PI8259_OCW2;

typedef union _I8259_OCW3
{
    struct
    {
        UCHAR ReadRequest:2;
        UCHAR PollCommand:1;
        UCHAR Sbo:1;
        UCHAR Sbz:1;
        UCHAR SpecialMaskMode:2;
        UCHAR Reserved:1;
    };
    UCHAR Bits;
} I8259_OCW3, *PI8259_OCW3;

typedef union _I8259_ISR
{
    union
    {
        struct
        {
            UCHAR Irq0:1;
            UCHAR Irq1:1;
            UCHAR Irq2:1;
            UCHAR Irq3:1;
            UCHAR Irq4:1;
            UCHAR Irq5:1;
            UCHAR Irq6:1;
            UCHAR Irq7:1;
        };
    };
    UCHAR Bits;
} I8259_ISR, *PI8259_ISR;

typedef I8259_ISR I8259_IDR, *PI8259_IDR;
#endif

/* EOF */
#if 0
#endif

