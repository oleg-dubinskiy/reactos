
/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
//#define NDEBUG
#include <debug.h>
#include <mm_new/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

extern PMM_SESSION_SPACE MmSessionSpace;
extern PMMPTE MiSessionImagePteStart;
extern PMMPTE MiSessionImagePteEnd;
extern PMMPTE MiSessionBasePte;
extern PMMPTE MiSessionLastPte;
extern PVOID MmSessionBase;
extern PVOID MiSessionImageStart;
extern PVOID MiSessionImageEnd;
extern PVOID MiSessionPoolEnd;     // 0xBE000000
extern PVOID MiSessionPoolStart;   // 0xBD000000
extern PVOID MiSessionViewStart;   // 0xBE000000
extern PVOID MiSessionSpaceWs;
extern PVOID MiSessionSpaceEnd;
extern SIZE_T MmSessionSize;
extern SIZE_T MmSessionImageSize;
extern SIZE_T MmSessionViewSize;
extern SIZE_T MmSessionPoolSize;
extern SIZE_T MmSystemViewSize;
extern PVOID MiSystemViewStart;

/* FUNCTIONS ******************************************************************/

INIT_FUNCTION
VOID
NTAPI
MiInitializeSessionSpaceLayout(VOID)
{
    /* Set the size of session view, pool, and image */
    MmSessionSize = MI_SESSION_SIZE;
    MmSessionViewSize = MI_SESSION_VIEW_SIZE;
    MmSessionPoolSize = MI_SESSION_POOL_SIZE;
    MmSessionImageSize = MI_SESSION_IMAGE_SIZE;

    /* Set the size of system view */
    MmSystemViewSize = MI_SYSTEM_VIEW_SIZE;

    /* This is where it all ends */
    MiSessionImageEnd = (PVOID)PTE_BASE;

    /* This is where we will load Win32k.sys and the video driver */
    MiSessionImageStart = (PVOID)((ULONG_PTR)MiSessionImageEnd - MmSessionImageSize);

    /* So the view starts right below the session working set (itself below the image area) */
    MiSessionViewStart = (PVOID)((ULONG_PTR)MiSessionImageEnd -
                                 MmSessionImageSize -
                                 MI_SESSION_WORKING_SET_SIZE -
                                 MmSessionViewSize);
    /* Session pool follows */
    MiSessionPoolEnd = MiSessionViewStart;
    MiSessionPoolStart = (PVOID)((ULONG_PTR)MiSessionPoolEnd - MmSessionPoolSize);

    /* And it all begins here */
    MmSessionBase = MiSessionPoolStart;

    /* Sanity check that our math is correct */
    ASSERT((ULONG_PTR)MmSessionBase + MmSessionSize == PTE_BASE);

    /* Session space ends wherever image session space ends */
    MiSessionSpaceEnd = MiSessionImageEnd;

    /* System view space ends at session space, so now that we know where this is,
       we can compute the base address of system view space itself.
    */
    MiSystemViewStart = (PVOID)((ULONG_PTR)MmSessionBase - MmSystemViewSize);

    /* Compute the PTE addresses for all the addresses we carved out */
    MiSessionImagePteStart = MiAddressToPte(MiSessionImageStart);
    MiSessionImagePteEnd = MiAddressToPte(MiSessionImageEnd);
    MiSessionBasePte = MiAddressToPte(MmSessionBase);
    MiSessionSpaceWs = (PVOID)((ULONG_PTR)MiSessionViewStart + MmSessionViewSize);
    MiSessionLastPte = MiAddressToPte(MiSessionSpaceEnd);

    /* Initialize session space */
    MmSessionSpace = (PMM_SESSION_SPACE)((ULONG_PTR)MmSessionBase +
                                         MmSessionSize -
                                         MmSessionImageSize -
                                         MM_ALLOCATION_GRANULARITY);
}

/* EOF */
