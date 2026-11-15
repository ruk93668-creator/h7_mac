#define App_MessagesSend B1_LegacySend
#define App_MessagesReceive B1_LegacyReceive
#define App_MessagesIsOwner B1_LegacyOwner
#include "../CameraModuleB1/behavior_test.c"
#undef App_MessagesSend
#undef App_MessagesReceive
#undef App_MessagesIsOwner
bool App_MessagesIsOwner(RuntimeEventTarget_t target);
bool App_MessagesReceive(AppChannel_t channel,AppMessage_t *message);
SysResult_t App_MessagesSend(AppChannel_t channel,const AppMessage_t *message);
#include "../../CM7/Application/Tasks/control_task.c"
static AppMessage_t control_in[4],control_out[8];
static uint32_t reads,incoming,outgoing,bad_channel;
bool App_MessagesIsOwner(RuntimeEventTarget_t target){return target==RUNTIME_TARGET_CONTROL;}
bool App_MessagesReceive(AppChannel_t channel,AppMessage_t *message)
{(void)channel;if(reads==incoming)return false;*message=control_in[reads++];return true;}
SysResult_t App_MessagesSend(AppChannel_t channel,const AppMessage_t *message)
{if(channel!=APP_CHANNEL_USB_COMMAND)bad_channel++;control_out[outgoing++]=*message;return SYS_OK;}
#define VERIFY(x) do{if(!(x)){g_failure=__LINE__;return g_failure;}}while(0)
int ControlIntegration_Run(uint32_t id)
{
 if(id==1)config.output=VIDEO_OUTPUT_NONE;
 if(id==2)config.output=VIDEO_OUTPUT_USB_TEST;
 if(id==5){ControlTask_Command(CONTROL_STOP_CAMERA);VERIFY(s_control_state==CONTROL_ERROR&&outgoing==0);return 0;}
 ControlTask_Process(RUNTIME_EVENT_BOOT_READY);
 if(id==1){VERIFY(outgoing==0&&s_control_state==CONTROL_IDLE);return 0;}
 VERIFY(outgoing==1&&control_out[0].type==APP_MESSAGE_START&&bad_channel==0);
 if(id<=2)return 0;
 control_in[0]=control_out[0];control_in[0].type=APP_MESSAGE_ACK;
 control_in[0].source=RUNTIME_TARGET_USB;control_in[0].result=SYS_OK;incoming=1;
 ControlTask_Process(0);VERIFY(s_control_state==CONTROL_RUNNING);
 control_in[1].type=id==4?APP_MESSAGE_FAULT:APP_MESSAGE_STOP;
 control_in[1].source=id==4?RUNTIME_TARGET_CAMERA:RUNTIME_TARGET_CONTROL;
 control_in[1].result=id==4?SYS_ERR_IO:SYS_OK;incoming=2;
 ControlTask_Process(0);VERIFY(outgoing==2&&control_out[1].type==APP_MESSAGE_STOP&&bad_channel==0);
 control_in[2]=control_out[1];control_in[2].type=APP_MESSAGE_ACK;
 control_in[2].source=RUNTIME_TARGET_USB;control_in[2].result=SYS_OK;incoming=3;
 ControlTask_Process(0);VERIFY(s_control_state==CONTROL_IDLE&&outgoing==2&&bad_channel==0);return 0;
}

SysResult_t App_StatusPublishControl(const AppControlStatus_t *s){(void)s;return SYS_OK;}
