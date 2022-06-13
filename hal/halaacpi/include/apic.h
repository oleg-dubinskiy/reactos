
#pragma once

INIT_FUNCTION
BOOLEAN
NTAPI 
DetectAcpiMP(
    _Out_ PBOOLEAN OutIsMpSystem,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

/* EOF */
