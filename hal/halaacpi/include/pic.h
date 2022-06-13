
#pragma once

/* See ISA System Architecture 3rd Edition (Tom Shanley, Don Anderson, John Swindle) P. 396, 397
   These ports are controlled by the i8259 Programmable Interrupt Controller (PIC)
*/
#define PIC1_CONTROL_PORT      (PUCHAR)0x0020
#define PIC1_DATA_PORT         (PUCHAR)0x0021
#define PIC2_CONTROL_PORT      (PUCHAR)0x00A0
#define PIC2_DATA_PORT         (PUCHAR)0x00A1

/* EOF */

