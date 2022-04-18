
add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(include ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

# Handle the spec file for the dll name
spec2def(halacpi.dll halacpi.spec ADD_IMPORTLIB)

# The components generic
list(APPEND HAL_SOURCE
    acpi/busemul.c
    acpi/dispatch.c
    acpi/halacpi.c
    acpi/halpnpdd.c
    bus/pcibus.c
    generic/i386/bios.c
    generic/i386/portio.c
    generic/cmos.c
    generic/display.c
    generic/dma.c
    generic/halinit.c
    generic/memory.c
    generic/misc.c
    generic/sysinfo.c
    pic/pic.c
)

add_library(halacpi MODULE
    ${HAL_SOURCE}
    version/halacpi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/halacpi.def)

set_module_type(halacpi kerneldll ENTRYPOINT HalInitSystem 8)
target_link_libraries(halacpi libcntpr)
add_dependencies(halacpi asm psdk bugcodes xdk)
add_importlibs(halacpi ntoskrnl)
#add_pch(halacpi include/hal.h SOURCE)
add_cd_file(TARGET halacpi DESTINATION reactos/system32 NO_CAB FOR all)
