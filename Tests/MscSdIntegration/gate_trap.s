    AREA |.text.trap|, CODE, READONLY
    THUMB
    IMPORT Gate_Unexpected
    EXPORT BSP_SDMMC_EnableTransferMode
    EXPORT BSP_SDMMC_GetCardInfo
    EXPORT BSP_SDMMC_GetIdentity
    EXPORT BSP_SDMMC_GetIoDiagnostics
    EXPORT BSP_SDMMC_Init
    EXPORT BSP_SDMMC_PrepareWriteInput
    EXPORT BSP_SDMMC_ReadBlocks
    EXPORT BSP_SDMMC_ReadDetect
    EXPORT BSP_SDMMC_Recover
    EXPORT BSP_SDMMC_SetWaitOps
    EXPORT BSP_SDMMC_WriteBlocks
    EXPORT FatFsDisk_GetLastResult
    EXPORT FatFsDisk_Reset
    EXPORT __aeabi_memcpy4
    EXPORT f_close
    EXPORT f_getfree
    EXPORT f_lseek
    EXPORT f_mkdir
    EXPORT f_mount
    EXPORT f_open
    EXPORT f_read
    EXPORT f_sync
    EXPORT f_unlink
    EXPORT f_write
    EXPORT memcmp
    EXPORT strncmp
    EXPORT xQueueGenericCreateStatic
    EXPORT xQueueGiveFromISR
    EXPORT xQueueSemaphoreTake
BSP_SDMMC_EnableTransferMode
BSP_SDMMC_GetCardInfo
BSP_SDMMC_GetIdentity
BSP_SDMMC_GetIoDiagnostics
BSP_SDMMC_Init
BSP_SDMMC_PrepareWriteInput
BSP_SDMMC_ReadBlocks
BSP_SDMMC_ReadDetect
BSP_SDMMC_Recover
BSP_SDMMC_SetWaitOps
BSP_SDMMC_WriteBlocks
FatFsDisk_GetLastResult
FatFsDisk_Reset
__aeabi_memcpy4
f_close
f_getfree
f_lseek
f_mkdir
f_mount
f_open
f_read
f_sync
f_unlink
f_write
memcmp
strncmp
xQueueGenericCreateStatic
xQueueGiveFromISR
xQueueSemaphoreTake
    EXPORT FatFsDisk_Begin
FatFsDisk_Begin
    EXPORT FatFsDisk_Configure
FatFsDisk_Configure
    EXPORT FatFsDisk_SetWritable
FatFsDisk_SetWritable
    B Gate_Unexpected
    END
