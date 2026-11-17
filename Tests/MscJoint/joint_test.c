//端到端软件时序：真实BOT+邮箱+控制事务，设备模拟在执行中被Reset抢占
#define Protocol_Run Protocol_PreviousRun
#include "../MscProtocol/protocol_test.c"
#undef Protocol_Run
#include "../../CM7/Service/usb_msc_storage.c"
static uint32_t s_joint_case,s_joint_exec;
static bool s_joint_quiet=true;
static MscBlockRequest_t s_joint_old;
uint32_t Platform_SyncGetPrimask(void){return __get_PRIMASK();}
void Platform_SyncDisableIrq(void){__disable_irq();}
void Platform_SyncRestoreIrq(uint32_t mask){__set_PRIMASK(mask);}
void Platform_SyncMemoryBarrier(void){__DMB();}
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target,uint32_t events){(void)target;(void)events;return SYS_OK;}
SysResult_t RuntimeEvent_PostFromIsr(RuntimeEventTarget_t target,uint32_t events){return RuntimeEvent_Post(target,events);}
static SysResult_t Joint_Init(void){return SYS_OK;}
static void Joint_Media(MscScsiMedia_t *media,uint32_t *generation)
{media->block_count=16;media->present=true;media->ready=true;media->write_protected=false;*generation=1;}
static void Joint_Execute(const MscBlockRequest_t *request,MscBlockCompletion_t *completion)
{
    s_joint_exec++;s_joint_old=*request;completion->result=SYS_OK;completion->completed_blocks=request->blocks;
    if(s_joint_case==4U){completion->result=SYS_ERR_IO;completion->completed_blocks=1;return;}
    s_joint_quiet=false;
    if(s_joint_case==1U)HAL_PCD_ResetCallback(&s_pcd);
    else if(s_joint_case==2U)HAL_PCD_DisconnectCallback(&s_pcd);
    else Request(0x21,0xFF,0,0,0);
}
static bool Joint_Quiet(void){return s_joint_quiet;}
static const MscStorageDeviceOps_t s_joint_device={Joint_Init,Joint_Media,Joint_Execute,Joint_Quiet};
int Joint_Run(uint32_t scenario)
{
    uint8_t data[1024];MscBlockCompletion_t done;
    s_joint_case=scenario;EQ(USB_MSC_StorageInit(&s_joint_device),SYS_OK);
    if(Protocol_Init())return 1;
    Protocol_Cbw(0x2A,1024,false,2);EQ(s_bot.state,BOT_OUT);memset(data,0x5A,sizeof(data));
    Protocol_Out(data,1024);Protocol_Tick();EQ(s_bot.state,BOT_WRITE);Protocol_Tick();EQ(s_bot.state,BOT_WRITE);EQ(s_joint_exec,0);
    USB_MSC_StorageProcess();EQ(s_joint_exec,1);
    if(scenario==4U)
    {
        Protocol_Tick();EQ(s_bot.state,BOT_FINISH);Protocol_Tick();EQ(s_bot.state,BOT_CSW);
        EQ(s_bot.csw[12],1);EQ(MscBot_Read32(s_bot.csw+8),512);EQ(s_bot.scsi.sense_key,3);return 0;
    }
    EQ(MscStorage_Drained(s_joint_old.epoch),false);EQ(s_storage.state,MSC_MAIL_DRAIN);
    if(scenario==1U||scenario==2U)
    {
        EQ(s_bot.active,false);HAL_PCD_ResetCallback(&s_pcd);Configured();Protocol_Tick();EQ(s_bot.state,BOT_START);
        EQ(s_endpoints[0][2].busy,false);s_joint_quiet=true;USB_MSC_StorageProcess();Protocol_Tick();EQ(s_bot.state,BOT_CBW);return 0;
    }
    EQ(s_bot.state,BOT_RESET_DRAIN);Protocol_Tick();EQ(s_endpoints[1][0].busy,false);EQ(s_bot.state,BOT_RESET_DRAIN);
    if(scenario==3U)
    {
        Request(0x80,6,0x100,0,18);Protocol_Tick();EQ(s_bot.state,BOT_FAULT);
        EQ(MscStorage_Drained(s_joint_old.epoch),false);
    }
    s_joint_quiet=true;USB_MSC_StorageProcess();EQ(MscStorage_Drained(s_joint_old.epoch),true);
    if(scenario==3U)Request(0x21,0xFF,0,0,0);
    Protocol_Tick();EQ(s_bot.state,BOT_RESET_STATUS);EQ(MscStorage_Poll(&s_joint_old,&done),SYS_ERR_INVALID_STATE);
    HAL_PCD_DataInStageCallback(&s_pcd,0);Request(2,1,0,0x82,0);Request(2,1,0,2,0);Protocol_Tick();
    EQ(s_bot.state,BOT_CBW);HAL_PCD_DataInStageCallback(&s_pcd,2);EQ(s_bot.state,BOT_CBW);return 0;
}
