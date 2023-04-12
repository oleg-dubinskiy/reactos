/*
 * PROJECT:     ACPI driver for NT 5.x
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Working with the registry
 * COPYRIGHT:   Copyright 2019, 2023 Vadim Galyant <vgal@rambler.ru>
 */

#include "acpi.h"

//#define NDEBUG
#include <debug.h>

#ifdef ALLOC_PRAGMA
  #pragma alloc_text(PAGE, OSCloseHandle)
#endif

/* GLOBALS *******************************************************************/


/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
OSCloseHandle(
    _In_ HANDLE Handle)
{
    PAGED_CODE();
    DPRINT("OSCloseHandle: %p\n", Handle);
    return ZwClose(Handle);
}

VOID
NTAPI
ACPIInitReadRegistryKeys(VOID)
{
    UNIMPLEMENTED_DBGBREAK();
}

/* EOF */
