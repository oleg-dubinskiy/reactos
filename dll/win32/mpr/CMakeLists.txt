
add_definitions(
    -D_WINE)

spec2def(mpr.dll mpr.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    auth.c
    mpr_main.c
    multinet.c
    nps.c
    pwcache.c
    wnet.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/mpr_stubs.c)

add_library(mpr MODULE
    ${SOURCE}
    mpr.rc
    ${CMAKE_CURRENT_BINARY_DIR}/mpr.def)

set_module_type(mpr win32dll)
target_link_libraries(mpr wine)
add_importlibs(mpr advapi32 user32 msvcrt kernel32 ntdll)
add_pch(mpr precomp.h SOURCE)
add_cd_file(TARGET mpr DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(mpr) # CORE-5743: No ARRAY_SIZE macro
