
/* INCLUDES *******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>


typedef
NTSTATUS
(NTAPI * PHALP_AMLI_ILLEGAL_IO_PORT_HANDLER)(
    BOOLEAN IsRead,
    ULONG Port,
    ULONG Length,
    PVOID Buffer
);

typedef struct _AMLI_ILLEGAL_IO_PORT_ADDRESSES
{
    ULONG Start;
    ULONG Length;
    ULONG Param;
    PHALP_AMLI_ILLEGAL_IO_PORT_HANDLER Handler;
} AMLI_ILLEGAL_IO_PORT_ADDRESSES, *PAMLI_ILLEGAL_IO_PORT_ADDRESSES;

NTSTATUS
NTAPI
HaliHandlePCIConfigSpaceAccess(
    BOOLEAN IsRead,
    ULONG Port,
    ULONG Length,
    PVOID Buffer
);

AMLI_ILLEGAL_IO_PORT_ADDRESSES AMLIIllegalIOPortAddresses[19] =
{
    { 0x0000, 0x10, 1, NULL },
    { 0x0020, 0x02, 0, NULL },
    { 0x0040, 0x04, 1, NULL },
    { 0x0048, 0x04, 1, NULL },
    { 0x0070, 0x02, 1, NULL },
    { 0x0074, 0x03, 1, NULL },
    { 0x0081, 0x03, 1, NULL },
    { 0x0087, 0x01, 1, NULL },
    { 0x0089, 0x01, 1, NULL },
    { 0x008A, 0x02, 1, NULL },
    { 0x008F, 0x01, 1, NULL },
    { 0x0090, 0x02, 1, NULL },
    { 0x0093, 0x02, 1, NULL },
    { 0x0096, 0x02, 1, NULL },
    { 0x00A0, 0x02, 0, NULL },
    { 0x00C0, 0x20, 1, NULL },
    { 0x04D0, 0x02, 0, NULL },
    { 0x0CF8, 0x08, 1, &HaliHandlePCIConfigSpaceAccess },
    { 0x0000, 0x00, 0, NULL }
};

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
HaliHandlePCIConfigSpaceAccess(BOOLEAN IsRead,
                               ULONG Port,
                               ULONG Length,
                               PVOID Buffer)
{
    UNIMPLEMENTED;
    ASSERT(0);//HalpDbgBreakPointEx();
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HaliQuerySystemInformation(IN HAL_QUERY_INFORMATION_CLASS InformationClass,
                           IN ULONG BufferSize,
                           IN OUT PVOID Buffer,
                           OUT PULONG ReturnedLength)
{
    ULONG Size;
    PVOID LocalBuffer;
    NTSTATUS Status;

    DPRINT1("HaliQuerySystemInformation: %X, %X\n", InformationClass, BufferSize);

#define REPORT_THIS_CASE(Class) case Class: DPRINT1("FIXME: %s\n", #Class); break

    switch (InformationClass)
    {
        case HalQueryAMLIIllegalIOPortAddresses:
        {
            DPRINT1("HalQueryAMLIIllegalIOPortAddresses\n");
            Size = sizeof(AMLIIllegalIOPortAddresses);

            if (BufferSize < Size) {
                *ReturnedLength = Size;
                Status = STATUS_INFO_LENGTH_MISMATCH;
                goto Exit;
            }

            Status = 0;
            LocalBuffer = AMLIIllegalIOPortAddresses;

            if (Size > BufferSize)
                Size = BufferSize;

            *ReturnedLength = Size;
            RtlCopyMemory(Buffer, LocalBuffer, Size);

Exit:
            //KeFlushWriteBuffer();
            return Status;
        }
        REPORT_THIS_CASE(HalInstalledBusInformation);
        REPORT_THIS_CASE(HalProfileSourceInformation);
        REPORT_THIS_CASE(HalInformationClassUnused1);
        REPORT_THIS_CASE(HalPowerInformation);
        REPORT_THIS_CASE(HalProcessorSpeedInformation);
        REPORT_THIS_CASE(HalCallbackInformation);
        REPORT_THIS_CASE(HalMapRegisterInformation);
        REPORT_THIS_CASE(HalMcaLogInformation);
        case HalFrameBufferCachingInformation:
        {
            /* FIXME: TODO */
            return STATUS_NOT_IMPLEMENTED;
        }
        REPORT_THIS_CASE(HalDisplayBiosInformation);
        REPORT_THIS_CASE(HalProcessorFeatureInformation);
        REPORT_THIS_CASE(HalNumaTopologyInterface);
        REPORT_THIS_CASE(HalErrorInformation);
        REPORT_THIS_CASE(HalCmcLogInformation);
        REPORT_THIS_CASE(HalCpeLogInformation);
        REPORT_THIS_CASE(HalQueryMcaInterface);
        REPORT_THIS_CASE(HalQueryMaxHotPlugMemoryAddress);
        REPORT_THIS_CASE(HalPartitionIpiInterface);
        REPORT_THIS_CASE(HalPlatformInformation);
        REPORT_THIS_CASE(HalQueryProfileSourceList);
        REPORT_THIS_CASE(HalInitLogInformation);
        REPORT_THIS_CASE(HalFrequencyInformation);
        REPORT_THIS_CASE(HalProcessorBrandString);
        REPORT_THIS_CASE(HalHypervisorInformation);
        REPORT_THIS_CASE(HalPlatformTimerInformation);
        REPORT_THIS_CASE(HalAcpiAuditInformation);
    }

#undef REPORT_THIS_CASE

    UNIMPLEMENTED;
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
