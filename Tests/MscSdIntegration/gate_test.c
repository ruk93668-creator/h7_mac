//直接尝试真实RAW/FATFS租约与StorageFS入口，验证受控模式代码门禁
#include "../../CM7/Platform/sd_block.c"
#include "../../CM7/Platform/storage_fs.c"
volatile struct {uint32_t fail_line,checks;} g_result;
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (TaskHandle_t)1;}
BaseType_t xTaskGetSchedulerState(void){return taskSCHEDULER_RUNNING;}
static int Eq(uint32_t line,uint32_t a,uint32_t b){g_result.checks++;if(a!=b){g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Eq(__LINE__,(uint32_t)(a),(uint32_t)(b)))return 1;}while(0)
int Gate_Run(uint32_t scenario)
{
    (void)scenario;
    EQ(SD_Block_Claim(SD_BLOCK_CLIENT_FATFS),SYS_ERR_INVALID_STATE);
    EQ(StorageFS_Mount(true,0),SYS_ERR_INVALID_STATE);
    EQ(StorageFS_Mount(false,0),SYS_ERR_INVALID_STATE);
    s_event = (SemaphoreHandle_t)1;
    s_prepare_result = SYS_OK; //模拟Framework已准备同步资源，仍执行真实租约检查
    EQ(SD_Block_Claim(SD_BLOCK_CLIENT_RAW),SYS_OK);
    EQ(SD_Block_IsOwner(SD_BLOCK_CLIENT_RAW),true);
    EQ(SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS),false);
    EQ(SD_Block_Claim(SD_BLOCK_CLIENT_FATFS),SYS_ERR_INVALID_STATE);
    s_diag.state=STORAGE_FS_MOUNTED;
    EQ(Storage_Enter(),SYS_ERR_INVALID_STATE);
    EQ(s_owner,(TaskHandle_t)1);
    return 0;
}

int Gate_Unexpected(void) { g_result.fail_line=9999; return SYS_ERR_IO; }
