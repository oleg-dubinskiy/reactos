
list(APPEND SOURCE
    ftdisk.c
    ftdisk.h)

add_library(ftdisk MODULE ${SOURCE} ftdisk.rc)
target_link_libraries(ftdisk wdmguid memcmp)
set_module_type(ftdisk kernelmodedriver)
add_importlibs(ftdisk ntoskrnl hal)
#add_pch(ftdisk ftdisk.h SOURCE)
add_cd_file(TARGET ftdisk DESTINATION reactos/system32/drivers NO_CAB FOR all)
