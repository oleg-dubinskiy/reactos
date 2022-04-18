
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
HaliQuerySystemInformation(IN HAL_QUERY_INFORMATION_CLASS InformationClass,
                           IN ULONG BufferSize,
                           IN OUT PVOID Buffer,
                           OUT PULONG OutLength)
{
    DPRINT1("HaliQuerySystemInformation: InformationClass %X, Size %X\n", InformationClass, BufferSize);
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HaliSetSystemInformation(IN HAL_SET_INFORMATION_CLASS InformationClass,
                         IN ULONG BufferSize,
                         IN OUT PVOID Buffer)
{
    DPRINT1("HaliSetSystemInformation: InformationClass %X, Size %X\n", InformationClass, BufferSize);
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
