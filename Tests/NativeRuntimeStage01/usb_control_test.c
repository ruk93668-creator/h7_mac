//复用原HAL硬件桩，实际执行Native任务、USB BSP/Platform/Core/Mode
#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Platform/platform_rtos.c"
#include "../../Common/Src/runtime_event.c"
#include "../../CM7/Application/Tasks/camera_task.c"
#include "../../CM7/Application/Tasks/usb_task.c"
#include "../../CM7/Application/Tasks/control_task.c"
#include "../../CM7/Application/app_tasks.c"
#include "../../CM7/Application/app_main.c"
uint32_t SystemCoreClock=480000000U;
void __aeabi_memset(void *d,size_t n,int v){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
extern void vTaskSwitchContext(void);
static int Prepare(void){
 __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
 EQ(App_TasksCreate(),SYS_OK);EQ(Framework_InitRuntime(),SYS_OK);EQ(start_count,0);
 EQ(App_TasksRecordInitialization(SYS_OK),SYS_OK);return 0;
}
static int StartNative(void){
 uint32_t events;
 EQ(Prepare(),0);EQ(Platform_RtosStart(),SYS_ERR_INVALID_STATE);
 vTaskSuspend(s_usb_task);vTaskSuspend(s_camera_task);vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),s_control_task);
 EQ(App_TasksReleaseBoot(),SYS_OK);vTaskResume(s_usb_task);vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),s_usb_task);
 EQ(App_EventsWait(RUNTIME_TARGET_USB,0,&events),SYS_OK);EQ(events&RUNTIME_EVENT_BOOT_READY,RUNTIME_EVENT_BOOT_READY);
 UsbTask_Process(events);return 0;
}
int NativeUsb_TestRun(uint32_t id){
 uint32_t events;
 if(id==0){EQ(Prepare(),0);EQ(start_count,0);EQ(s_usb_mode.state,USB_MODE_STATE_IDLE);return 0;}
 if(id==10){__HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;EQ(App_TasksCreate(),SYS_OK);fault=4;EQ(Framework_InitRuntime(),SYS_ERR_IO);EQ(App_TasksRecordInitialization(SYS_ERR_IO),SYS_ERR_IO);EQ(start_count,0);return 0;}
 if(id==9)fault=7;
 EQ(StartNative(),0);
 if(id==9){EQ(g_usb_task_debug.result,SYS_ERR_BUSY);EQ(s_usb_started,false);EQ(s_usb_mode.state,USB_MODE_STATE_ERROR);return 0;}
 EQ(g_usb_task_debug.result,SYS_OK);EQ(start_count,1);EQ(s_usb_mode.state,USB_MODE_STATE_WAIT_CONFIGURED);
 if(id==1)return 0;
 if(id==5){UsbTask_Process(RUNTIME_EVENT_BOOT_READY);EQ(start_count,1);return 0;}
 if(id==6){s_test_tick=5000;UsbTask_Process(0);EQ(s_usb_mode.state,USB_MODE_STATE_ERROR);EQ(g_usb_task_debug.result,SYS_ERR_TIMEOUT);return 0;}
 HAL_PCD_ResetCallback(&s_pcd);
 EQ(s_endpoints[0][0].opened,true);EQ(s_endpoints[1][0].opened,true);
 if(id==2){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x0100,0,18);EQ(tx_size,18);EQ(tx_data[0],18);EQ(stall_count,0);return 0;}
 if(id==3){Request(0,USB_REQ_SET_ADDRESS,7,0,0);EQ(address_value,7);EQ(tx_size,0);EQ(stall_count,0);return 0;}
 if(id==7){EQ(App_EventsWait(RUNTIME_TARGET_USB,0,&events),SYS_OK);EQ(events&RUNTIME_EVENT_USB_PROGRESS,RUNTIME_EVENT_USB_PROGRESS);return 0;}
 if(id==14){Request(0x81,USB_REQ_SET_INTERFACE,1,0,0);EQ(s_endpoints[0][0].stalled,true);EQ(s_endpoints[1][0].stalled,true);EQ(BSP_USB_IsStallEp(0),true);EQ(BSP_USB_IsStallEp(0x80),true);Request(0x80,USB_REQ_GET_DESCRIPTOR,0x0100,0,18);EQ(tx_size,18);EQ(stall_count,0);return 0;}
 if(id==11){Request(0x80,0xff,0,0,0);EQ(s_endpoints[0][0].stalled,true);EQ(s_endpoints[1][0].stalled,true);EQ(BSP_USB_IsStallEp(0),true);EQ(BSP_USB_IsStallEp(0x80),true);Request(0x80,USB_REQ_GET_DESCRIPTOR,0x0100,0,18);EQ(tx_size,18);EQ(stall_count,0);return 0;}
 if(id==12){HAL_PCD_ResetCallback(&s_pcd);EQ(s_endpoints[0][0].opened,true);EQ(s_endpoints[1][0].opened,true);EQ(start_count,1);return 0;}
 if(id==13){EQ(USB_Device_SetEventCallback(s_event_callback,s_event_context),SYS_ERR_INVALID_STATE);return 0;}
 Configured();UsbTask_Process(0);EQ(s_usb_mode.state,USB_MODE_STATE_RUNNING);EQ(USB_Device_IsConfigured(),true);
 if(id==4)return 0;
 if(id==8){HAL_PCD_DisconnectCallback(&s_pcd);EQ(USB_Device_IsConfigured(),false);EQ(App_EventsWait(RUNTIME_TARGET_USB,0,&events),SYS_OK);EQ(events&RUNTIME_EVENT_USB_PROGRESS,RUNTIME_EVENT_USB_PROGRESS);return 0;}
 return 1;
}
