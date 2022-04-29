
#pragma once

/*
    Mode 0 - Interrupt On Terminal Count
    Mode 1 - Hardware Re-triggerable One-Shot
    Mode 2 - Rate Generator
    Mode 3 - Square Wave Generator
    Mode 4 - Software Triggered Strobe
    Mode 5 - Hardware Triggered Strobe
*/
typedef enum _TIMER_OPERATING_MODES
{
    PitOperatingMode0,
    PitOperatingMode1,
    PitOperatingMode2,
    PitOperatingMode3,
    PitOperatingMode4,
    PitOperatingMode5,
    PitOperatingMode2Reserved,
    PitOperatingMode5Reserved
} TIMER_OPERATING_MODES;

typedef enum _TIMER_ACCESS_MODES
{
    PitAccessModeCounterLatch,
    PitAccessModeLow,
    PitAccessModeHigh,
    PitAccessModeLowHigh
} TIMER_ACCESS_MODES;

typedef enum _TIMER_CHANNELS
{
    PitChannel0,
    PitChannel1,
    PitChannel2,
    PitReadBack
} TIMER_CHANNELS;

typedef union _TIMER_CONTROL_PORT_REGISTER
{
    struct
    {
        UCHAR BcdMode:1;
        UCHAR OperatingMode:3;
        UCHAR AccessMode:2;
        UCHAR Channel:2;
    };
    UCHAR Bits;
} TIMER_CONTROL_PORT_REGISTER, *PTIMER_CONTROL_PORT_REGISTER;

/* Commonly stated as being 1.19318MHz

   See ISA System Architecture 3rd Edition (Tom Shanley, Don Anderson, John Swindle) P. 471

   However, the true value is closer to 1.19318181[...]81MHz since this is 1/3rd
   of the NTSC color subcarrier frequency which runs at 3.57954545[...]45MHz.

   Note that Windows uses 1.193167MHz which seems to have no basis.
   However, if one takes the NTSC color subcarrier frequency as being 3.579545
   (trimming the infinite series) and divides it by three, one obtains 1.19318167.

   It may be that the original NT HAL source code introduced a typo and turned
   119318167 into 1193167 by ommitting the "18". This is very plausible as the
   number is quite long.
*/
#define PIT_FREQUENCY 1193182

/* These ports are controlled by the i8254 Programmable Interrupt Timer (PIT) */
#define TIMER_CHANNEL0_DATA_PORT (PUCHAR)0x0040
#define TIMER_CHANNEL1_DATA_PORT (PUCHAR)0x0041
#define TIMER_CHANNEL2_DATA_PORT (PUCHAR)0x0042
#define TIMER_CONTROL_PORT       (PUCHAR)0x0043

#define PIT_LATCH  0x00

/* EOF */
