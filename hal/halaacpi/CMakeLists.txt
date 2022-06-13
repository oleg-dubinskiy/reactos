
add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(include ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

# Handle the spec file for the dll name
spec2def(halaacpi.dll halaacpi.spec ADD_IMPORTLIB)

# The components generic
list(APPEND HAL_SOURCE
    generic/display.c
    generic/halinit.c
)

add_library(halaacpi MODULE
    ${HAL_SOURCE}
    version/halaacpi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/halaacpi.def)

set_module_type(halaacpi kerneldll ENTRYPOINT HalInitSystem 8)
target_link_libraries(halaacpi libcntpr)
add_dependencies(halaacpi asm psdk bugcodes xdk)
add_importlibs(halaacpi ntoskrnl)
#add_pch(halaacpi include/hal.h SOURCE)
add_cd_file(TARGET halaacpi DESTINATION reactos/system32 NO_CAB FOR all)
