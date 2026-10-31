//复用既有HAL桩，实际执行当前正式BSP、Platform、Core和Mode代码
#define Stream_TestRun UsbLegacy_TestRun
#include "legacy_test.c"
#undef Stream_TestRun
#include "../../CM7/Platform/usb_mode.c"
#include "../../CM7/Framework/framework_init.c"

//可控制的32位毫秒时钟
static uint32_t s_test_tick;

//失败时也保存实际状态与硬件调用计数
volatile uint32_t g_mode_observation[12];

static int Mode_Boot(bool configure);

static int Mode_RunCase(uint32_t id);
static int Core_RunCase(uint32_t id);
static int Stage2_RunCase(uint32_t id);

uint32_t Platform_TimeGetMs(void)
{
    return s_test_tick;
}

int Mode_TestRun(uint32_t id)
{
    int result = Mode_RunCase(id);
    g_mode_observation[0] = s_usb_mode.state;
    g_mode_observation[1] = s_usb_mode.mode;
    g_mode_observation[2] = s_usb_mode.target_mode;
    g_mode_observation[3] = (uint32_t)s_usb_mode.last_result;
    g_mode_observation[4] = s_usb_mode.state_enter_ms;
    g_mode_observation[5] = s_test_tick;
    g_mode_observation[6] = start_count;
    g_mode_observation[7] = stop_count;
    g_mode_observation[8] = s_device.dev_state;
    g_mode_observation[9] = (uint32_t)USB_Device_GetLastResult();
    g_mode_observation[10] = nvic;
    g_mode_observation[11] = s_usb_mode.initialized;
    return result;
}

//建立TEST连接，可选经过真实Core配置请求进入RUNNING
static int Mode_Boot(bool configure)
{
    EQ(USB_Device_Init(), SYS_OK); EQ(Usb_TestRegister(), SYS_OK);
    EQ(USB_Mode_Init(), SYS_OK);
    EQ(USB_Mode_Start(), SYS_OK);
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
    if (configure)
    {
        HAL_PCD_ResetCallback(&s_pcd);
        Configured();
        USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
    }
    return 0;
}

//每个用例从全新的模拟器内存开始
static int Mode_RunCase(uint32_t id)
{
    uint32_t i;
    const FrameworkInitEntry_t *entry;
    uint32_t service_entries = 0U, start_entries = 0U;
    g_result.case_id = id;

    //准备当前正式BSP直接检查的RCC寄存器
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    if (id >= 300U) return Stage2_RunCase(id);
    if (id >= 200U) return Core_RunCase(id);
    if (id < 52U)
    {
        if (id == 4U) RCC->CR &= ~RCC_CR_HSI48RDY;
        return UsbLegacy_TestRun(id);
    }
    if (id == 100U)
    {
        EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_INVALID_STATE);
        USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_UNINITIALIZED);
        EQ(start_count, 0U);
        return 0;
    }
    if (id == 101U)
    {
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(s_usb_mode.mode, USB_MODE_TEST);
        EQ(s_usb_mode.state, USB_MODE_STATE_IDLE);
        EQ(init_count, 0U);
        EQ(start_count, 0U);
        return 0;
    }
    if (id == 107U)
    {
        EQ(USB_Mode_Init(), SYS_OK);
        EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
        EQ(s_usb_mode.last_result, SYS_ERR_INVALID_STATE);
        EQ(start_count, 0U);
        return 0;
    }
    if (id == 109U || id == 110U)
    {
        for (entry = Image$$ER_FW_INIT$$Base; entry < Image$$ER_FW_INIT$$Limit; entry++)
        {
            if (entry->init == USB_Mode_Init) {EQ(entry->level, INIT_LEVEL_SERVICE); service_entries++;}
            if (entry->init == UsbMode_AppStart) {EQ(entry->level, INIT_LEVEL_APPLICATION); start_entries++;}
        }
        EQ(service_entries, 1U); EQ(start_entries, 1U);
        EQ(Framework_InitLevel(INIT_LEVEL_DEVICE), SYS_OK);
        EQ(s_initialized, true); EQ(s_usb_mode.initialized, false); EQ(start_count, 0U);
        EQ(Framework_InitLevel(INIT_LEVEL_SERVICE), SYS_OK);
        EQ(s_usb_mode.state, USB_MODE_STATE_IDLE); EQ(start_count, 0U);
        EQ(Framework_InitLevel(INIT_LEVEL_APPLICATION), SYS_OK);
        EQ(start_count, 1U); EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
        if (id == 110U)
        {
            HAL_PCD_ResetCallback(&s_pcd);
            Request(0x80, USB_REQ_GET_DESCRIPTOR, 0x302, 0x409, 64);
            EQ(tx_size, 38U); EQ(stall_count, 0U);
            for (i = 0U; i < 18U; i++)
            {
                EQ(tx_data[2U + 2U * i], "h7_mac USB FS Test"[i]);
                EQ(tx_data[3U + 2U * i], 0U);
            }
            Configured(); USB_Mode_Process();
            EQ(USB_Device_IsConfigured(), true); EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
        }
        return 0;
    }
    if (id == 106U || id == 108U || id == 111U)
    {
        EQ(USB_Device_Init(), SYS_OK); EQ(Usb_TestRegister(), SYS_OK); EQ(USB_Mode_Init(), SYS_OK);
        if (id == 106U)
        {
            fault = 7U;
            EQ(USB_Mode_Start(), SYS_ERR_BUSY); EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
            EQ(s_usb_mode.last_result, SYS_ERR_BUSY); USB_Mode_Process(); EQ(start_count, 1U);
            EQ(USB_Mode_Start(), SYS_ERR_INVALID_STATE);
        }
        else if (id == 108U)
        {
            s_usb_mode.mode = USB_MODE_UVC; EQ(USB_Mode_Start(), SYS_ERR_NOT_SUPPORTED);
            s_usb_mode.mode = USB_MODE_MSC; EQ(USB_Mode_Start(), SYS_ERR_NOT_SUPPORTED);
            EQ(start_count, 0U);
        }
        else
        {
            s_usb_mode.state = USB_MODE_STATE_REGISTERING; USB_Mode_Process();
            EQ(s_usb_mode.state, USB_MODE_STATE_REGISTERING); EQ(start_count, 0U); EQ(stop_count, 0U);
        }
        return 0;
    }
    if (id == 112U || id == 113U || id == 114U || id == 115U || id == 125U || id == 129U)
    {
        s_test_tick = id == 129U ? 0xFFFFFF00U : 100U;
        EQ(Mode_Boot(false), 0);
        if (id == 125U)
        {
            EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_BUSY);
            EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED); EQ(stop_count, 0U); return 0;
        }
        if (id == 115U)
        {
            HAL_PCD_ResetCallback(&s_pcd);
            fault = 9U;
            EQ(USBD_LL_Transmit(&s_device, 0x80U, tx_data, 1U), USBD_BUSY);
            USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_ERROR);
            EQ(s_usb_mode.last_result, SYS_ERR_BUSY); return 0;
        }
        s_test_tick += 4999U; USB_Mode_Process(); EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
        if (id == 112U) return 0;
        if (id == 114U) {HAL_PCD_ResetCallback(&s_pcd); Configured();}
        s_test_tick++; USB_Mode_Process();
        EQ(s_usb_mode.state, id == 114U ? USB_MODE_STATE_RUNNING : USB_MODE_STATE_ERROR);
        EQ(s_usb_mode.last_result, id == 114U ? SYS_OK : SYS_ERR_TIMEOUT); return 0;
    }
    EQ(Mode_Boot(true), 0);
    if (id == 102U || id == 103U || id == 105U)
    {
        if (id == 103U) EQ(USB_Mode_Init(), SYS_OK);
        if (id == 105U) for (i = 0U; i < 1000U; i++) USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING); EQ(USB_Mode_Start(), SYS_ERR_BUSY);
        EQ(start_count, 1U); EQ(stop_count, 0U); return 0;
    }
    if (id == 124U)
    {
        s_usb_mode.state = USB_MODE_STATE_ERROR;
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_BUSY);
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR); return 0;
    }
    if (id == 126U)
    {
        EQ(USB_Mode_RequestReconnect(USB_MODE_UVC), SYS_ERR_NOT_SUPPORTED);
        EQ(USB_Mode_RequestReconnect(USB_MODE_MSC), SYS_ERR_NOT_SUPPORTED);
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING); EQ(stop_count, 0U); return 0;
    }
    s_test_tick = id == 128U ? 0xFFFFFF00U : 1000U;
    EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_OK);
    EQ(s_usb_mode.target_mode, USB_MODE_TEST); EQ(s_usb_mode.state, USB_MODE_STATE_STOPPING);
    if (id == 104U) {EQ(stop_count, 0U); EQ(start_count, 1U); return 0;}
    if (id == 127U)
    {
        EQ(USB_Mode_RequestReconnect(USB_MODE_TEST), SYS_ERR_BUSY);
        EQ(s_usb_mode.state, USB_MODE_STATE_STOPPING); EQ(stop_count, 0U); return 0;
    }
    if (id == 117U) fault = 8U;
    USB_Mode_Process(); EQ(stop_count, 1U);
    if (id == 117U)
    {
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR); EQ(s_usb_mode.last_result, SYS_ERR_IO);
        USB_Mode_Process(); EQ(stop_count, 1U); return 0;
    }
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_RECONNECT);
    EQ(s_usb_mode.state_enter_ms, s_test_tick); EQ(nvic, 0U);
    if (id == 116U) {USB_Mode_Process(); EQ(stop_count, 1U); EQ(start_count, 1U); return 0;}
    s_test_tick += 499U; USB_Mode_Process();
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_RECONNECT); EQ(start_count, 1U);
    if (id == 118U) return 0;
    s_test_tick++; USB_Mode_Process();
    EQ(s_usb_mode.state, USB_MODE_STATE_STARTING); EQ(start_count, 1U);
    if (id == 119U || id == 128U) return 0;
    if (id == 122U || id == 123U) fault = 7U;
    USB_Mode_Process(); EQ(start_count, 2U);
    if (id == 122U)
    {
        EQ(s_usb_mode.state, USB_MODE_STATE_ERROR); EQ(s_usb_mode.last_result, SYS_ERR_BUSY); return 0;
    }
    if (id == 123U) {USB_Mode_Process(); EQ(start_count, 2U); return 0;}
    EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED);
    if (id == 120U) {EQ(s_usb_mode.state_enter_ms, s_test_tick); return 0;}
    if (id == 121U)
    {
        s_test_tick += 4999U; USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_WAIT_CONFIGURED); return 0;
    }
    if (id == 130U || id == 131U)
    {
        HAL_PCD_ResetCallback(&s_pcd); Configured(); USB_Mode_Process();
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING); EQ(USB_Device_IsConfigured(), true);
        EQ(stop_count, 1U); EQ(start_count, 2U);
        if (id == 131U)
        {
            Request(0x80, USB_REQ_GET_DESCRIPTOR, 0x302, 0x409, 64U);
            EQ(tx_size, 38U); EQ(stall_count, 0U);
        }
        return 0;
    }
    EQ(0U, 1U); return 1;
}

static uint8_t core_packet[512];
static uint32_t core_in_count,core_in_ep,core_iso_count,core_setup_count,core_deinit_count;
static bool core_rearm;
static uint32_t core_lifecycle_action;
static SysResult_t core_nested[4];
static USBD_ClassTypeDef core_driver;
static USBD_DescriptorsTypeDef core_descriptors;

//只用于验证Core生命周期重入，不实现设备协议
static void Core_NestedLifecycle(void)
{
    core_nested[0] = USB_Device_Init();
    core_nested[1] = USB_Device_Start();
    core_nested[2] = USB_Device_Stop();
    core_nested[3] = USB_Device_RegisterClass(&core_driver, &core_descriptors);
}

static uint8_t core_config[25]={9,2,25,0,1,1,0,0x80,250,9,4,0,0,1,0xff,0,0,0,7,5,0x81,1,0,2,1};
static uint8_t *Core_Config(uint16_t *length)
{
    if (core_lifecycle_action == 2U)
    {
        core_lifecycle_action = 0U;
        Core_NestedLifecycle();
    }
    *length = 25U;
    return core_config;
}
static uint8_t *Core_Device(USBD_SpeedTypeDef speed,uint16_t *length){static uint8_t desc[20]={18,1,0,2,0,0,0,64,9,18,1,0,0,1,0,0,0,1};(void)speed;*length=18;return desc;}
static uint8_t *Core_BadDevice(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;*length=17;return s_device_desc;}
static uint8_t Core_Init(USBD_HandleTypeDef *p,uint8_t c){(void)p;(void)c;return USBD_OK;}
static uint8_t Core_Deinit(USBD_HandleTypeDef *p,uint8_t c){(void)p;(void)c;core_deinit_count++;
if(core_lifecycle_action==1U){core_lifecycle_action=0U;Core_NestedLifecycle();}
if(core_lifecycle_action==5U)return USBD_FAIL;
return USB_Device_CloseEp(0x81)==SYS_OK?USBD_OK:USBD_FAIL;}
static uint8_t Core_Setup(USBD_HandleTypeDef *p,USBD_SetupReqTypedef *q){core_setup_count++;if(q->bRequest==0x55){USBD_CtlSendStatus(p);return USBD_OK;}return USBD_FAIL;}
static uint8_t Core_In(USBD_HandleTypeDef *p,uint8_t ep){(void)p;core_in_count++;core_in_ep=ep;if(core_rearm)return USB_Device_Transmit(0x81,core_packet,512)==SYS_OK?USBD_OK:USBD_FAIL;return USBD_OK;}
static uint8_t Core_Iso(USBD_HandleTypeDef *p,uint8_t ep){(void)p;core_iso_count++;core_in_ep=ep;return USBD_OK;}
static USBD_ClassTypeDef core_driver={.Init=Core_Init,.DeInit=Core_Deinit,.Setup=Core_Setup,.DataIn=Core_In,.IsoINIncomplete=Core_Iso,.GetFSConfigDescriptor=Core_Config};
static USBD_DescriptorsTypeDef core_descriptors={.GetDeviceDescriptor=Core_Device};
static int Core_Boot(void)
{
 EQ(USB_Device_Init(),SYS_OK);EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_OK);
 EQ(USB_Device_Start(),SYS_OK);HAL_PCD_ResetCallback(&s_pcd);Configured();EQ(USB_Device_IsConfigured(),true);return 0;
}
static int Core_RunCase(uint32_t id)
{
 UsbDeviceEndpointState_t state;uint8_t *saved;uint16_t length;uint32_t calls;
 if(id==200){EQ(USB_Device_Init(),SYS_OK);EQ(s_registered,false);EQ(USB_Device_Start(),SYS_ERR_INVALID_STATE);EQ(start_count,0);return 0;}
 if(id==201){EQ(Framework_InitRuntime(),SYS_OK);EQ(s_registered,true);EQ(s_device.pDesc,&s_descriptor_driver);EQ(start_count,1);return 0;}
 if(id==202 || id==203 || id==204 || id==231){
  EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);saved=USB_Device_GetConfigDescriptor(&length);EQ(length,18);
  if(id==202){core_config[2]=24;EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_ERR_INVALID_ARG);}
  if(id==203){core_config[18]=0;EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_ERR_INVALID_ARG);}
  if(id==204){core_config[13]=2;EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_ERR_INVALID_ARG);}
  if(id==231){USBD_ClassTypeDef invalid=core_driver;invalid.Setup=NULL;EQ(USB_Device_RegisterClass(&invalid,&core_descriptors),SYS_ERR_INVALID_ARG);}
  EQ(USB_Device_GetConfigDescriptor(&length),saved);EQ(length,18);return 0;
 }
 if(id==232){EQ(USB_Device_GetConfigDescriptor(NULL),NULL);EQ(USB_Device_GetConfigDescriptor(&length),NULL);EQ(length,0);return 0;}
 if(id==239){EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);core_descriptors.GetDeviceDescriptor=Core_BadDevice;EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_ERR_INVALID_ARG);EQ(USB_Device_GetConfigDescriptor(&length),s_config_desc);return 0;}
 if(id==245){EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);core_lifecycle_action=2U;EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_OK);for(calls=0;calls<4;calls++)EQ(core_nested[calls],SYS_ERR_BUSY);EQ(start_count,0);return 0;}
 if(id==247){EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);fault=18;EQ(USB_Device_Start(),SYS_OK);EQ(reentry_from_start,SYS_ERR_BUSY);EQ(s_started,true);return 0;}
 EQ(Core_Boot(),0);
 if(id==242){fault=13;HAL_PCD_ResetCallback(&s_pcd);EQ(s_stop_required,true);EQ(USB_Device_IsConfigured(),false);EQ(USB_Device_Start(),SYS_ERR_INVALID_STATE);fault=0;EQ(USB_Device_Stop(),SYS_OK);return 0;}
 if(id==244){core_lifecycle_action=1U;EQ(USB_Device_Stop(),SYS_OK);for(calls=0;calls<4;calls++)EQ(core_nested[calls],SYS_ERR_BUSY);EQ(s_started,false);return 0;}
 if(id==246){Request(0x21,0x77,0,0,0);EQ(USB_Device_GetEpState(0x80,&state),SYS_OK);EQ(state.stalled,true);Request(0x80,USB_REQ_GET_DESCRIPTOR,0x100,0,18);EQ(USB_Device_GetEpState(0x80,&state),SYS_OK);EQ(state.stalled,false);return 0;}
 if(id==205){EQ(USB_Device_GetConfigDescriptor(&length),core_config);EQ(length,25);Request(0x80,USB_REQ_GET_DESCRIPTOR,0x200,0,255);EQ(tx_size,25);EQ(tx_data[2],25);return 0;}
 if(id==206){EQ(USB_Device_RegisterClass(&s_base_class,&s_descriptors),SYS_ERR_INVALID_STATE);EQ(USB_Device_GetConfigDescriptor(&length),core_config);return 0;}
 if(id==207){EQ(USB_Device_OpenEp(0,0,64),SYS_ERR_INVALID_ARG);EQ(USB_Device_CloseEp(0x80),SYS_ERR_INVALID_ARG);EQ(USB_Device_Transmit(0x80,core_packet,1),SYS_ERR_INVALID_ARG);EQ(USB_Device_Receive(0,core_packet,1),SYS_ERR_INVALID_ARG);EQ(USB_Device_GetEpState(0x80,&state),SYS_OK);EQ(state.opened,true);return 0;}
 if(id==210){EQ(USB_Device_OpenEp(0x81,USBD_EP_TYPE_BULK,64),SYS_OK);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,true);EQ(USB_Device_CloseEp(0x81),SYS_OK);EQ(USB_Device_OpenEp(0x82,USBD_EP_TYPE_INTR,16),SYS_OK);return 0;}
 if(id==211){EQ(USB_Device_Transmit(0x81,core_packet,512),SYS_ERR_INVALID_STATE);return 0;}
 if(id==217){EQ(USB_Device_OpenEp(0x91,1,512),SYS_ERR_INVALID_ARG);EQ(USB_Device_GetEpState(0x91,&state),SYS_ERR_INVALID_ARG);EQ(USB_Device_GetEpState(0x81,NULL),SYS_ERR_INVALID_ARG);return 0;}
 if(id==228){Request(0x60,0x55,0,0,0);EQ(stall_count>0,1);EQ(core_setup_count,0);return 0;}
 if(id==229){Request(0x21,0x77,0,0,0);EQ(core_setup_count,1);EQ(stall_count>0,1);return 0;}
 if(id==230){Request(0x21,0x55,0,0,0);EQ(core_setup_count,1);EQ(stall_count,0);EQ(tx_size,0);return 0;}
 if(id==233){USBD_HandleTypeDef foreign={0};EQ(USBD_LL_OpenEP(&foreign,0x81,1,512),USBD_FAIL);return 0;}
 if(id==238)HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
 EQ(USB_Device_OpenEp(0x81,USBD_EP_TYPE_ISOC,512),SYS_OK);
 if(id==243){core_lifecycle_action=5U;HAL_PCD_ResetCallback(&s_pcd);EQ(s_stop_required,true);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);return 0;}
 if(id==238){EQ(nvic,0);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(nvic,0);EQ(__get_PRIMASK(),0);return 0;}
 if(id==240){core_driver.GetFSConfigDescriptor=Usb_ConfigDesc;Request(0x80,USB_REQ_GET_DESCRIPTOR,0x200,0,255);EQ(tx_size,25);EQ(USB_Device_GetConfigDescriptor(&length),core_config);EQ(length,25);return 0;}
 if(id==241){PCD_HandleTypeDef foreign={0};HAL_PCD_ISOINIncompleteCallback(&foreign,1);EQ(core_iso_count,0);return 0;}
 if(id==236){fault=16;EQ(USB_Device_Transmit(0x81,core_packet,512),SYS_OK);EQ(core_in_count,1);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,false);EQ(nvic,1);return 0;}

 if(id==208){EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,true);EQ(state.busy,false);EQ(state.type,USBD_EP_TYPE_ISOC);EQ(state.max_packet,512);EQ(s_device.ep_in[1].is_used,1);EQ(nvic,1);return 0;}
 if(id==209){calls=open_count;EQ(USB_Device_OpenEp(0x81,1,512),SYS_ERR_ALREADY_EXISTS);EQ(open_count,calls);return 0;}
 if(id==218){EQ(USB_Device_Transmit(1,core_packet,512),SYS_ERR_INVALID_ARG);EQ(USB_Device_Receive(0x81,core_packet,512),SYS_ERR_INVALID_ARG);return 0;}
 if(id==219){EQ(USB_Device_Transmit(0x81,core_packet,512),SYS_OK);HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);EQ(core_iso_count,1);EQ(core_in_ep,1);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,false);EQ(state.last_result,SYS_ERR_TIMEOUT);return 0;}
 if(id==220){fault=9;EQ(USB_Device_Transmit(0x81,core_packet,512),SYS_ERR_BUSY);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,false);EQ(state.last_result,SYS_ERR_BUSY);return 0;}
 if(id==226){Request(0x82,USB_REQ_GET_STATUS,0,0x82,2);EQ(stall_count>0,1);return 0;}
 if(id==227){Request(2,USB_REQ_SET_FEATURE,USB_FEATURE_EP_HALT,0x81,0);EQ(stall_count>0,1);EQ(s_pcd.IN_ep[1].is_stall,0);return 0;}
 if(id==234){EQ(USB_Device_StallEp(0x81),SYS_ERR_NOT_SUPPORTED);EQ(USB_Device_ClearStallEp(0x81),SYS_ERR_NOT_SUPPORTED);return 0;}
 EQ(USB_Device_Transmit(0x81,core_packet,512),SYS_OK);
 if(id==212){EQ(USB_Device_Transmit(0x81,core_packet,512),SYS_ERR_BUSY);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,true);EQ(tx_size,512);EQ(USB_Device_IsConfigured(),true);return 0;}
 if(id==213 || id==235){core_rearm=(id==235);HAL_PCD_DataInStageCallback(&s_pcd,1);EQ(core_in_count,1);EQ(core_in_ep,1);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,id==235);return 0;}
 if(id==214){EQ(USB_Device_CloseEp(0x81),SYS_OK);HAL_PCD_DataInStageCallback(&s_pcd,1);EQ(core_in_count,0);return 0;}
 if(id==215){EQ(USB_Device_CloseEp(0x81),SYS_OK);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);EQ(state.busy,false);EQ(s_device.ep_in[0].is_used,1);return 0;}
 if(id==216){EQ(USB_Device_FlushEp(0x81),SYS_ERR_BUSY);EQ(flush_count,0);EQ(USB_Device_CloseEp(0x81),SYS_OK);EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(USB_Device_FlushEp(0x81),SYS_OK);EQ(flush_count,1);return 0;}
 if(id==237){fault=17;EQ(USB_Device_CloseEp(0x81),SYS_OK);EQ(core_in_count,0);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);return 0;}
 if(id==221){fault=14;EQ(USB_Device_CloseEp(0x81),SYS_ERR_IO);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,true);EQ(state.busy,true);return 0;}
 if(id==222){EQ(USB_Device_Stop(),SYS_OK);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);EQ(state.busy,false);EQ(USB_Device_IsConfigured(),false);EQ(USB_Device_Start(),SYS_OK);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);return 0;}
 if(id==223){fault=8;calls=core_deinit_count;EQ(USB_Device_Stop(),SYS_ERR_IO);EQ(core_deinit_count,calls);EQ(USB_Device_Start(),SYS_ERR_INVALID_STATE);EQ(USB_Device_RegisterClass(&core_driver,&core_descriptors),SYS_ERR_INVALID_STATE);fault=0;EQ(USB_Device_Stop(),SYS_OK);EQ(s_stop_required,false);return 0;}
 if(id==224){HAL_PCD_ResetCallback(&s_pcd);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);EQ(USB_Device_GetEpState(0x80,&state),SYS_OK);EQ(state.opened,true);EQ(USB_Device_IsConfigured(),false);return 0;}
 if(id==225){HAL_PCD_DisconnectCallback(&s_pcd);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);return 0;}
 EQ(0,1);return 1;
}

#include "endpoint_lifecycle_test.c"
