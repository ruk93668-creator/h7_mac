//独立测试夹具，不实现UVC、MSC或其他设备协议
static uint8_t stage2_config[160];
static uint16_t stage2_config_length;
static uint8_t stage2_previous_config[160];
static uint8_t *stage2_descriptor_view;
static uint8_t stage2_data[2048];
static void *stage2_memory;
static uint32_t stage2_init_calls, stage2_deinit_calls;
static uint32_t stage2_in_calls, stage2_out_calls, stage2_iso_in, stage2_iso_out;
static bool stage2_allocate, stage2_init_fail, stage2_deinit_fail, stage2_leak;
static bool stage2_init_reentry, stage2_deinit_reentry, stage2_deferred;
volatile uint32_t g_stage2_observation[12];

static uint8_t *Stage2_Config(uint16_t *length)
{
    *length = stage2_config_length;
    return stage2_descriptor_view != NULL ? stage2_descriptor_view : stage2_config;
}

//构造普通测试描述符，只声明要验收的端点资源
static void Stage2_ConfigBuild(const BspUsbEndpointConfig_t *entries, uint32_t count)
{
    uint32_t i;
    static const uint8_t header[18] = {9,2,18,0,1,1,0,0x80,250,9,4,0,0,0,0xFF,0,0,0};
    memcpy(stage2_config, header, sizeof(header));
    stage2_config_length = (uint16_t)(18U + 7U * count);
    stage2_config[2] = (uint8_t)stage2_config_length;
    stage2_config[3] = (uint8_t)(stage2_config_length >> 8);
    stage2_config[13] = (uint8_t)count;
    for (i = 0; i < count; ++i)
    {
        uint8_t *desc = stage2_config + 18U + 7U * i;
        desc[0] = 7; desc[1] = 5; desc[2] = entries[i].address;
        desc[3] = entries[i].type;
        desc[4] = (uint8_t)entries[i].max_packet;
        desc[5] = (uint8_t)(entries[i].max_packet >> 8);
        desc[6] = 1;
    }
}

static uint8_t Stage2_Init(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device; (void)config;
    device->pUserData[0] = stage2_data;
    ++stage2_init_calls;
    if (stage2_init_reentry) Core_NestedLifecycle();
    if (stage2_allocate)
    {
        stage2_memory = USB_Device_Alloc(128U);
        if (stage2_memory == NULL) return USBD_FAIL;
    }
    return stage2_init_fail ? USBD_FAIL : USBD_OK;
}

static uint8_t Stage2_DeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device; (void)config;
    ++stage2_deinit_calls;
    if (stage2_deinit_reentry) Core_NestedLifecycle();
    if (stage2_memory != NULL && !stage2_leak)
    {
        USB_Device_Free(stage2_memory);
        stage2_deferred = s_class_memory_used && USB_Device_Alloc(1U) == NULL;
    }
    //故意不关端点，验证Platform兜底清理，不在测试Class重复实现
    return stage2_deinit_fail ? USBD_FAIL : USBD_OK;
}

static uint8_t Stage2_In(USBD_HandleTypeDef *device, uint8_t ep)
{
    (void)device; (void)ep; ++stage2_in_calls; return USBD_OK;
}

static uint8_t Stage2_Out(USBD_HandleTypeDef *device, uint8_t ep)
{
    (void)device; (void)ep; ++stage2_out_calls; return USBD_OK;
}

static uint8_t Stage2_IsoIn(USBD_HandleTypeDef *device, uint8_t ep)
{
    (void)device; (void)ep; ++stage2_iso_in; return USBD_OK;
}

static uint8_t Stage2_IsoOut(USBD_HandleTypeDef *device, uint8_t ep)
{
    (void)device; (void)ep; ++stage2_iso_out; return USBD_OK;
}

static USBD_ClassTypeDef stage2_driver = {
    .Init=Stage2_Init, .DeInit=Stage2_DeInit, .Setup=Core_Setup,
    .DataIn=Stage2_In, .DataOut=Stage2_Out,
    .IsoINIncomplete=Stage2_IsoIn, .IsoOUTIncomplete=Stage2_IsoOut,
    .GetFSConfigDescriptor=Stage2_Config
};

static int Stage2_Boot(bool configure)
{
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_Device_RegisterClass(&stage2_driver, &core_descriptors), SYS_OK);
    EQ(USB_Mode_Init(), SYS_OK); EQ(USB_Mode_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    if (configure)
    {
        Configured(); USB_Mode_Process();
        EQ(USB_Device_IsConfigured(), true);
        EQ(s_usb_mode.state, USB_MODE_STATE_RUNNING);
    }
    return 0;
}

//核对真正HAL写入的起始Word和深度，不能只检查Mock参数
static int Stage2_FifoLayout(uint32_t expected_end)
{
    uint32_t index, value, offset = seen_rx_fifo;
    EQ(s_pcd.Instance->GRXFSIZ, 320U);
    for (index = 0; index < BSP_USB_ENDPOINT_COUNT; ++index)
    {
        value = index == 0 ? s_pcd.Instance->DIEPTXF0_HNPTXFSIZ : s_pcd.Instance->DIEPTXF[index-1];
        EQ(value & 0xFFFFU, offset);
        EQ(value >> 16, seen_tx_fifo[index]);
        offset += seen_tx_fifo[index];
    }
    EQ(offset, expected_end); EQ(offset <= 1024U, true);
    return 0;
}

static int Stage2_Case(uint32_t id)
{
    BspUsbEndpointConfig_t entries[16] = {{0x81U,EP_TYPE_ISOC,512U}};
    UsbDeviceEndpointState_t state, other;
    uint32_t i, count, saved, calls;
    uint8_t *old_config;
    uint16_t length;
    void *memory;

    //CASE 300 | IN等时未完成保持设备健康 | EP1 IN发送512字节后ISO incomplete | 清busy、端点TIMEOUT、Class收到事件、Mode保持RUNNING
    if (id == 300)
    {
        Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);
        EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);
        HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);
        EQ(USB_Device_GetEpState(0x81,&state),SYS_OK); EQ(state.busy,false);
        EQ(state.last_result,SYS_ERR_TIMEOUT); EQ(stage2_iso_in,1);
        EQ(USB_Device_GetLastResult(),SYS_OK); USB_Mode_Process();
        EQ(s_usb_mode.state,USB_MODE_STATE_RUNNING); return 0;
    }
    //CASE 301 | OUT等时未完成保持设备健康 | OUT2接收64字节后ISO incomplete | 只更新OUT2，转交IsoOUTIncomplete，无全局故障
    if (id == 301)
    {
        entries[0].address=2; entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(2,1,64),SYS_OK); EQ(USB_Device_Receive(2,stage2_data,64),SYS_OK);
        HAL_PCD_ISOOUTIncompleteCallback(&s_pcd,2);
        EQ(USB_Device_GetEpState(2,&state),SYS_OK); EQ(state.busy,false);
        EQ(state.last_result,SYS_ERR_TIMEOUT); EQ(stage2_iso_out,1); EQ(stage2_iso_in,0);
        EQ(USB_Device_GetLastResult(),SYS_OK); USB_Mode_Process();
        EQ(s_usb_mode.state,USB_MODE_STATE_RUNNING); return 0;
    }
    //CASE 302 | 非等时端点忽略ISO通知 | Bulk端点、关闭端点和异PCD收到伪通知 | 不改变busy、不调用ISO回调
    if (id == 302)
    {
        entries[0].type=EP_TYPE_BULK; entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,2,64),SYS_OK); EQ(USB_Device_Transmit(0x81,stage2_data,64),SYS_OK);
        HAL_PCD_ISOINIncompleteCallback(&s_pcd,1); HAL_PCD_ISOINIncompleteCallback(&s_pcd,2);
        EQ(stage2_iso_in,0); EQ(USB_Device_GetEpState(0x81,&state),SYS_OK); EQ(state.busy,true); return 0;
    }
    //CASE 303 | 初次Reset不释放未初始化Class | 尚未SET_CONFIGURATION时连续Reset | DeInit零次，EP0双向实际打开
    if (id == 303)
    {
        Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(false),0); HAL_PCD_ResetCallback(&s_pcd);
        EQ(stage2_deinit_calls,0); EQ(stage2_init_calls,0);
        EQ(USB_Device_GetEpState(0,&state),SYS_OK); EQ(state.opened,true);
        EQ(USB_Device_GetEpState(0x80,&state),SYS_OK); EQ(state.opened,true); return 0;
    }
    //CASE 304 | Reset后Stop不重复释放 | 成功Init并分配内存，Reset两次再Stop | DeInit一次，数据端点关闭，池空闲
    //CASE 305 | Disconnect后Stop不重复释放 | 活动Class断线再Stop | DeInit一次，池空闲，数据端点清理
    if (id == 304 || id == 305)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);
        if (id==304) {HAL_PCD_ResetCallback(&s_pcd);HAL_PCD_ResetCallback(&s_pcd);}
        else HAL_PCD_DisconnectCallback(&s_pcd);
        EQ(USB_Device_GetEpState(0x81,&state),SYS_OK); EQ(state.opened,false);
        EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,false);
        EQ(USB_Device_Stop(),SYS_OK); EQ(stage2_deinit_calls,1); return 0;
    }
    //CASE 306 | 连续Stop幂等并可再启动 | Stop两次，Start后重新枚举再Stop | 两次Init对应两次DeInit，无残留占用
    if (id == 306)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_Stop(),SYS_OK); EQ(USB_Device_Stop(),SYS_OK); EQ(stage2_deinit_calls,1);
        EQ(USB_Device_Start(),SYS_OK); HAL_PCD_ResetCallback(&s_pcd); Configured();
        EQ(stage2_init_calls,2); EQ(USB_Device_Stop(),SYS_OK); EQ(stage2_deinit_calls,2);
        EQ(s_class_memory_used,false); return 0;
    }
    //CASE 307 | Disconnect释放失败保留池 | DeInit先Free再FAIL，断线后Stop重试 | 故障锁存、内存仍占用，成功重试后回收
    //CASE 308 | Reset释放失败仍重建EP0 | DeInit返回FAIL且存在数据端点 | 数据端点关闭，EP0双向重建，池保留
    //CASE 309 | Stop释放失败保留所有权 | DeInit先Free再FAIL，尝试外部Free和重启 | 不回收、不允许重启或注册，Stop成功后释放
    if (id >= 307 && id <= 309)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK); stage2_deinit_fail=true;
        if(id==307)HAL_PCD_DisconnectCallback(&s_pcd);
        else if(id==308)HAL_PCD_ResetCallback(&s_pcd);
        else EQ(USB_Device_Stop(),SYS_ERR_IO);
        EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,true); EQ(s_stop_required,true);
        EQ(s_class_state,USB_CLASS_RELEASE_FAILED); USB_Device_Free(stage2_memory);
        EQ(s_class_memory_used,true); EQ(USB_Device_Alloc(1),NULL);
        EQ(USB_Device_Start(),SYS_ERR_INVALID_STATE);
        EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_INVALID_STATE);
        EQ(USB_Device_GetEpState(0x81,&state),SYS_OK); EQ(state.opened,false);
        if(id==308){EQ(USB_Device_GetEpState(0,&state),SYS_OK);EQ(state.opened,true);EQ(USB_Device_GetEpState(0x80,&state),SYS_OK);EQ(state.opened,true);}
        stage2_deinit_fail=false; EQ(USB_Device_Stop(),SYS_OK);
        EQ(stage2_deinit_calls,2); EQ(s_class_memory_used,false); return 0;
    }
    //CASE 310 | 硬件Stop失败不提前释放 | 活动发送期间HAL Stop返回ERROR | 不DeInit，busy和池保留，成功重试才释放
    //CASE 311 | 官方Stop忽略返回值仍不误释放 | 直接USBD_Stop且HAL Stop返回ERROR | 官方返回OK但Platform锁存故障，保留资源
    //CASE 313 | 官方DeInit遇到硬件停止失败 | 直接USBD_DeInit且HAL Stop返回ERROR | 返回FAIL，未调用外部DeInit，池与注册保留
    if (id==310 || id==311 || id==313)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK); EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);
        fault=8;
        if(id==310)EQ(USB_Device_Stop(),SYS_ERR_IO);
        else if(id==311)EQ(USBD_Stop(&s_device),USBD_OK);
        else EQ(USBD_DeInit(&s_device),USBD_FAIL);
        EQ(stage2_deinit_calls,0); EQ(s_class_memory_used,true); EQ(s_stop_required,true);
        EQ(USB_Device_GetEpState(0x81,&state),SYS_OK); EQ(state.opened,true); EQ(state.busy,true);
        fault=0; EQ(USB_Device_Stop(),SYS_OK); EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,false); return 0;
    }
    //CASE 312 | 官方DeInit遇到Class释放失败 | DeInit先Free再FAIL，USBD_DeInit清注册指针 | 适配层恢复注册指针并保留池，可Stop重试
    if (id == 312)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        stage2_deinit_fail=true; EQ(USBD_DeInit(&s_device),USBD_FAIL);
        EQ(s_class_memory_used,true); EQ(s_initialized,true); EQ(s_device.pClass[0],&s_class_driver);
        EQ(s_device.pDesc,&s_descriptor_driver); EQ(s_device.pConfDesc,stage2_config);
        stage2_deinit_fail=false; EQ(USB_Device_Stop(),SYS_OK); EQ(s_class_memory_used,false); return 0;
    }
    //CASE 314 | 官方DeInit成功后可重新初始化 | 活动Class释放后DeInit、Init、注册、Start | 无旧池残留，第二次Init成功
    if (id == 314)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USBD_DeInit(&s_device),USBD_OK); EQ(s_class_memory_used,false); EQ(s_initialized,false);
        //Core独立DeInit后独立重启，不改动仍保留RUNNING的Mode Manager
        EQ(USB_Device_Init(),SYS_OK);EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_OK);
        EQ(USB_Device_Start(),SYS_OK);HAL_PCD_ResetCallback(&s_pcd);Configured();
        EQ(USB_Device_IsConfigured(),true);EQ(stage2_init_calls,2);EQ(stage2_deinit_calls,1);return 0;
    }
    //CASE 315 | 固定池地址与重复释放边界 | 长度0和1025、内指针、尾指针、NULL、重复Free | 只接受1到1024，只有精确基址能释放
    if (id == 315)
    {
        EQ(USB_Device_Alloc(0),NULL); EQ(USB_Device_Alloc(1025),NULL);
        memory=USB_Device_Alloc(1024); EQ(memory!=NULL,true); EQ(USB_Device_Alloc(1),NULL);
        USB_Device_Free(NULL); USB_Device_Free((uint8_t*)memory+1); USB_Device_Free((uint8_t*)memory+1024);
        EQ(USB_Device_Alloc(1),NULL); USB_Device_Free(memory); USB_Device_Free(memory);
        EQ(USB_Device_Alloc(1),memory); return 0;
    }
    //CASE 316 | DeInit中的Free延后提交 | DeInit回调Free后立即再次Alloc | 回调内部仍占用，返回成功后才可再分配
    if (id == 316)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_Stop(),SYS_OK); EQ(stage2_deferred,true); EQ(s_class_memory_used,false);
        EQ(USB_Device_Alloc(1024),stage2_memory); return 0;
    }
    //CASE 317 | 成功DeInit漏Free不静默回收 | DeInit返回OK但未Free，Stop重试后显式补Free | 报IO且保留占用，不重复DeInit，补Free后恢复
    //CASE 353 | Reset时发现内存泄漏 | DeInit成功漏Free，Reset后Stop | 保留池、EP0重建、不重复释放，补Free后可恢复
    if (id==317 || id==353)
    {
        stage2_allocate=true; stage2_leak=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        if(id==317)EQ(USB_Device_Stop(),SYS_ERR_IO);else HAL_PCD_ResetCallback(&s_pcd);
        EQ(s_class_memory_used,true); EQ(stage2_deinit_calls,1); EQ(USB_Device_Stop(),SYS_ERR_IO);
        EQ(stage2_deinit_calls,1); EQ(USB_Device_Alloc(1),NULL);
        USB_Device_Free(stage2_memory); EQ(USB_Device_Stop(),SYS_OK);
        EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,false); return 0;
    }
    //CASE 318 | Init失败保留部分资源 | Init分配内存后FAIL，Stop清理后再启动 | 初次不进入ACTIVE，清理后第二次Init可成功
    if (id == 318)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(false),0);
        stage2_init_fail=true; Configured(); EQ(s_class_state,USB_CLASS_RELEASE_FAILED);
        EQ(s_class_memory_used,true); EQ(USB_Device_IsConfigured(),false); EQ(USB_Device_Stop(),SYS_OK);
        EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,false); stage2_init_fail=false;
        EQ(USB_Device_Start(),SYS_OK); HAL_PCD_ResetCallback(&s_pcd); Configured();
        EQ(stage2_init_calls,2); EQ(USB_Device_IsConfigured(),true); return 0;
    }
    //CASE 319 | Init回调拒绝生命周期重入 | Init内部调用Init、Start、Stop和Register | 四项均BUSY，原Init正常完成
    //CASE 320 | Reset释放回调拒绝重入 | Reset触发DeInit并调用四项生命周期接口 | 四项均BUSY，无递归DeInit
    if (id==319 || id==320)
    {
        stage2_init_reentry=id==319; stage2_deinit_reentry=id==320;
        Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        if(id==320)HAL_PCD_ResetCallback(&s_pcd);
        for(i=0;i<4;i++)EQ(core_nested[i],SYS_ERR_BUSY);
        EQ(stage2_init_calls,1); EQ(stage2_deinit_calls,id==320); return 0;
    }
    //CASE 321 | 端点关闭失败不重复成功DeInit | DeInit成功且Free，兜底Close返回ERROR，再Stop | 池延后回收，重试只关端点，DeInit仍一次
    if (id == 321)
    {
        stage2_allocate=true; Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK); fault=14;
        EQ(USB_Device_Stop(),SYS_ERR_IO); EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,true);
        EQ(s_class_state,USB_CLASS_RELEASING); fault=0; EQ(USB_Device_Stop(),SYS_OK);
        EQ(stage2_deinit_calls,1); EQ(s_class_memory_used,false); return 0;
    }
    //CASE 322 | 同编号IN和OUT独立完成 | IN3及OUT3同时发送接收，完成IN后关IN | OUT保持busy、buffer和opened，不受IN事件影响
    //CASE 332 | 同编号IN的Flush不影响OUT | IN3空闲而OUT3接收中，Flush和Close IN3 | OUT事务、缓冲区及busy保持
    if (id==322 || id==332)
    {
        entries[0].address=0x83; entries[0].type=2; entries[0].max_packet=64;
        entries[1]=entries[0]; entries[1].address=3;
        Stage2_ConfigBuild(entries,2); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x83,2,64),SYS_OK); EQ(USB_Device_OpenEp(3,2,64),SYS_OK);
        EQ(USB_Device_Receive(3,stage2_data,128),SYS_OK);
        if(id==322){EQ(USB_Device_Transmit(0x83,stage2_data,128),SYS_OK);HAL_PCD_DataInStageCallback(&s_pcd,3);EQ(stage2_in_calls,1);}
        else EQ(USB_Device_FlushEp(0x83),SYS_OK);
        EQ(USB_Device_CloseEp(0x83),SYS_OK); EQ(USB_Device_GetEpState(3,&state),SYS_OK);
        EQ(state.busy,true); EQ(state.opened,true); EQ(s_pcd.OUT_ep[3].xfer_buff,stage2_data);
        s_pcd.OUT_ep[3].xfer_count=31; HAL_PCD_DataOutStageCallback(&s_pcd,3);
        EQ(stage2_out_calls,1); EQ(USB_Device_GetRxSize(3),31); return 0;
    }
    //CASE 323 | EP8支持且EP9拒绝 | 同时打开IN8和OUT8，再申请IN9及OUT9 | EP8正常，EP9 INVALID_ARG
    if (id == 323)
    {
        entries[0].address=0x88; entries[0].type=2; entries[0].max_packet=64;
        entries[1]=entries[0]; entries[1].address=8; Stage2_ConfigBuild(entries,2); EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x88,2,64),SYS_OK); EQ(USB_Device_OpenEp(8,2,64),SYS_OK);
        EQ(USB_Device_OpenEp(0x89,2,64),SYS_ERR_INVALID_ARG); EQ(USB_Device_OpenEp(9,2,64),SYS_ERR_INVALID_ARG); return 0;
    }
    //CASE 324 | Bulk全部合法FS包长 | 双向Bulk依次配置8、16、32、64 | 四种包长均可打开和关闭
    if (id == 324)
    {
        entries[0].address=0x82; entries[0].type=2; entries[0].max_packet=64;
        entries[1]=entries[0]; entries[1].address=2; Stage2_ConfigBuild(entries,2); EQ(Stage2_Boot(true),0);
        for(i=8;i<=64;i*=2){EQ(USB_Device_OpenEp(0x82,2,(uint16_t)i),SYS_OK);EQ(USB_Device_OpenEp(2,2,(uint16_t)i),SYS_OK);EQ(USB_Device_CloseEp(0x82),SYS_OK);EQ(USB_Device_CloseEp(2),SYS_OK);} return 0;
    }
    //CASE 325 | Interrupt及ISO包长边界 | IN/OUT分别测试INTR 1和64，ISO 1和1023 | 合法边界均支持且方向上下文正确
    if (id == 325)
    {
        entries[0].address=0x82; entries[0].type=1; entries[0].max_packet=1023;
        entries[1]=entries[0]; entries[1].address=2; Stage2_ConfigBuild(entries,2); EQ(Stage2_Boot(true),0);
        for(i=1;i<=3;i+=2){EQ(USB_Device_OpenEp(0x82,(uint8_t)i,1),SYS_OK);EQ(USB_Device_OpenEp(2,(uint8_t)i,1),SYS_OK);EQ(USB_Device_CloseEp(0x82),SYS_OK);EQ(USB_Device_CloseEp(2),SYS_OK);count=i==1?1023:64;EQ(USB_Device_OpenEp(0x82,(uint8_t)i,(uint16_t)count),SYS_OK);EQ(USB_Device_OpenEp(2,(uint8_t)i,(uint16_t)count),SYS_OK);EQ(USB_Device_CloseEp(0x82),SYS_OK);EQ(USB_Device_CloseEp(2),SYS_OK);} return 0;
    }
    //CASE 326 | 非法传输类型及包长拒绝 | 非零Control、Bulk15或65、INTR65、ISO1024 | NOT_SUPPORTED，不调用HAL打开非法配置
    if (id == 326)
    {
        Stage2_ConfigBuild(entries,1); EQ(Stage2_Boot(true),0); calls=open_count;
        EQ(USB_Device_OpenEp(0x81,0,64),SYS_ERR_NOT_SUPPORTED);
        EQ(USB_Device_OpenEp(0x81,2,15),SYS_ERR_NOT_SUPPORTED); EQ(USB_Device_OpenEp(1,2,65),SYS_ERR_NOT_SUPPORTED);
        EQ(USB_Device_OpenEp(0x81,3,65),SYS_ERR_NOT_SUPPORTED); EQ(USB_Device_OpenEp(1,1,1024),SYS_ERR_NOT_SUPPORTED);
        EQ(open_count,calls); EQ(BSP_USB_OpenEp(0x80,2,64),SYS_ERR_NOT_SUPPORTED); return 0;
    }
    //CASE 327 | Bulk IN多包不受512字节限制 | EP8 Bulk64提交1024字节并完成 | HAL收到1024，busy完成后清除
    if (id == 327)
    {
        entries[0].address=0x88;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x88,2,64),SYS_OK);
        EQ(USB_Device_Transmit(0x88,stage2_data,1024),SYS_OK);EQ(tx_size,64);EQ(last_ep,0x88);
        for(i=0;i<16;i++)
        {
            HAL_PCD_DataInStageCallback(&s_pcd,8);
            EQ(USB_Device_GetEpState(0x88,&state),SYS_OK);
            EQ(state.busy,i!=15);
        }
        EQ(stage2_in_calls,1);return 0;
    }
    //CASE 328 | Bulk OUT实际长度和编号 | OUT4准备64字节缓冲，模拟收到31字节 | GetRxSize返回31，不读取OUT0上下文
    if (id == 328)
    {
        entries[0].address=4;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(4,2,64),SYS_OK);
        EQ(USB_Device_Receive(4,stage2_data,64),SYS_OK);s_pcd.OUT_ep[4].xfer_count=31;s_pcd.OUT_ep[0].xfer_count=9;
        HAL_PCD_DataOutStageCallback(&s_pcd,4);EQ(USB_Device_GetRxSize(4),31);EQ(stage2_out_calls,1);
        EQ(USB_Device_GetEpState(4,&state),SYS_OK);EQ(state.busy,false);return 0;
    }
    //CASE 329 | OUT缓冲容量防HAL取整溢出 | Bulk64的容量31、0及NULL64，随后合法64 | 非整包容量拒绝且不残留busy，合法接收可继续
    if (id == 329)
    {
        entries[0].address=4;entries[0].type=2;entries[0].max_packet=64;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(4,2,64),SYS_OK);EQ(USB_Device_Receive(4,stage2_data,31),SYS_ERR_INVALID_ARG);
        EQ(USB_Device_Receive(4,stage2_data,0),SYS_ERR_INVALID_ARG);EQ(USB_Device_Receive(4,NULL,64),SYS_ERR_INVALID_ARG);
        EQ(USB_Device_GetEpState(4,&state),SYS_OK);EQ(state.busy,false);EQ(USB_Device_Receive(4,stage2_data,64),SYS_OK);return 0;
    }
    //CASE 330 | ISO单次整包限制 | ISO512 IN提交513，OUT提交1024，再合法512 | 非法长度拒绝，合法单包正常
    if (id == 330)
    {
        entries[1]=entries[0];entries[1].address=1;Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(USB_Device_OpenEp(1,1,512),SYS_OK);
        EQ(USB_Device_Transmit(0x81,stage2_data,513),SYS_ERR_INVALID_ARG);EQ(USB_Device_Receive(1,stage2_data,1024),SYS_ERR_INVALID_ARG);
        EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);EQ(USB_Device_Receive(1,stage2_data,512),SYS_OK);return 0;
    }
    //CASE 331 | STALL按类型而非EP1判断 | EP1 Bulk及EP7 ISO分别Stall和ClearStall | Bulk可HALT且可清除，ISO拒绝
    if (id == 331)
    {
        entries[0].type=2;entries[0].max_packet=64;entries[1].address=0x87;entries[1].type=1;entries[1].max_packet=64;
        Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,2,64),SYS_OK);EQ(USB_Device_OpenEp(0x87,1,64),SYS_OK);
        EQ(USB_Device_StallEp(0x81),SYS_OK);EQ(BSP_USB_IsStallEp(0x81),true);EQ(BSP_USB_IsStallEp(0x87),false);
        EQ(USB_Device_ClearStallEp(0x81),SYS_OK);EQ(BSP_USB_IsStallEp(0x81),false);
        EQ(USB_Device_StallEp(0x87),SYS_ERR_NOT_SUPPORTED);EQ(USB_Device_ClearStallEp(0x87),SYS_ERR_NOT_SUPPORTED);return 0;
    }
    //CASE 333 | 共享RX Flush保护其他OUT | OUT4在接收时Flush空闲OUT3，完成OUT4再Flush | 先BUSY且不调用HAL，后成功
    if (id == 333)
    {
        entries[0].address=3;entries[0].type=2;entries[0].max_packet=64;entries[1]=entries[0];entries[1].address=4;
        Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(3,2,64),SYS_OK);EQ(USB_Device_OpenEp(4,2,64),SYS_OK);
        EQ(USB_Device_Receive(4,stage2_data,64),SYS_OK);EQ(USB_Device_FlushEp(3),SYS_ERR_BUSY);EQ(flush_count,0);
        HAL_PCD_DataOutStageCallback(&s_pcd,4);EQ(USB_Device_FlushEp(3),SYS_OK);EQ(flush_count,1);return 0;
    }
    //CASE 334 | 关闭一个方向失败保留资源 | IN3及OUT3同时活动，关闭IN3失败 | IN和OUT仍opened及busy，未误释放任一缓冲
    if (id == 334)
    {
        entries[0].address=0x83;entries[0].type=2;entries[0].max_packet=64;entries[1]=entries[0];entries[1].address=3;
        Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x83,2,64),SYS_OK);EQ(USB_Device_OpenEp(3,2,64),SYS_OK);
        EQ(USB_Device_Transmit(0x83,stage2_data,64),SYS_OK);EQ(USB_Device_Receive(3,stage2_data,64),SYS_OK);fault=14;
        EQ(USB_Device_CloseEp(0x83),SYS_ERR_IO);EQ(USB_Device_GetEpState(0x83,&state),SYS_OK);EQ(USB_Device_GetEpState(3,&other),SYS_OK);
        EQ(state.opened,true);EQ(state.busy,false);EQ(other.opened,true);EQ(other.busy,true);return 0;
    }
    //CASE 335 | HAL发送BUSY后能继续使用 | Bulk发送启动BUSY，清故障再次发送 | 首次不残留busy，第二次正常启动
    if (id == 335)
    {
        entries[0].type=2;entries[0].max_packet=64;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,2,64),SYS_OK);
        fault=9;EQ(USB_Device_Transmit(0x81,stage2_data,128),SYS_ERR_BUSY);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,false);
        fault=0;EQ(USB_Device_Transmit(0x81,stage2_data,128),SYS_OK);return 0;
    }
    //CASE 336 | 运行期间禁止重排FIFO | 启动后调用BSP ConfigureEndpoints | BUSY，原FIFO寄存器保持不变
    if (id == 336)
    {
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);saved=s_pcd.Instance->DIEPTXF[0];
        EQ(BSP_USB_ConfigureEndpoints(NULL,0),SYS_ERR_BUSY);EQ(s_pcd.Instance->DIEPTXF[0],saved);return 0;
    }
    //CASE 337 | 默认FIFO真实寄存器布局 | 只有EP0的TEST配置 | RX320、TX0为64、TX1到8各16，共512Word
    if (id == 337)
    {
        EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);EQ(Stage2_FifoLayout(512),0);return 0;
    }
    //CASE 338 | EP1 ISO512兼容原能力 | 注册IN1 ISO512并申请其他大包IN | TX1为128Word，共624Word，未规划的大包被拒绝
    if (id == 338)
    {
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(seen_tx_fifo[1],128);EQ(Stage2_FifoLayout(624),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(USB_Device_OpenEp(0x82,1,1023),SYS_ERR_NO_RESOURCE);return 0;
    }
    //CASE 339 | EP7最大ISO包及OUT最大包 | IN7 ISO1023与OUT8 ISO1023 | TX7为256Word，共752Word，双向可提交
    if (id == 339)
    {
        entries[0].address=0x87;entries[0].max_packet=1023;entries[1]=entries[0];entries[1].address=8;
        Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);EQ(Stage2_FifoLayout(752),0);
        EQ(USB_Device_OpenEp(0x87,1,1023),SYS_OK);EQ(USB_Device_OpenEp(8,1,1023),SYS_OK);
        EQ(USB_Device_Transmit(0x87,stage2_data,1023),SYS_OK);EQ(USB_Device_Receive(8,stage2_data,1023),SYS_OK);return 0;
    }
    //CASE 340 | 512字节FIFO组合容量边界 | 四个ISO512 IN，再注册五个同规格IN | 四个占960Word成功，五个NO_RESOURCE且不改旧布局
    //CASE 341 | 1023字节FIFO组合容量边界 | 两个ISO1023 IN，再注册三个同规格IN | 两个占992Word成功，三个拒绝且保留旧注册
    if (id==340 || id==341)
    {
        count=id==340?4:2;
        for(i=0;i<count+1;i++){entries[i].address=(uint8_t)(0x81U+i);entries[i].type=1;entries[i].max_packet=id==340?512:1023;}
        Stage2_ConfigBuild(entries,count);memcpy(stage2_previous_config,stage2_config,stage2_config_length);stage2_descriptor_view=stage2_previous_config;
        EQ(USB_Device_Init(),SYS_OK);EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_OK);
        EQ(Stage2_FifoLayout(id==340?960:992),0);saved=s_pcd.Instance->DIEPTXF[0];
        Stage2_ConfigBuild(entries,count+1);stage2_descriptor_view=NULL;EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_NO_RESOURCE);
        EQ(s_pcd.Instance->DIEPTXF[0],saved);EQ(s_configuration_length,18+7*count);EQ(s_configuration,stage2_previous_config);return 0;
    }
    //CASE 342 | 备用设置取最大FIFO需求 | 同接口Alt0无端点、Alt1 ISO129、Alt2 ISO512 | TX1按512规划，Close/Open不改FIFO寄存器
    if (id == 342)
    {
        Stage2_ConfigBuild(NULL,0);
        for(i=0;i<2;i++)
        {
            uint8_t *desc=stage2_config+18+16*i;
            memcpy(desc,stage2_config+9,9);desc[3]=(uint8_t)(i+1);desc[4]=1;
            desc+=9;desc[0]=7;desc[1]=5;desc[2]=0x81;desc[3]=1;desc[4]=i?0:129;desc[5]=i?2:0;desc[6]=1;
        }
        stage2_config_length=50;stage2_config[2]=50;EQ(Stage2_Boot(true),0);EQ(seen_tx_fifo[1],128);
        saved=s_pcd.Instance->DIEPTXF[0];EQ(USB_Device_OpenEp(0x81,1,129),SYS_OK);EQ(USB_Device_CloseEp(0x81),SYS_OK);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(s_pcd.Instance->DIEPTXF[0],saved);return 0;
    }
    //CASE 343 | OUT不占用额外TX FIFO | 仅OUT8 Bulk64 | 总FIFO仍512Word，OUT8正常接收
    if (id == 343)
    {
        entries[0].address=8;entries[0].type=2;entries[0].max_packet=64;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);
        EQ(Stage2_FifoLayout(512),0);EQ(USB_Device_OpenEp(8,2,64),SYS_OK);EQ(USB_Device_Receive(8,stage2_data,128),SYS_OK);return 0;
    }
    //CASE 344 | 描述符中的非法端点拒绝 | EP9、保留地址位、Bulk65及高带宽位 | 拒绝注册，原TEST配置指针及长度保留
    //CASE 348 | 描述符中的重复地址拒绝 | 同一备用设置重复声明同一个IN地址 | INVALID_ARG且保持旧注册对象
    if (id==344 || id==348)
    {
        EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);old_config=USB_Device_GetConfigDescriptor(&length);
        if(id==348)
        {
            entries[1]=entries[0];Stage2_ConfigBuild(entries,2);
            EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_INVALID_ARG);
        }
        else
        {
            for(i=0;i<4;i++)
            {
                entries[0].address=i==0?0x89:i==1?0x91:0x81;entries[0].type=i==2?2:1;entries[0].max_packet=i==2?65:i==3?0xA00:512;
                Stage2_ConfigBuild(entries,1);EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors)!=SYS_OK,true);
            }
        }
        EQ(USB_Device_GetConfigDescriptor(&length),old_config);EQ(length,18);return 0;
    }
    //CASE 345 | FIFO硬件配置失败禁止启动 | 注册时SetTxFifo失败，随后恢复并Stop重试 | 标记布局无效及故障，恢复旧TEST布局后可启动
    if (id == 345)
    {
        EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);Stage2_ConfigBuild(entries,1);fault=6;
        EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_IO);EQ(s_fifo_valid,false);
        EQ(USB_Device_Start(),SYS_ERR_INVALID_STATE);fault=0;EQ(USB_Device_Stop(),SYS_OK);EQ(s_fifo_valid,true);
        EQ(USB_Device_GetConfigDescriptor(&length),s_config_desc);EQ(USB_Device_Start(),SYS_OK);return 0;
    }
    //CASE 346 | 官方Stop兜底清理通用端点 | Class DeInit不主动关闭端点，直接USBD_Stop | 适配层关闭数据端点、池释放、后续Stop不重复DeInit
    if (id == 346)
    {
        stage2_allocate=true;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);
        EQ(USBD_Stop(&s_device),USBD_OK);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);
        EQ(s_class_memory_used,false);EQ(USB_Device_Stop(),SYS_OK);EQ(stage2_deinit_calls,1);return 0;
    }
    //CASE 347 | 多轮启停无泄漏 | 16轮Reset、配置、分配、打开、发送、Stop | 16次Init及DeInit，池及全部数据端点均空闲
    if (id == 347)
    {
        stage2_allocate=true;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);
        for(i=0;i<16;i++)
        {
            if(i){EQ(USB_Device_Start(),SYS_OK);HAL_PCD_ResetCallback(&s_pcd);Configured();}
            EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);
            EQ(USB_Device_Stop(),SYS_OK);EQ(s_class_memory_used,false);EQ(s_class_state,USB_CLASS_INACTIVE);
            EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,false);
        }
        EQ(stage2_init_calls,16);EQ(stage2_deinit_calls,16);return 0;
    }
    //CASE 349 | 全部8组双向Bulk端点 | 同时申请IN1到8及OUT1到8，逐个完成 | 16个数据端点独立工作，共512Word，无索引越界
    if (id == 349)
    {
        for(i=0;i<16;i++){entries[i].address=(uint8_t)((i%8)+1+(i<8?0x80:0));entries[i].type=2;entries[i].max_packet=64;}
        Stage2_ConfigBuild(entries,16);EQ(Stage2_Boot(true),0);EQ(Stage2_FifoLayout(512),0);
        for(i=1;i<=8;i++)
        {
            EQ(USB_Device_OpenEp((uint8_t)(0x80+i),2,64),SYS_OK);EQ(USB_Device_OpenEp((uint8_t)i,2,64),SYS_OK);
            EQ(USB_Device_Transmit((uint8_t)(0x80+i),stage2_data,64),SYS_OK);EQ(USB_Device_Receive((uint8_t)i,stage2_data,64),SYS_OK);
            HAL_PCD_DataInStageCallback(&s_pcd,(uint8_t)i);HAL_PCD_DataOutStageCallback(&s_pcd,(uint8_t)i);
            EQ(USB_Device_GetEpState((uint8_t)(0x80+i),&state),SYS_OK);EQ(state.busy,false);
            EQ(USB_Device_GetEpState((uint8_t)i,&state),SYS_OK);EQ(state.busy,false);
        }
        EQ(stage2_in_calls,8);EQ(stage2_out_calls,8);return 0;
    }
    //CASE 350 | ISO通知不清除已有真实错误 | HAL发送返回BUSY后收到ISO incomplete | 原全局BUSY保留，端点独立记录TIMEOUT
    if (id == 350)
    {
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);fault=9;
        EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_ERR_BUSY);HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);
        EQ(USB_Device_GetLastResult(),SYS_OK);EQ(USB_Device_GetLastOperationResult(),SYS_ERR_BUSY);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.last_result,SYS_ERR_TIMEOUT);return 0;
    }
    //CASE 351 | 可选ISO回调为空 | 不提供IsoINIncomplete，触发等时未完成 | 仍清busy，Core安全处理，不产生全局故障
    if (id == 351)
    {
        stage2_driver.IsoINIncomplete=NULL;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);
        EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);
        HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);EQ(USB_Device_GetLastResult(),SYS_OK);EQ(stage2_iso_in,0);return 0;
    }
    //CASE 352 | 注册拒绝旧池占用 | 注册TEST后直接分配池，再注册新测试Class | BUSY，旧注册和池所有权不变
    if (id == 352)
    {
        EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);memory=USB_Device_Alloc(1);EQ(memory!=NULL,true);
        Stage2_ConfigBuild(entries,1);EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_BUSY);
        EQ(USB_Device_GetConfigDescriptor(&length),s_config_desc);EQ(s_class_memory_used,true);return 0;
    }
    //CASE 354 | 取消配置和再次配置生命周期 | SET_CONFIGURATION0后再1，最后Stop | 第一次只释放一次，第二次Init重新取得池，最终释放两次
    if (id == 354)
    {
        stage2_allocate=true;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);
        Request(0,USB_REQ_SET_CONFIGURATION,0,0,0);EQ(stage2_deinit_calls,1);EQ(s_class_memory_used,false);
        Request(0,USB_REQ_SET_CONFIGURATION,1,0,0);EQ(stage2_init_calls,2);EQ(s_class_memory_used,true);
        EQ(USB_Device_Stop(),SYS_OK);EQ(stage2_deinit_calls,2);return 0;
    }
    //CASE 355 | 多个OUT接收长度互不影响 | OUT2收到31字节、OUT3收到7字节 | 各GetRxSize返回自身长度，独立清busy
    if (id == 355)
    {
        entries[0].address=2;entries[0].type=2;entries[0].max_packet=64;entries[1]=entries[0];entries[1].address=3;
        Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(2,2,64),SYS_OK);EQ(USB_Device_OpenEp(3,2,64),SYS_OK);
        EQ(USB_Device_Receive(2,stage2_data,64),SYS_OK);EQ(USB_Device_Receive(3,stage2_data+64,64),SYS_OK);
        s_pcd.OUT_ep[2].xfer_count=31;s_pcd.OUT_ep[3].xfer_count=7;
        HAL_PCD_DataOutStageCallback(&s_pcd,2);EQ(USB_Device_GetEpState(3,&state),SYS_OK);EQ(state.busy,true);
        HAL_PCD_DataOutStageCallback(&s_pcd,3);EQ(USB_Device_GetRxSize(2),31);EQ(USB_Device_GetRxSize(3),7);
        EQ(stage2_out_calls,2);return 0;
    }
    //CASE 356 | 官方DeInit失败保留Class外部绑定 | Class持有pUserData且DeInit返回FAIL | LL适配恢复原pUserData，Stop重试能读取旧绑定
    if (id == 356)
    {
        stage2_allocate=true;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);
        EQ(s_device.pUserData[0],stage2_data);stage2_deinit_fail=true;EQ(USBD_DeInit(&s_device),USBD_FAIL);
        EQ(s_device.pUserData[0],stage2_data);EQ(s_class_memory_used,true);
        stage2_deinit_fail=false;EQ(USB_Device_Stop(),SYS_OK);return 0;
    }
    //CASE 357 | Core分发失败仍记录真实错误 | 模拟Core的Class指针缺失，收到ISO incomplete | Core返回FAIL，端点及全局为IO，保持现有Mode运行分支行为
    if (id == 357)
    {
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);
        EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);s_device.pClass[0]=NULL;HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);
        EQ(USB_Device_GetLastResult(),SYS_ERR_IO);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.last_result,SYS_ERR_IO);
        EQ(state.busy,false);USB_Mode_Process();EQ(s_usb_mode.state,USB_MODE_STATE_ERROR);return 0;
    }
    //CASE 358 | HAL打开BUSY回滚PCD上下文 | HAL按真实顺序先写PCD再因锁忙返回，随后Stop重试 | Core不占用，PCD恢复旧值，Stop和FIFO重规划成功
    if (id == 358)
    {
        entries[0].address=0x83;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);fault=19;
        EQ(USB_Device_OpenEp(0x83,2,64),SYS_ERR_BUSY);EQ(USB_Device_GetEpState(0x83,&state),SYS_OK);
        EQ(state.opened,false);EQ(s_pcd.IN_ep[3].maxpacket,0);EQ(s_pcd.OUT_ep[3].maxpacket,0);
        fault=0;EQ(USB_Device_Stop(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);return 0;
    }
    //CASE 359 | PCD反初始化失败恢复注册绑定 | Class已成功释放，但HAL PCD DeInit返回ERROR | 描述符及外部绑定保留，Stop恢复后可重新枚举
    if (id == 359)
    {
        stage2_allocate=true;Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);fault=11;
        EQ(USBD_DeInit(&s_device),USBD_FAIL);EQ(stage2_deinit_calls,1);EQ(s_class_memory_used,false);
        EQ(s_device.pDesc,&s_descriptor_driver);EQ(s_device.pConfDesc,stage2_config);EQ(s_device.pUserData[0],NULL);
        fault=0;EQ(USB_Device_Stop(),SYS_OK);EQ(stage2_deinit_calls,1);EQ(USB_Device_Start(),SYS_OK);
        HAL_PCD_ResetCallback(&s_pcd);Configured();EQ(USB_Device_IsConfigured(),true);
        Request(0x80,USB_REQ_GET_DESCRIPTOR,0x100,0,18);EQ(tx_size,18);return 0;
    }
    //CASE 360 | PIO尾部补齐且多包续传 | Bulk64发送67字节，检查第二包3字节及补齐字节 | HAL只读有界对齐暂存区，第二包尾部补0，完成后才回调
    if (id == 360)
    {
        entries[0].address=0x81;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,2,64),SYS_OK);
        EQ(USB_Device_Transmit(0x81,stage2_data,67),SYS_OK);EQ(tx_size,64);
        HAL_PCD_DataInStageCallback(&s_pcd,1);EQ(tx_size,3);
        EQ(s_pcd.IN_ep[1].xfer_buff,s_tx_pio_pool[1]);EQ(s_tx_pio_pool[1][0],stage2_data[64]);
        EQ(s_tx_pio_pool[1][1],stage2_data[65]);EQ(s_tx_pio_pool[1][2],stage2_data[66]);
        EQ(s_tx_pio_pool[1][3],0);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,true);
        HAL_PCD_DataInStageCallback(&s_pcd,1);EQ(stage2_in_calls,1);
        EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.busy,false);return 0;
    }
    //CASE 361 | Abort释放传输所有权但保留端点 | 活动Bulk IN执行Abort后再次发送 | Abort成功清busy、保留opened并允许重用
    if (id == 361)
    {
        entries[0].address=0x81;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,2,64),SYS_OK);
        EQ(USB_Device_Transmit(0x81,stage2_data,64),SYS_OK);EQ(USB_Device_AbortEp(0x81),SYS_OK);
        EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);EQ(state.opened,true);EQ(state.busy,false);
        EQ(USB_Device_Transmit(0x81,stage2_data,64),SYS_OK);return 0;
    }
    //CASE 362 | Abort失败保留缓冲所有权 | 活动Bulk IN注入HAL Abort失败，再恢复后Abort | 首次失败保持busy和opened，重试成功才释放
    if (id == 362)
    {
        entries[0].address=0x81;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);EQ(Stage2_Boot(true),0);EQ(USB_Device_OpenEp(0x81,2,64),SYS_OK);
        EQ(USB_Device_Transmit(0x81,stage2_data,64),SYS_OK);fault=20;
        EQ(USB_Device_AbortEp(0x81),SYS_ERR_IO);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);
        EQ(state.opened,true);EQ(state.busy,true);fault=0;
        EQ(USB_Device_AbortEp(0x81),SYS_OK);EQ(USB_Device_GetEpState(0x81,&state),SYS_OK);
        EQ(state.opened,true);EQ(state.busy,false);return 0;
    }
    //CASE 365 | 描述符属性和备用设置重复校验 | 保留属性位、零Interval、重复Alt0和缺失配置字符串回调 | 均拒绝注册且旧Class保持有效
    if (id == 365)
    {
        EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);
        entries[0].address=0x81;entries[0].type=2;entries[0].max_packet=64;
        Stage2_ConfigBuild(entries,1);stage2_config[21]=0xC2;
        EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_INVALID_ARG);
        Stage2_ConfigBuild(entries,1);stage2_config[24]=0;
        EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_INVALID_ARG);
        Stage2_ConfigBuild(NULL,0);memcpy(stage2_config+18,stage2_config+9,9);
        stage2_config_length=27;stage2_config[2]=27;
        EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_INVALID_ARG);
        Stage2_ConfigBuild(NULL,0);stage2_config[6]=1;
        EQ(USB_Device_RegisterClass(&stage2_driver,&core_descriptors),SYS_ERR_INVALID_ARG);
        EQ(USB_Device_GetConfigDescriptor(&length),s_config_desc);EQ(length,18);return 0;
    }
    EQ(0,1); return 1;
}

static int Stage2_RunCase(uint32_t id)
{
    int result=Stage2_Case(id);
    uint32_t i, total=seen_rx_fifo;
    for(i=0;i<BSP_USB_ENDPOINT_COUNT;i++)total+=seen_tx_fifo[i];
    g_stage2_observation[0]=s_class_state;g_stage2_observation[1]=s_class_memory_used;
    g_stage2_observation[2]=stage2_init_calls;g_stage2_observation[3]=stage2_deinit_calls;
    g_stage2_observation[4]=stage2_in_calls;g_stage2_observation[5]=stage2_out_calls;
    g_stage2_observation[6]=stage2_iso_in;g_stage2_observation[7]=stage2_iso_out;
    g_stage2_observation[8]=seen_rx_fifo;g_stage2_observation[9]=total;
    g_stage2_observation[10]=bsp_started;g_stage2_observation[11]=s_stop_required;
    return result;
}
