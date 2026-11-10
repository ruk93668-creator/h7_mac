//编译真实Platform和官方FatFS，仅模拟任务环境及物理扇区介质
#include "../../CM7/Platform/sd_block.c"
#include "../../CM7/Platform/diskio.c"
#include "../../CM7/Platform/storage_fs.c"
#include "hardware_acceptance.c"

uint32_t test_ipsr, test_primask, test_basepri;
static uint32_t test_task = 1U;
static uint32_t test_blocks = 16384U;
static bool test_present = true;
static bool test_initialized, test_fault, test_active;
static bool fail_recover, fail_init, reenter;
static int32_t read_fail_after = -1, write_fail_after = -1;
static SysResult_t injected_error = SYS_ERR_IO;
static uint32_t read_calls, write_calls, recover_calls, reenter_checks;
static uint32_t prepare_calls;
static uint32_t io_clock, io_clock_step;
uint32_t Platform_TimeGetMs(void) { uint32_t value=io_clock;io_clock+=io_clock_step;return value; }

static int32_t prepare_fail_after = -1;
static bool prepare_reenter;
static StorageFile_t prepare_file;

//物理BSP替身仅准备输入副本，真实FatFS和Platform仍完整执行
SysResult_t BSP_SDMMC_PrepareWriteInput(void *destination,const void *source,uint32_t bytes)
{
    uint32_t n;
    if(destination!=s_write_input || bytes==0U || bytes>sizeof(s_write_input))
        return SYS_ERR_INVALID_ARG;
    if(prepare_fail_after==0)return SYS_ERR_TIMEOUT;
    if(prepare_fail_after>0)prepare_fail_after--;
    if(prepare_reenter)
    {
        prepare_reenter=false;
        if(StorageFS_Write(prepare_file,source,bytes,&n)!=SYS_ERR_BUSY)return SYS_ERR_PROTOCOL;
    }
    memcpy(destination,source,bytes);
    prepare_calls++;
    return SYS_OK;
}

static uint8_t *const medium = (uint8_t *)0x60000000U;
static uint8_t *const buffer = (uint8_t *)0x68000000U;
static uint8_t *const verify = (uint8_t *)0x68040000U;

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)test_task; }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *p) { return p; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t e, TickType_t t) { (void)e; (void)t; return pdTRUE; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t e, BaseType_t *w) { (void)e; *w=pdFALSE; return pdTRUE; }
SysResult_t BSP_SDMMC_SetWaitOps(const BspSdmmcWaitOps_t *ops) { (void)ops; return SYS_OK; }
SysResult_t BSP_SDMMC_Init(void)
{
    if (!test_present) return SYS_ERR_NOT_FOUND;
    if (fail_init) return SYS_ERR_TIMEOUT;
    test_initialized = true; return SYS_OK;
}
SysResult_t BSP_SDMMC_EnableTransferMode(void) { return test_initialized ? SYS_OK : SYS_ERR_INVALID_STATE; }
SysResult_t BSP_SDMMC_ReadDetect(uint32_t *raw, bool *present) { *raw=test_present?1U:0U; *present=test_present; return SYS_OK; }
SysResult_t BSP_SDMMC_GetCardInfo(BspSdmmcCardInfo_t *p)
{
    if (!test_initialized) return SYS_ERR_INVALID_STATE;
    memset(p,0,sizeof(*p)); p->logical_block_count=test_blocks; p->logical_block_size=512U;
    p->capacity_bytes=(uint64_t)test_blocks*512U; return SYS_OK;
}
SysResult_t BSP_SDMMC_GetIdentity(uint32_t p[4]) { p[0]=1;p[1]=2;p[2]=3;p[3]=4;return SYS_OK; }
SysResult_t BSP_SDMMC_GetIoDiagnostics(BspSdmmcIoDiagnostics_t *p)
{
    memset(p,0,sizeof(*p));p->active=test_active;p->fault=test_fault;p->transfer_ready=test_initialized;return SYS_OK;
}
SysResult_t BSP_SDMMC_ReadBlocks(uint32_t lba,uint32_t n,void *p,uint32_t timeout)
{
    (void)timeout;
    if (!test_present) return SYS_ERR_NOT_FOUND;
    if (lba>=test_blocks || n>test_blocks-lba) return SYS_ERR_INVALID_ARG;
    if (reenter)
    {
        StorageFsDiagnostics_t d;
        reenter=false;
        if(StorageFS_GetDiagnostics(&d)==SYS_ERR_BUSY)reenter_checks++;
    }
    if (read_fail_after==0) { test_fault=true;return injected_error; }
    if (read_fail_after>0)read_fail_after--;
    read_calls++;memcpy(p,medium+lba*512U,n*512U);return SYS_OK;
}
SysResult_t BSP_SDMMC_WriteBlocks(uint32_t lba,uint32_t n,const void *p,uint32_t timeout)
{
    (void)timeout;
    if (!test_present) return SYS_ERR_NOT_FOUND;
    if (lba>=test_blocks || n>test_blocks-lba) return SYS_ERR_INVALID_ARG;
    if (write_fail_after==0) { test_fault=true;return injected_error; }
    if (write_fail_after>0)write_fail_after--;
    write_calls++;memcpy(medium+lba*512U,p,n*512U);return SYS_OK;
}
SysResult_t BSP_SDMMC_Recover(void)
{
    recover_calls++;
    if (fail_recover)return SYS_ERR_IO;
    test_initialized=false;test_fault=false;test_active=false;read_fail_after=-1;write_fail_after=-1;
    return SYS_OK;
}

//每个用例从独立介质镜像和全新静态内存开始
uint32_t FatFs_TestRun(uint32_t id)
{
    StorageFile_t f=0U,old=0U,h[4];
    StorageFsDiagnostics_t d;
    uint32_t i,n,got,before;
    uint64_t total,free_bytes;
    SysResult_t result;
    CHECK(SdBlock_Prepare()==SYS_OK);
    if(id==0){CHECK(SD_Block_Init()==SYS_ERR_INVALID_STATE);CHECK(StorageFS_Mount(false,0)==SYS_ERR_INVALID_STATE);return 0;}
    CHECK(StorageFS_Init()==SYS_OK);
    if(id==51){CHECK(StorageFS_Mount(false,0)==SYS_OK);CHECK(StorageFS_Unmount()==SYS_OK);medium[510]=0;CHECK(StorageFS_Mount(false,0)==SYS_ERR_PROTOCOL);CHECK(StorageFS_GetDiagnostics(&d)==SYS_OK);CHECK(d.filesystem==0 && d.layout==STORAGE_FS_LAYOUT_INVALID && d.fatfs_called==0);return 0;}
    if(id>=43 && id<=50){
        if(id==50){FatFsAcceptance_Process(4);CHECK(g_fatfs_acceptance.result==SYS_ERR_INVALID_ARG);CHECK(read_calls==0 && write_calls==0);return 0;}
        FatFsAcceptance_Process(1);CHECK(g_fatfs_acceptance.state==2);CHECK(write_calls==0);
        if(id==43)return 0;
        if(id==44){FatFsAcceptance_Process(2);CHECK(g_fatfs_acceptance.result==SYS_ERR_INVALID_ARG);CHECK(write_calls==0);return 0;}
        g_fatfs_acceptance.write_confirm=FATFS_ACCEPT_WRITE_CONFIRM;
        g_fatfs_acceptance.expected_capacity=g_fatfs_acceptance.capacity;
        g_fatfs_acceptance.expected_first_lba=g_fatfs_acceptance.volume.first_lba;
        g_fatfs_acceptance.expected_sector_count=g_fatfs_acceptance.volume.sector_count;
        for(i=0;i<4;i++)g_fatfs_acceptance.expected_cid[i]=g_fatfs_acceptance.volume.cid[i];
        g_fatfs_acceptance.file_index=1;
        if(id==47){g_fatfs_acceptance.expected_cid[0]++;FatFsAcceptance_Process(2);CHECK(g_fatfs_acceptance.result==SYS_ERR_INVALID_ARG);CHECK(write_calls==0);return 0;}
        if(id==49){g_fatfs_acceptance.file_index=0;FatFsAcceptance_Process(2);CHECK(g_fatfs_acceptance.result==SYS_ERR_INVALID_ARG);CHECK(write_calls==0);return 0;}
        if(id==48){CHECK(StorageFS_Mount(true,0)==SYS_OK);CHECK(StorageFS_Open("0:/ERR.TST",6,&f)==SYS_OK);CHECK(StorageFS_Write(f,buffer,37,&n)==SYS_OK);write_fail_after=0;CHECK(StorageFS_Sync(f)==SYS_ERR_IO);fail_recover=true;FatFsAcceptance_Process(3);CHECK(g_fatfs_acceptance.state==3 && s_lease);fail_recover=false;FatFsAcceptance_Process(3);CHECK(g_fatfs_acceptance.state==2 && !s_lease);return 0;}
        FatFsAcceptance_Process(2);CHECK(g_fatfs_acceptance.state==2);CHECK(g_fatfs_acceptance.bytes_written==131109U && g_fatfs_acceptance.bytes_read==131109U);CHECK(g_fatfs_acceptance.write_crc==g_fatfs_acceptance.read_crc && g_fatfs_acceptance.buffer_valid==1);
        if(id==46){before=write_calls;FatFsAcceptance_Process(2);CHECK(g_fatfs_acceptance.result==SYS_ERR_ALREADY_EXISTS);CHECK(write_calls==before);}
        return 0;
    }
    if(id==1){test_present=false;CHECK(StorageFS_Mount(false,0)==SYS_ERR_NOT_FOUND);CHECK(!s_lease);return 0;}
    if(id==2){medium[510]=0;CHECK(StorageFS_Mount(false,0)==SYS_ERR_PROTOCOL);CHECK(write_calls==0);return 0;}
    if(id==3){memset(medium,0,512);medium[510]=0x55;medium[511]=0xAA;medium[450]=0xEE;CHECK(StorageFS_Mount(false,0)==SYS_ERR_NOT_SUPPORTED);CHECK(write_calls==0);return 0;}
    if(id==4){memcpy(medium+3,"EXFAT   ",8);CHECK(StorageFS_Mount(false,0)==SYS_ERR_NOT_SUPPORTED);return 0;}
    if(id==5){medium[454]=0xFF;medium[455]=0xFF;medium[456]=0xFF;medium[457]=0xFF;CHECK(StorageFS_Mount(false,0)==SYS_ERR_PROTOCOL);return 0;}
    if(id==12){read_fail_after=0;injected_error=SYS_ERR_TIMEOUT;CHECK(StorageFS_Mount(false,0)==SYS_ERR_TIMEOUT);CHECK(s_lease);CHECK(StorageFS_Recover()==SYS_OK);return 0;}
    if(id==17){CHECK(SD_Block_Claim(SD_BLOCK_CLIENT_RAW)==SYS_OK);CHECK(StorageFS_Mount(false,0)==SYS_ERR_BUSY);CHECK(SD_Block_Release(SD_BLOCK_CLIENT_RAW)==SYS_OK);return 0;}
    if(id==18){test_ipsr=16;CHECK(StorageFS_Mount(false,0)==SYS_ERR_INVALID_STATE);CHECK(!s_lease);return 0;}
    if(id==19){test_basepri=80;CHECK(StorageFS_Mount(false,0)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==24){medium[466]=0x83;medium[470]=1;medium[471]=4;medium[472]=0;medium[473]=0;medium[474]=1;CHECK(StorageFS_Mount(false,0)==SYS_ERR_PROTOCOL);return 0;}
    if(id==25){medium[1024U*512U+19]=0xFF;medium[1024U*512U+20]=0xFF;CHECK(StorageFS_Mount(false,0)==SYS_ERR_PROTOCOL);return 0;}
    if(id==28)reenter=true;
    if(id==29){medium[22]=0;medium[23]=0;CHECK(StorageFS_Mount(false,0)==SYS_ERR_PROTOCOL);CHECK(s_diag.fatfs_result==FR_NO_FILESYSTEM);CHECK(write_calls==0);return 0;}
    if(id==34){CHECK(StorageFS_Mount(false,2)==SYS_ERR_NOT_SUPPORTED);return 0;}
    if(id==35){CHECK(StorageFS_Mount(false,5)==SYS_ERR_INVALID_ARG);CHECK(!s_lease);return 0;}
    if(id==37){fail_init=true;CHECK(StorageFS_Mount(false,0)==SYS_ERR_TIMEOUT);CHECK(!s_lease);fail_init=false;CHECK(StorageFS_Mount(false,0)==SYS_OK);CHECK(StorageFS_Unmount()==SYS_OK);return 0;}
    if(id==40)test_blocks=131072U;
    if(id==68)
    {
        /* Move the FAT fixture to MBR entry 3; entry order is explicit. */
        memcpy(medium+478,medium+446,16);
        memset(medium+446,0,16);
        CHECK(StorageFS_Mount(true,3)==SYS_OK);
        CHECK(s_diag.partition==3 && s_diag.first_lba==1024);
        CHECK(StorageFS_Open("0:/P3.BIN",6,&f)==SYS_OK);
        CHECK(StorageFS_Write(f,buffer,1024,&n)==SYS_OK && n==1024);
        CHECK(StorageFS_Close(f)==SYS_OK);
        CHECK(g_sd_write_debug.last_lba>=1024);
        CHECK(StorageFS_Unmount()==SYS_OK);
        return 0;
    }
    CHECK(StorageFS_Mount(id!=6,0)==SYS_OK);
    if(id==6){CHECK(StorageFS_Open("0:/TEST.BIN",6,&f)==SYS_ERR_NOT_SUPPORTED);CHECK(StorageFS_Unmount()==SYS_OK);CHECK(write_calls==0);return 0;}
    if(id==8){CHECK(StorageFS_Mount(true,0)==SYS_ERR_ALREADY_EXISTS);test_task=2;CHECK(StorageFS_Mount(true,0)==SYS_ERR_BUSY);CHECK(StorageFS_Open("0:/T.BIN",6,&f)==SYS_ERR_INVALID_STATE);CHECK(SD_Block_Write(0,1,buffer,10)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==21){CHECK(disk_read(0,buffer,0,0)==RES_PARERR);CHECK(disk_read(1,buffer,0,1)==RES_PARERR);CHECK(disk_read(0,buffer,16384,1)==RES_PARERR);return 0;}
    if(id==22){before=write_calls;CHECK(disk_write(0,buffer,0,1)==RES_PARERR);CHECK(disk_write(0,buffer,1023,2)==RES_PARERR);CHECK(write_calls==before);return 0;}
    if(id==23){CHECK(s_diag.partition==1);CHECK(s_diag.first_lba==1024);CHECK(StorageFS_Unmount()==SYS_OK);return 0;}
    if(id==28){CHECK(reenter_checks==1);CHECK(StorageFS_Unmount()==SYS_OK);return 0;}
    if(id==30){CHECK(StorageFS_Open("0:/VERYLONGNAME.TST",6,&f)==SYS_ERR_INVALID_ARG);CHECK(StorageFS_Open("1:/TEST.BIN",6,&f)==SYS_ERR_INVALID_ARG);CHECK(StorageFS_Open("0:/../T.BIN",6,&f)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==39){test_active=true;CHECK(StorageFS_GetSpace(&total,&free_bytes)==SYS_ERR_BUSY);CHECK(SD_Block_Release(SD_BLOCK_CLIENT_FATFS)==SYS_ERR_BUSY);return 0;}
    CHECK(StorageFS_Open("0:/TEST.BIN",STORAGE_FS_OPEN_READ|STORAGE_FS_OPEN_WRITE|STORAGE_FS_OPEN_CREATE_NEW,&f)==SYS_OK);

    if(id>=69 && id<=74)
    {
        uint64_t lower;
        uint32_t before_write=g_storage_io_timing.block_write.samples;
        io_clock_step=1;
        if(id==70)
        {
            prepare_fail_after=0;
            CHECK(StorageFS_Write(f,buffer,1024,&n)==SYS_ERR_TIMEOUT && n==0);
            CHECK(g_storage_io_timing.input_prepare.samples==1);
            CHECK(g_storage_io_timing.fatfs_write.samples==0);
            CHECK(g_storage_io_timing.block_write.samples==before_write);
            return 0;
        }
        if(id==71)
        {
            write_fail_after=0;injected_error=SYS_ERR_IO;
            CHECK(StorageFS_Write(f,buffer,1024,&n)==SYS_ERR_IO);
            CHECK(g_storage_io_timing.block_write.samples==before_write+1);
            CHECK(g_storage_io_timing.block_write.last_ms==1);
            CHECK(g_storage_io_timing.fatfs_write.samples==1);
            return 0;
        }
        if(id==72)io_clock=UINT32_MAX-3U;
        if(id==73)
        {
            FatFsIoTimingSample_t sample={0};
            FatFsDisk_RecordTiming(&sample,3);FatFsDisk_RecordTiming(&sample,8);
            CHECK(sample.samples==2 && sample.total_ms==11 && sample.max_ms==8 && sample.average_ms==5);
            sample.samples=UINT32_MAX;FatFsDisk_RecordTiming(&sample,100);
            CHECK(sample.total_ms==11);
            return 0;
        }
        for(i=0;i<16385;i++)buffer[i]=(uint8_t)(i*7U);
        CHECK(StorageFS_Write(f,buffer,16385,&n)==SYS_OK && n==16385);
        CHECK(g_storage_io_timing.input_prepare.samples==3);
        CHECK(g_storage_io_timing.fatfs_write.samples==3);
        CHECK(g_storage_io_timing.fatfs_other.samples==3);
        CHECK(g_storage_io_timing.input_prepare.total_ms==3);
        lower=g_storage_io_timing.block_read.total_ms+
            g_storage_io_timing.block_write.total_ms+g_storage_io_timing.block_sync.total_ms;
        CHECK(g_storage_io_timing.fatfs_other.total_ms<=g_storage_io_timing.fatfs_write.total_ms);
        CHECK(g_storage_io_timing.fatfs_write.total_ms<10000 && lower<10000);
        CHECK(StorageFS_Sync(f)==SYS_OK);
        CHECK(g_storage_io_timing.file_sync.samples==1);
        CHECK(StorageFS_Seek(f,0)==SYS_OK && g_storage_io_timing.file_seek.samples==1);
        CHECK(StorageFS_Read(f,verify,16385,&got)==SYS_OK && got==16385);
        CHECK(memcmp(buffer,verify,got)==0);
        CHECK(StorageFS_Close(f)==SYS_OK && g_storage_io_timing.file_close.samples==1);
        if(id==74)
        {
            CHECK(StorageFS_Unmount()==SYS_OK);
            CHECK(g_storage_io_timing.fatfs_write.samples==3);
            CHECK(StorageFS_Mount(true,0)==SYS_OK);
            CHECK(g_storage_io_timing.fatfs_write.samples==0);
        }
        return 0;
    }
    if(id==60)
    {
        CHECK(g_sd_write_debug.layout_valid==1);
        CHECK(g_sd_write_debug.sector_bytes==512);
        CHECK(g_sd_write_debug.sectors_per_cluster==s_volume.csize);
        CHECK(g_sd_write_debug.cluster_bytes==512U*s_volume.csize);
        CHECK(g_sd_write_debug.filesystem_type==s_volume.fs_type);
        return 0;
    }
    if(id==61)
    {
        const uint32_t counts[]={1,2,8,16,64,65};
        CHECK(g_sd_write_debug.write_calls==0);
        for(i=0;i<6;i++)CHECK(disk_write(0,buffer,4096,counts[i])==RES_OK);
        CHECK(g_sd_write_debug.write_calls==6 && g_sd_write_debug.success_calls==6);
        CHECK(g_sd_write_debug.requested_sectors==156 && g_sd_write_debug.successful_sectors==156);
        CHECK(g_sd_write_debug.single_calls==1 && g_sd_write_debug.multi_calls==5);
        CHECK(g_sd_write_debug.min_sectors==1 && g_sd_write_debug.max_sectors==65);
        CHECK(g_sd_write_debug.over64_calls==1 && g_sd_write_debug.average_sectors_milli==26000);
        for(i=0;i<5;i++)CHECK(g_sd_write_debug.sectors_histogram[counts[i]]==1);
        CHECK(g_sd_write_debug.in_flight==0);
        return 0;
    }
    if(id==62)
    {
        uint32_t before=write_calls;
        CHECK(disk_write(0,NULL,4096,1)==RES_PARERR);
        CHECK(disk_write(0,buffer,4096,0)==RES_PARERR);
        CHECK(disk_write(0,buffer,16384,1)==RES_PARERR);
        test_task=2;CHECK(disk_write(0,buffer,4096,1)==RES_NOTRDY);test_task=1;
        CHECK(g_sd_write_debug.write_calls==0 && write_calls==before);
        return 0;
    }
    if(id==63)
    {
        write_fail_after=0;injected_error=SYS_ERR_TIMEOUT;
        CHECK(disk_write(0,buffer,4096,8)==RES_ERROR);
        CHECK(g_sd_write_debug.write_calls==1 && g_sd_write_debug.failed_calls==1);
        CHECK(g_sd_write_debug.requested_sectors==8 && g_sd_write_debug.successful_sectors==0);
        CHECK(g_sd_write_debug.in_flight==0);
        return 0;
    }
    if(id==64 || id==65)
    {
        uint32_t session=g_sd_write_debug.session, calls;
        CHECK(StorageFS_Write(f,buffer,1024,&n)==SYS_OK && n==1024);
        CHECK(StorageFS_Close(f)==SYS_OK);calls=g_sd_write_debug.write_calls;
        CHECK(calls>0);CHECK(StorageFS_Unmount()==SYS_OK);
        CHECK(g_sd_write_debug.session==session && g_sd_write_debug.write_calls==calls);
        if(id==65)
        {
            CHECK(StorageFS_Mount(true,0)==SYS_OK);
            CHECK(g_sd_write_debug.session==session+1 && g_sd_write_debug.write_calls==0);
            CHECK(g_sd_write_debug.layout_valid==1);
        }
        return 0;
    }
    if(id==66)
    {
        uint32_t before=write_calls;
        g_sd_write_debug.write_calls=UINT32_MAX;
        CHECK(disk_write(0,buffer,4096,1)==RES_OK);
        CHECK(write_calls==before+1 && g_sd_write_debug.saturated==1);
        CHECK(g_sd_write_debug.write_calls==UINT32_MAX);
        return 0;
    }
    if(id==67)
    {
        CHECK(s_volume.csize==1);
        for(i=0;i<16385;i++)buffer[i]=(uint8_t)i;
        CHECK(StorageFS_Write(f,buffer,16385,&n)==SYS_OK && n==16385);
        CHECK(StorageFS_Sync(f)==SYS_OK);
        CHECK(g_sd_write_debug.write_calls>0);
        CHECK(g_sd_write_debug.single_calls==g_sd_write_debug.write_calls);
        CHECK(g_sd_write_debug.multi_calls==0 && g_sd_write_debug.max_sectors==1);
        CHECK(g_sd_write_debug.requested_sectors==g_sd_write_debug.write_calls);
        CHECK(StorageFS_Seek(f,0)==SYS_OK);
        CHECK(StorageFS_Read(f,verify,16385,&got)==SYS_OK && got==16385);
        CHECK(memcmp(buffer,verify,got)==0);
        return 0;
    }

    if(id>=54 && id<=59)
    {
        prepare_file=f;
        for(i=0;i<16385U;i++)buffer[i]=(uint8_t)(i*13U+7U);
        if(id==55)prepare_fail_after=0;
        if(id==56)prepare_fail_after=1;
        if(id==58)prepare_reenter=true;
        if(id==57)
        {
            CHECK(StorageFS_Write(f,"12345678",8,&n)==SYS_OK && n==8);
            CHECK(StorageFS_Write(f,buffer,2000,&n)==SYS_OK && n==2000);
            CHECK(StorageFS_Write(f,buffer+2000,2805,&n)==SYS_OK && n==2805);
            CHECK(StorageFS_Seek(f,0)==SYS_OK);
            CHECK(StorageFS_Read(f,verify,4813,&n)==SYS_OK && n==4813);
            CHECK(memcmp(verify,"12345678",8)==0 && memcmp(verify+8,buffer,4805)==0);
            return 0;
        }
        result=StorageFS_Write(f,buffer,16385,&n);
        if(id==55){CHECK(result==SYS_ERR_TIMEOUT && n==0 && prepare_calls==0);CHECK(!s_busy && s_diag.state==STORAGE_FS_MOUNTED);return 0;}
        if(id==56)
        {
            CHECK(result==SYS_ERR_TIMEOUT && n==8192 && prepare_calls==1);
            CHECK(!s_busy);prepare_fail_after=-1;
            CHECK(StorageFS_Write(f,buffer+8192,8193,&n)==SYS_OK && n==8193);
        }
        else CHECK(result==SYS_OK && n==16385 && prepare_calls==3);
        if(id==59)
        {
            CHECK(((uint32_t)s_write_input&31U)==0);
            CHECK((uint32_t)s_write_input<0x24000000UL);
        }
        CHECK(StorageFS_Sync(f)==SYS_OK);CHECK(StorageFS_Seek(f,0)==SYS_OK);
        CHECK(StorageFS_Read(f,verify,16385,&n)==SYS_OK && n==16385);
        CHECK(memcmp(verify,buffer,16385)==0);CHECK(StorageFS_Close(f)==SYS_OK);
        return 0;
    }

    if(id==9){old=f;CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",3,&f)==SYS_OK);CHECK(old!=f);CHECK(StorageFS_GetSize(old,&n)==SYS_ERR_INVALID_STATE);for(i=1;i<4;i++){char name[]="0:/T0.BIN";name[4]=(char)('0'+i);CHECK(StorageFS_Open(name,6,&h[i])==SYS_OK);}CHECK(StorageFS_Open("0:/MORE.BIN",6,&old)==SYS_ERR_NO_RESOURCE);return 0;}
    if(id==16){test_present=false;CHECK(StorageFS_GetSize(f,&n)==SYS_ERR_NOT_FOUND);CHECK(s_diag.state==STORAGE_FS_FAULT);CHECK(StorageFS_Recover()==SYS_OK);return 0;}
    if(id==27){CHECK(StorageFS_Close(f)==SYS_OK);for(i=0;i<4;i++)s_files[i].generation=STORAGE_GENERATION_MAX;CHECK(StorageFS_Open("0:/NEW.BIN",6,&f)==SYS_ERR_NO_RESOURCE);return 0;}
    if(id==31){s_files[(f&255U)-1U].object.fptr=UINT32_MAX-2U;CHECK(StorageFS_Write(f,buffer,4,&n)==SYS_ERR_NOT_SUPPORTED);return 0;}
    if(id==32){CHECK(StorageFS_Open("0:/TEST.BIN",6,&old)==SYS_ERR_BUSY);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",6,&old)==SYS_ERR_ALREADY_EXISTS);CHECK(StorageFS_Open("0:/TEST.BIN",2,&f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",2,&old)==SYS_ERR_BUSY);return 0;}
    if(id==33){CHECK(StorageFS_Unmount()==SYS_ERR_BUSY);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Unmount()==SYS_OK);return 0;}
    if(id==38){before=read_calls;test_ipsr=16;CHECK(disk_read(0,buffer,0,1)==RES_NOTRDY);CHECK(StorageFS_GetSize(f,&n)==SYS_ERR_INVALID_STATE);CHECK(FatFsDisk_GetLastResult()==SYS_OK);CHECK(read_calls==before);return 0;}
    for(i=0;i<131109U;i++)buffer[i]=(uint8_t)(i*13U+7U);
    if(id==41){CHECK(StorageFS_Write(f,buffer,4097,&n)==SYS_OK);CHECK(n==4097);CHECK(StorageFS_Sync(f)==SYS_OK);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",1,&f)==SYS_OK);CHECK(StorageFS_Read(f,verify,4098,&got)==SYS_OK);CHECK(got==4097);CHECK(memcmp(buffer,verify,got)==0);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Unmount()==SYS_OK);return 0;}
    if(id==42){CHECK(StorageFS_Write(f,buffer,37,&n)==SYS_OK);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",STORAGE_FS_OPEN_WRITE|STORAGE_FS_OPEN_APPEND,&f)==SYS_OK);CHECK(StorageFS_Write(f,buffer,19,&n)==SYS_OK);CHECK(StorageFS_Sync(f)==SYS_OK);CHECK(StorageFS_GetSize(f,&n)==SYS_OK);CHECK(n==56);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",1,&f)==SYS_OK);CHECK(StorageFS_Read(f,verify,56,&n)==SYS_OK);CHECK(memcmp(buffer,verify,37)==0 && memcmp(buffer,verify+37,19)==0);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Unmount()==SYS_OK);return 0;}
    if(id==52){CHECK(StorageFS_Seek(f,131109U)==SYS_ERR_NO_RESOURCE);CHECK(StorageFS_GetDiagnostics(&d)==SYS_OK);CHECK(d.fatfs_called==1 && d.fatfs_result==FR_OK && d.last_result==SYS_ERR_NO_RESOURCE);CHECK(StorageFS_Close(f)==SYS_OK);return 0;}
    if(id==53){CHECK(StorageFS_Read(f,NULL,1,&n)==SYS_ERR_INVALID_ARG);CHECK(StorageFS_Write(f,NULL,1,&n)==SYS_ERR_INVALID_ARG);CHECK(StorageFS_GetSize(f,NULL)==SYS_ERR_INVALID_ARG);CHECK(StorageFS_Close(f)==SYS_OK);CHECK(StorageFS_Open("0:/TEST.BIN",1,&f)==SYS_OK);CHECK(StorageFS_Read(f,NULL,0,&n)==SYS_OK && n==0);CHECK(StorageFS_Seek(f,1)==SYS_ERR_INVALID_ARG);CHECK(StorageFS_Close(f)==SYS_OK);return 0;}
    if(id==11){CHECK(StorageFS_Write(f,buffer,131109U,&n)==SYS_ERR_NO_RESOURCE);CHECK(n>0 && n<131109U);CHECK(StorageFS_Sync(f)==SYS_OK);CHECK(StorageFS_Close(f)==SYS_OK);return 0;}
    if(id==13){write_fail_after=0;injected_error=SYS_ERR_TIMEOUT;CHECK(StorageFS_Write(f,buffer,131109U,&n)==SYS_ERR_TIMEOUT);CHECK(n<131109U);CHECK(s_diag.state==STORAGE_FS_FAULT);CHECK(StorageFS_Recover()==SYS_OK);return 0;}
    if(id==14 || id==15 || id==20){CHECK(StorageFS_Write(f,buffer,37,&n)==SYS_OK);write_fail_after=0;result=id==15?StorageFS_Close(f):StorageFS_Sync(f);CHECK(result==SYS_ERR_IO);CHECK(s_diag.data_may_be_lost==1);CHECK(SD_Block_Claim(SD_BLOCK_CLIENT_RAW)==SYS_ERR_BUSY);if(id==20){fail_recover=true;CHECK(StorageFS_Recover()==SYS_ERR_IO);CHECK(s_lease);fail_recover=false;}CHECK(StorageFS_Recover()==SYS_OK);CHECK(StorageFS_Mount(true,0)==SYS_OK);CHECK(StorageFS_GetSize(f,&n)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==36){CHECK(StorageFS_Write(f,buffer,37,&n)==SYS_OK);CHECK(StorageFS_Close(f)==SYS_OK);read_fail_after=0;injected_error=SYS_ERR_IO;CHECK(StorageFS_Open("0:/TEST.BIN",1,&f)==SYS_OK);CHECK(StorageFS_Read(f,verify,37,&n)==SYS_ERR_IO);CHECK(StorageFS_Recover()==SYS_OK);return 0;}
    CHECK(StorageFS_Write(f,buffer,131109U,&n)==SYS_OK);CHECK(n==131109U);
    CHECK(StorageFS_Sync(f)==SYS_OK);CHECK(StorageFS_Close(f)==SYS_OK);old=f;
    if(id==26){CHECK(StorageFS_Unmount()==SYS_OK);CHECK(StorageFS_Mount(true,0)==SYS_OK);}
    CHECK(StorageFS_Open("0:/TEST.BIN",1,&f)==SYS_OK);CHECK(f!=old);
    CHECK(StorageFS_GetSize(f,&n)==SYS_OK);CHECK(n==131109U);
    CHECK(StorageFS_Read(f,verify,id==10?131110U:131109U,&got)==SYS_OK);CHECK(got==n);CHECK(memcmp(buffer,verify,n)==0);
    CHECK(StorageFS_Read(f,verify,1,&got)==SYS_OK);CHECK(got==0);
    CHECK(StorageFS_Seek(f,0)==SYS_OK);CHECK(StorageFS_Read(f,verify,37,&got)==SYS_OK);CHECK(got==37);
    CHECK(StorageFS_Close(f)==SYS_OK);
    CHECK(StorageFS_Mkdir("0:/DIR")==SYS_OK);CHECK(StorageFS_Remove("0:/DIR")==SYS_OK);
    CHECK(StorageFS_GetSpace(&total,&free_bytes)==SYS_OK);CHECK(total>131109U && free_bytes<total);
    CHECK(StorageFS_GetDiagnostics(&d)==SYS_OK);CHECK(d.open_files==0);
    CHECK(StorageFS_Unmount()==SYS_OK);return 0;
}
