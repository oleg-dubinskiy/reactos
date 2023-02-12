
add_definitions(
    -DNDIS50_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_MINIPORT
    -DNDIS51_MINIPORT)

list(APPEND SOURCE
    pcnet.c
    requests.c
    pcnet.h)

add_library(pcntpci5 MODULE ${SOURCE} pcntpci5.rc)
add_pch(pcntpci5 pcnet.h SOURCE)
set_module_type(pcntpci5 kernelmodedriver)
add_importlibs(pcntpci5 ndis ntoskrnl hal)
add_cd_file(TARGET pcntpci5 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(pcntpci5 amdpcnet.inf)
