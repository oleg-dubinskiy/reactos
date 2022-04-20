
#pragma once

/* DMA Page Register Structure
   080     DMA        RESERVED
   081     DMA        Page Register (channel 2)
   082     DMA        Page Register (channel 3)
   083     DMA        Page Register (channel 1)
   084     DMA        RESERVED
   085     DMA        RESERVED
   086     DMA        RESERVED
   087     DMA        Page Register (channel 0)
   088     DMA        RESERVED
   089     PS/2-DMA   Page Register (channel 6)
   08A     PS/2-DMA   Page Register (channel 7)
   08B     PS/2-DMA   Page Register (channel 5)
   08C     PS/2-DMA   RESERVED
   08D     PS/2-DMA   RESERVED
   08E     PS/2-DMA   RESERVED
   08F     PS/2-DMA   Page Register (channel 4)
*/
typedef struct _DMA_PAGE
{
   UCHAR Reserved1;
   UCHAR Channel2;
   UCHAR Channel3;
   UCHAR Channel1;
   UCHAR Reserved2[3];
   UCHAR Channel0;
   UCHAR Reserved3;
   UCHAR Channel6;
   UCHAR Channel7;
   UCHAR Channel5;
   UCHAR Reserved4[3];
   UCHAR Channel4;
} DMA_PAGE, *PDMA_PAGE;

/* Channel Stop Registers for each Channel */
typedef struct _DMA_CHANNEL_STOP
{
   UCHAR ChannelLow;
   UCHAR ChannelMid;
   UCHAR ChannelHigh;
   UCHAR Reserved;
} DMA_CHANNEL_STOP, *PDMA_CHANNEL_STOP;

typedef struct _DMA1_ADDRESS_COUNT
{
   UCHAR DmaBaseAddress;
   UCHAR DmaBaseCount;
} DMA1_ADDRESS_COUNT, *PDMA1_ADDRESS_COUNT;

typedef struct _DMA2_ADDRESS_COUNT
{
   UCHAR DmaBaseAddress;
   UCHAR Reserved1;
   UCHAR DmaBaseCount;
   UCHAR Reserved2;
} DMA2_ADDRESS_COUNT, *PDMA2_ADDRESS_COUNT;

typedef struct _DMA1_CONTROL
{
   DMA1_ADDRESS_COUNT DmaAddressCount[4];
   UCHAR DmaStatus;
   UCHAR DmaRequest;
   UCHAR SingleMask;
   UCHAR Mode;
   UCHAR ClearBytePointer;
   UCHAR MasterClear;
   UCHAR ClearMask;
   UCHAR AllMask;
} DMA1_CONTROL, *PDMA1_CONTROL;

typedef struct _DMA2_CONTROL
{
   DMA2_ADDRESS_COUNT DmaAddressCount[4];
   UCHAR DmaStatus;
   UCHAR Reserved1;
   UCHAR DmaRequest;
   UCHAR Reserved2;
   UCHAR SingleMask;
   UCHAR Reserved3;
   UCHAR Mode;
   UCHAR Reserved4;
   UCHAR ClearBytePointer;
   UCHAR Reserved5;
   UCHAR MasterClear;
   UCHAR Reserved6;
   UCHAR ClearMask;
   UCHAR Reserved7;
   UCHAR AllMask;
   UCHAR Reserved8;
} DMA2_CONTROL, *PDMA2_CONTROL;

/* This structure defines the I/O Map of the 82537 controller. */
typedef struct _EISA_CONTROL
{
   /* DMA Controller 1 */
   DMA1_CONTROL DmaController1;         /* 00h-0Fh */
   UCHAR Reserved1[16];                 /* 0Fh-1Fh */

   /* Interrupt Controller 1 (PIC) */
   UCHAR Pic1Operation;                 /* 20h     */
   UCHAR Pic1Interrupt;                 /* 21h     */
   UCHAR Reserved2[30];                 /* 22h-3Fh */

   /* Timer */
   UCHAR TimerCounter;                  /* 40h     */
   UCHAR TimerMemoryRefresh;            /* 41h     */
   UCHAR Speaker;                       /* 42h     */
   UCHAR TimerOperation;                /* 43h     */
   UCHAR TimerMisc;                     /* 44h     */
   UCHAR Reserved3[2];                  /* 45-46h  */
   UCHAR TimerCounterControl;           /* 47h     */
   UCHAR TimerFailSafeCounter;          /* 48h     */
   UCHAR Reserved4;                     /* 49h     */
   UCHAR TimerCounter2;                 /* 4Ah     */
   UCHAR TimerOperation2;               /* 4Bh     */
   UCHAR Reserved5[20];                 /* 4Ch-5Fh */

   /* NMI / Keyboard / RTC */
   UCHAR Keyboard;                      /* 60h     */
   UCHAR NmiStatus;                     /* 61h     */
   UCHAR Reserved6[14];                 /* 62h-6Fh */
   UCHAR NmiEnable;                     /* 70h     */
   UCHAR Reserved7[15];                 /* 71h-7Fh */

   /* DMA Page Registers Controller 1 */
   DMA_PAGE DmaController1Pages;        /* 80h-8Fh */
   UCHAR Reserved8[16];                 /* 90h-9Fh */

    /* Interrupt Controller 2 (PIC) */
   UCHAR Pic2Operation;                 /* 0A0h      */
   UCHAR Pic2Interrupt;                 /* 0A1h      */
   UCHAR Reserved9[30];                 /* 0A2h-0BFh */

   /* DMA Controller 2 */
   DMA1_CONTROL DmaController2;         /* 0C0h-0CFh */

   /* System Reserved Ports */
   UCHAR SystemReserved[816];           /* 0D0h-3FFh */

   /* Extended DMA Registers, Controller 1 */
   UCHAR DmaHighByteCount1[8];          /* 400h-407h */
   UCHAR Reserved10[2];                 /* 408h-409h */
   UCHAR DmaChainMode1;                 /* 40Ah      */
   UCHAR DmaExtendedMode1;              /* 40Bh      */
   UCHAR DmaBufferControl;              /* 40Ch      */
   UCHAR Reserved11[84];                /* 40Dh-460h */
   UCHAR ExtendedNmiControl;            /* 461h      */
   UCHAR NmiCommand;                    /* 462h      */
   UCHAR Reserved12;                    /* 463h      */
   UCHAR BusMaster;                     /* 464h      */
   UCHAR Reserved13[27];                /* 465h-47Fh */

   /* DMA Page Registers Controller 2 */
   DMA_PAGE DmaController2Pages;        /* 480h-48Fh */
   UCHAR Reserved14[48];                /* 490h-4BFh */

   /* Extended DMA Registers, Controller 2 */
   UCHAR DmaHighByteCount2[16];         /* 4C0h-4CFh */

   /* Edge/Level Control Registers */
   UCHAR Pic1EdgeLevel;                 /* 4D0h      */
   UCHAR Pic2EdgeLevel;                 /* 4D1h      */
   UCHAR Reserved15[2];                 /* 4D2h-4D3h */

   /* Extended DMA Registers, Controller 2 */
   UCHAR DmaChainMode2;                 /* 4D4h      */
   UCHAR Reserved16;                    /* 4D5h      */
   UCHAR DmaExtendedMode2;              /* 4D6h      */
   UCHAR Reserved17[9];                 /* 4D7h-4DFh */

   /* DMA Stop Registers */
   DMA_CHANNEL_STOP DmaChannelStop[8];  /* 4E0h-4FFh */
} EISA_CONTROL, *PEISA_CONTROL;

PDMA_ADAPTER NTAPI
HalpGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescription,
    OUT PULONG NumberOfMapRegisters
);

typedef struct _HALP_DMA_MASTER_ADAPTER
{
    PADAPTER_OBJECT MasterAdapter;
    ULONG MapBufferMaxPages;
    ULONG MapBufferSize;
    ULONG Unknown1;
    PHYSICAL_ADDRESS MapBufferPhysicalAddress;
    ULONG Unknown2;
    ULONG Unknown3;
} HALP_DMA_MASTER_ADAPTER, *PHALP_DMA_MASTER_ADAPTER;

/* EOF */
