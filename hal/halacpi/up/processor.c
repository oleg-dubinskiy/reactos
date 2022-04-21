
/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG HalpFeatureBits;

/* CPU Signatures */
static const CHAR CmpIntelID[]       = "GenuineIntel";
static const CHAR CmpAmdID[]         = "AuthenticAMD";
static const CHAR CmpCyrixID[]       = "CyrixInstead";
static const CHAR CmpTransmetaID[]   = "GenuineTMx86";
static const CHAR CmpCentaurID[]     = "CentaurHauls";
static const CHAR CmpRiseID[]        = "RiseRiseRise";

/* PRIVATE FUNCTIONS *********************************************************/

FORCEINLINE
VOID
HalpCpuId(
    PCPU_INFO CpuInfo,
    ULONG Function)
{
    __cpuid((INT*)CpuInfo->AsUINT32, Function);
}

ULONG
NTAPI
HalpGetCpuVendor(VOID)
{
    CHAR VendorString[13];
    CPU_INFO CpuInfo;

    /* Assume no Vendor ID and fail if no CPUID Support. */
    VendorString[0] = 0;

    if (KeGetCurrentPrcb()->CpuID == 0)
        return 0;

    /* Get the Vendor ID */
    HalpCpuId(&CpuInfo, 0);

    /* Copy it to the PRCB and null-terminate it */
    *(ULONG*)&VendorString[0] = CpuInfo.Ebx;
    *(ULONG*)&VendorString[4] = CpuInfo.Edx;
    *(ULONG*)&VendorString[8] = CpuInfo.Ecx;
    VendorString[12] = 0;

    /* Now check the CPU Type */
    if (!strcmp(VendorString, CmpIntelID))
    {
        return CPU_INTEL;
    }
    else if (!strcmp(VendorString, CmpAmdID))
    {
        return CPU_AMD;
    }
    else if (!strcmp(VendorString, CmpCyrixID))
    {
        DPRINT1("HalpGetCpuVendor: Cyrix CPU support not fully tested!\n");
        return CPU_CYRIX;
    }
    else if (!strcmp(VendorString, CmpTransmetaID))
    {
        DPRINT1("HalpGetCpuVendor: Transmeta CPU support not fully tested!\n");
        return CPU_TRANSMETA;
    }
    else if (!strcmp(VendorString, CmpCentaurID))
    {
        DPRINT1("HalpGetCpuVendor: Centaur CPU support not fully tested!\n");
        return CPU_CENTAUR;
    }
    else if (!strcmp(VendorString, CmpRiseID))
    {
        DPRINT1("HalpGetCpuVendor: Rise CPU support not fully tested!\n");
        return CPU_RISE;
    }

    /* Unknown CPU */
    DPRINT1("HalpGetCpuVendor: %s CPU support not fully tested!\n", VendorString);
    return CPU_UNKNOWN;
}

ULONG
NTAPI
HalpGetFeatureBits(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Vendor;
    ULONG FeatureBits = 0;
    ULONG CpuFeatures;
    ULONGLONG MsrApicBase;
    CPU_INFO CpuInfo;

    Vendor = HalpGetCpuVendor();
    if (Vendor == CPU_NONE)
        return 2;

    HalpCpuId(&CpuInfo, 1);
    CpuFeatures = CpuInfo.Edx;

    switch (Vendor)
    {
        case CPU_INTEL:
        {
            if (Prcb->CpuType == 6)
            {
                FeatureBits = 1;

                MsrApicBase = __readmsr(0x1B); // MSR_IA32_APICBASE
                if (MsrApicBase & 0x800)
                     __writemsr(0x1B, MsrApicBase & ~0x800); // MSR_IA32_APICBASE
            }
            else if (Prcb->CpuType < 6)
            {
                FeatureBits |= 2;
            }

            break;
        }
        case CPU_AMD:
        {
            CpuInfo.Eax = 0;

            HalpCpuId(&CpuInfo, 0x80000000); // HighestExFunction
            if (CpuInfo.Eax < 0x80000001)
                break;

            HalpCpuId(&CpuInfo, 0x80000001); // ExFeatures
            if (CpuInfo.Edx & 0x100000)
                FeatureBits |= 0x40;

            break;
        }
        default:
        {
            DPRINT1("HalpGetFeatureBits: CPU %d\n", Vendor);
            break;
        }
    }

    if (CpuFeatures & 0x4000)
        FeatureBits |= 4;

    if (CpuFeatures & 0x80)
        FeatureBits |= 8;

    if (CpuFeatures & 2)
        FeatureBits |= 0x10;

    if (CpuFeatures & 0x4000000)
        FeatureBits |= 0x20;

    KeFlushWriteBuffer();

    return FeatureBits;
}

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpRemoveFences(VOID)
{
    DPRINT1("HalpRemoveFences FIXME!\n");
    ASSERT(FALSE);//HalpDbgBreakPointEx();
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    DPRINT1("HalAllProcessorsStarted: HalpFeatureBits %X\n", HalpFeatureBits);

    if (HalpFeatureBits & 2) {
        HalpRemoveFences();
    }

    return TRUE;
}

BOOLEAN
NTAPI
HalStartNextProcessor(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN PKPROCESSOR_STATE ProcessorState)
{
    /* Ready to start */
    return FALSE;
}

VOID
NTAPI
HalProcessorIdle(VOID)
{
    /* Enable interrupts and halt the processor */
    _enable();
    __halt();
}

VOID
NTAPI
HalRequestIpi(KAFFINITY TargetProcessors)
{
    /* Not implemented on UP */
    __debugbreak();
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    return;
}

/* EOF */
