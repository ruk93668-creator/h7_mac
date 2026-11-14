//执行真实验收适配器，公共业务入口用只记录请求的桩替换
#include "HardwareControl.c"
#include <string.h>
#include <stddef.h>

//保存测试失败行和公共接口调用次数
volatile uint32_t g_failure;
static uint32_t calls[6], exception, primask, basepri, barriers, race;
static bool control_owner=true, scheduler_running=true, reenter, change_during_dispatch;
static SysResult_t request_result, nested_result;
#define CHECK(x) do { if(!(x)){g_failure=__LINE__;return __LINE__;} } while(0)

//提供可验证的任务侧上下文
//提供测试中断上下文
uint32_t Platform_SyncGetException(void){return exception;}
//提供测试PRIMASK状态
uint32_t Platform_SyncGetPrimask(void){return primask;}
//提供测试BASEPRI状态
uint32_t Platform_SyncGetBasepri(void){return basepri;}
//确认测试Control任务身份
bool App_MessagesIsOwner(RuntimeEventTarget_t target){return control_owner && scheduler_running && target==RUNTIME_TARGET_CONTROL;}

//仅在测试中模拟调试器在读取窗口修改输入
//提供测试内存屏障边界
void Platform_SyncMemoryBarrier(void)
{
    barriers++;
    if(barriers==2U && race==1U)g_r2_hw.submit_seq++;
    if(barriers==2U && race==2U)g_r2_hw.command++;
}

//记录实际选择的公共请求入口
static SysResult_t Request(uint32_t index)
{
    calls[index]++;
    if(reenter)nested_result=R2_HardwareControlProcess();
    if(change_during_dispatch){g_r2_hw.command=2U;g_r2_hw.submit_seq=2U;}
    return request_result;
}
//记录系统启动请求
SysResult_t ControlTask_RequestStart(void){return Request(0U);}
//记录系统停止请求
SysResult_t ControlTask_RequestStop(void){return Request(1U);}
//记录录像请求
SysResult_t App_StorageStartRecording(void){return Request(2U);}
//记录抓拍请求
SysResult_t App_StorageRequestSnapshot(void){return Request(3U);}
//记录存储停止请求
SysResult_t App_StorageStop(void){return Request(4U);}
//记录存储恢复请求
SysResult_t App_StorageRecover(void){return Request(5U);}

//以最后写提交号的方式发布一条请求
static void Submit(uint32_t command,uint32_t sequence)
{
    g_r2_hw.command=command;g_r2_hw.submit_seq=sequence;
}

//验证单次投递、请求冻结、上下文及序号边界
int R2_MailboxRun(uint32_t id)
{
    uint32_t i,total=0U;SysResult_t result;
    CHECK(sizeof(g_r2_hw)==24U && offsetof(R2HardwareMailbox_t,dispatch_result)==20U);
    if(id==0U){CHECK(R2_HardwareControlProcess()==SYS_OK && g_r2_hw.ready && !g_r2_hw.processed_seq);return 0;}
    if(id>=1U && id<=6U)
    {
        Submit(id,1U);CHECK(R2_HardwareControlProcess()==SYS_OK);
        CHECK(calls[id-1U]==1U && g_r2_hw.processed_seq==1U && g_r2_hw.processed_command==id && g_r2_hw.dispatch_result==SYS_OK);return 0;
    }
    Submit(1U,1U);
    if(id==7U)g_r2_hw.command=7U;
    if(id==11U)request_result=SYS_ERR_NO_RESOURCE;
    if(id==12U)request_result=SYS_ERR_BUSY;
    if(id==13U)control_owner=false;
    if(id==14U)exception=16U;
    if(id==15U)primask=1U;
    if(id==16U)basepri=0x50U;
    if(id==17U)scheduler_running=false;
    if(id==18U)race=1U;
    if(id==19U)race=2U;
    if(id==20U)reenter=true;
    if(id==21U)change_during_dispatch=true;
    if(id==22U){s_processed_sequence=UINT32_MAX-1U;g_r2_hw.submit_seq=UINT32_MAX;}
    if(id==23U)g_r2_hw.submit_seq=3U;
    if(id==24U)g_r2_hw.command=0U;
    if(id==25U)g_r2_hw.command=UINT32_MAX;
    if(id==26U)g_r2_hw.submit_seq=0U;
    if(id==27U)basepri=0x50U;
    if(id==29U){g_r2_hw.command=6U;request_result=SYS_ERR_IO;}
    result=R2_HardwareControlProcess();
    if((id>=13U && id<=17U) || id==27U)
    {
        CHECK(result==SYS_ERR_INVALID_STATE && !g_r2_hw.processed_seq && !g_r2_hw.ready);
        for(i=0U;i<6U;i++)CHECK(calls[i]==0U);
        if(id==27U){basepri=0U;CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==1U);}return 0;
    }
    if(id==18U || id==19U)
    {
        CHECK(result==SYS_ERR_BUSY && !s_processed_sequence && !g_r2_hw.processed_seq && calls[0]==0U);
        race=0U;Submit(1U,1U);CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==1U);return 0;
    }
    if(id==7U || id==24U || id==25U)
    {
        CHECK(result==SYS_ERR_INVALID_ARG && g_r2_hw.processed_seq==1U);
        for(i=0U;i<6U;i++)CHECK(calls[i]==0U);return 0;
    }
    if(id==23U)
    {
        CHECK(result==SYS_ERR_PROTOCOL && calls[0]==0U && g_r2_hw.processed_seq==3U);
        Submit(2U,4U);CHECK(R2_HardwareControlProcess()==SYS_OK && calls[1]==1U);return 0;
    }
    if(id==26U){CHECK(result==SYS_OK && calls[0]==0U && !g_r2_hw.processed_seq);return 0;}
    if(id==29U){CHECK(result==SYS_ERR_IO && calls[5]==1U && g_r2_hw.dispatch_result==SYS_ERR_IO);return 0;}
    if(id==11U || id==12U)
    {
        CHECK(result==request_result && g_r2_hw.processed_seq==1U && calls[0]==1U);
        CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==1U);
        request_result=SYS_OK;Submit(1U,2U);CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==2U);return 0;
    }
    CHECK(result==SYS_OK && calls[0]==1U);
    if(id==8U){CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==1U);}
    if(id==9U){Submit(2U,0U);CHECK(R2_HardwareControlProcess()==SYS_OK && calls[1]==0U);}
    if(id==10U){g_r2_hw.processed_seq=0U;g_r2_hw.ready=0U;CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==1U && s_processed_sequence==1U);}
    if(id==20U)CHECK(nested_result==SYS_ERR_BUSY && calls[0]==1U);
    if(id==21U)
    {
        CHECK(g_r2_hw.processed_seq==1U && g_r2_hw.processed_command==1U);
        change_during_dispatch=false;CHECK(R2_HardwareControlProcess()==SYS_OK && calls[1]==1U && g_r2_hw.processed_seq==2U);
    }
    if(id==22U){Submit(1U,1U);CHECK(R2_HardwareControlProcess()==SYS_OK && calls[0]==1U && s_processed_sequence==UINT32_MAX);}
    if(id==28U)
    {
        for(i=2U;i<=6U;i++){Submit(i,i);CHECK(R2_HardwareControlProcess()==SYS_OK);}
        for(i=0U;i<6U;i++){CHECK(calls[i]==1U);total+=calls[i];}CHECK(total==6U);
    }
    return 0;
}
