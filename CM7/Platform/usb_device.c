//USB协议适配层：连接官方Device Core与BSP，不在这里直接操作寄存器
//下行：USBD Core -> USBD_LL接口 -> BSP -> HAL PCD
//上行：HAL回调 -> BSP事件表 -> Usb回调 -> USBD_LL事件处理
#include "usb_device.h"
#include "bsp_usb.h"
#include "framework_init.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

//1209:0001只供内部测试，不唯一，不得用于生产或分发
#define USB_TEST_VID 0x1209U
#define USB_TEST_PID 0x0001U
//官方Core上下文，保存设备地址、配置和EP0控制传输状态
static USBD_HandleTypeDef s_device;
//官方Core及基础Class已完成初始化
static bool s_initialized;
//Platform已成功启动USB连接
static bool s_started;
//保存底层失败原因，避免官方Core返回值丢失错误信息
static volatile SysResult_t s_last_result;
//Class使用的固定内存块，位于普通RW区域，不进行动态堆分配
static uint8_t s_class_memory[1024] __attribute__((aligned(32)));
//固定块是否已分配，同一时间只允许一个使用者
static bool s_class_memory_used;

//描述符额外留出尾部空间，适配PIO的Word读取
static uint8_t s_device_desc[20] __attribute__((aligned(4))) =
{
    18U, USB_DESC_TYPE_DEVICE, //设备描述符长度和类型
    0x00U, 0x02U,             //USB版本2.00，低字节在前
    0U, 0U, 0U,              //设备级不指定类别，由接口描述符声明
    64U,                     //EP0最大包长64字节
    LOBYTE(USB_TEST_VID), HIBYTE(USB_TEST_VID), //测试厂商编号
    LOBYTE(USB_TEST_PID), HIBYTE(USB_TEST_PID), //测试产品编号
    0x00U, 0x01U, //设备版本1.00
    1U, 2U, 3U,   //厂家、产品和序列号字符串索引
    1U            //只有一个配置
};
//一个Vendor接口，无非零端点，不声明视频能力
static uint8_t s_config_desc[20] __attribute__((aligned(4))) =
{
    9U, USB_DESC_TYPE_CONFIGURATION, //配置描述符长度和类型
    18U, 0U, //配置与接口描述符总长度18字节，低字节在前
    1U,      //一个接口
    1U,      //配置值1，供SET_CONFIGURATION选择
    0U,      //不提供配置名称字符串索引
    0x80U,   //总线供电，不声明远程唤醒
    250U,    //最大电流单位为2mA，此处声明500mA
    9U, USB_DESC_TYPE_INTERFACE, //接口描述符长度和类型
    0U, 0U, //接口编号0、备用设置0
    0U,     //不配置非零端点，EP0不计入这里
    0xFFU,  //厂商自定义接口，尚未声明为UVC
    0U, 0U, //子类和协议均为0
    0U      //不提供接口名称字符串索引
};
//语言描述符：长度4、字符串类型、语言编号0x0409
static uint8_t s_lang_desc[4] = {4U, USB_DESC_TYPE_STRING, 0x09U, 0x04U};
//各字符串查询共用的发送缓冲区，由EP0顺序处理请求
static uint8_t s_string_desc[64] __attribute__((aligned(4)));
//接口0当前备用设置，基础枚举固定为0
static uint8_t s_interface_alt;

//官方Core描述符回调
//返回设备描述符，仅发送有效的18字节，不发送尾部补齐空间
static uint8_t *Usb_DeviceDesc(USBD_SpeedTypeDef speed, uint16_t *length);

//返回字符串支持的语言编号，0x0409表示英语
static uint8_t *Usb_LangDesc(USBD_SpeedTypeDef speed, uint16_t *length);

//将厂家名称转换为USB使用的UTF-16LE字符串描述符
static uint8_t *Usb_Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length);

//返回主机设备列表中使用的产品名称
static uint8_t *Usb_Product(USBD_SpeedTypeDef speed, uint16_t *length);

//返回当前阶段固定测试序列号，不代表芯片唯一编号
static uint8_t *Usb_Serial(USBD_SpeedTypeDef speed, uint16_t *length);

//返回基础枚举配置的名称
static uint8_t *Usb_ConfigString(USBD_SpeedTypeDef speed, uint16_t *length);

//返回接口名称，说明本阶段仅有EP0控制通信
static uint8_t *Usb_InterfaceString(USBD_SpeedTypeDef speed, uint16_t *length);

//基础Class只处理接口0的标准请求
//SET_CONFIGURATION时调用，基础Class只接受配置1
static uint8_t Usb_ClassInit(USBD_HandleTypeDef *device, uint8_t config);

//取消配置时调用，基础Class没有额外端点或资源需要释放
static uint8_t Usb_ClassDeInit(USBD_HandleTypeDef *device, uint8_t config);

//处理接口0的标准查询，其他请求通过EP0 STALL拒绝
static uint8_t Usb_ClassSetup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request);

//返回Full-Speed配置和接口描述符，共18字节
static uint8_t *Usb_ConfigDesc(uint16_t *length);

//返回空描述符，本设备不提供高速和其他速度配置
static uint8_t *Usb_NoDesc(uint16_t *length);

//PCD事件转交官方Core
//PCD收到SETUP后进入这里，由官方Core分发设备或接口请求
static void Usb_Setup(uint8_t *data);

//把OUT完成事件交给官方Core，推进接收或状态阶段
static void Usb_DataOut(uint8_t ep, uint8_t *data);

//把IN完成事件交给官方Core，推进发送或状态阶段
static void Usb_DataIn(uint8_t ep, uint8_t *data);

//固定为Full-Speed，再由官方Core复位状态并打开EP0
static void Usb_Reset(void);

//把总线挂起事件转交官方Core
static void Usb_Suspend(void);

//把总线恢复事件转交官方Core
static void Usb_Resume(void);

//把设备连接事件转交官方Core
static void Usb_Connect(void);

//把设备断开事件转交官方Core
static void Usb_Disconnect(void);

//记录BSP错误并转换为USBD结果
//锁存底层失败原因，并转换为官方Core返回状态
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result);

//在Application等级启动，不改Application源码
//在Application初始化等级启动USB，供主机开始枚举
static SysResult_t Usb_AutoStart(void);

//描述符查询表：主机GET_DESCRIPTOR时，Core按类型调用对应函数
static USBD_DescriptorsTypeDef s_descriptors =
{
    Usb_DeviceDesc,      //设备描述符
    Usb_LangDesc,        //字符串语言编号
    Usb_Manufacturer,    //厂家名称
    Usb_Product,         //产品名称
    Usb_Serial,          //设备序列号
    Usb_ConfigString,    //配置名称
    Usb_InterfaceString  //接口名称
};
//基础Class操作表：Core处理配置和接口请求时调用，不负责视频传输
static USBD_ClassTypeDef s_base_class =
{
    //主机选择配置后初始化Class
    .Init = Usb_ClassInit,

    //主机取消配置时释放Class
    .DeInit = Usb_ClassDeInit,

    //处理发给接口的控制请求
    .Setup = Usb_ClassSetup,

    //不提供高速配置
    .GetHSConfigDescriptor = Usb_NoDesc,

    //返回实际使用的全速配置
    .GetFSConfigDescriptor = Usb_ConfigDesc,

    //不提供其他速度配置
    .GetOtherSpeedConfigDescriptor = Usb_NoDesc,

    //不提供高速设备限定描述符
    .GetDeviceQualifierDescriptor = Usb_NoDesc
};
//绑定BSP硬件事件与本文件适配函数，回调在USB中断上下文执行
static const BspUsbCallbacks_t s_callbacks =
{
    Usb_Setup,      //主机控制请求
    Usb_DataOut,    //主机到设备的数据阶段完成
    Usb_DataIn,     //设备到主机的数据阶段完成
    Usb_Reset,      //USB总线复位
    Usb_Suspend,    //总线挂起
    Usb_Resume,     //总线恢复
    Usb_Connect,    //连接事件
    Usb_Disconnect  //断开事件
};

//初始化官方USB Device Core，并注册基础枚举Class
SysResult_t USB_Device_Init(void)
{
    USBD_StatusTypeDef status;
    if (s_initialized)
        return SYS_OK;
    s_last_result = SYS_OK;
    //官方Core通过USBD_LL_Init回到本文件，再调用BSP初始化
    status = USBD_Init(&s_device, &s_descriptors, 0U);
    if (status != USBD_OK)
        return s_last_result != SYS_OK ? s_last_result : SYS_ERR_IO;
    //注册只支持基础枚举的Class，暂不提供UVC视频功能
    status = USBD_RegisterClass(&s_device, &s_base_class);
    if (status != USBD_OK)
    {
        (void)USBD_DeInit(&s_device);
        return SYS_ERR_IO;
    }
    s_initialized = true;
    return SYS_OK;
}
//停止时替换Class和描述符，调用方需保证对象长期有效
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors)
{
    uint16_t length = 0U;
    uint8_t *config;
    if (driver == NULL || descriptors == NULL || driver->Init == NULL ||
        driver->DeInit == NULL || driver->Setup == NULL || driver->GetFSConfigDescriptor == NULL ||
        descriptors->GetDeviceDescriptor == NULL || descriptors->GetLangIDStrDescriptor == NULL ||
        descriptors->GetManufacturerStrDescriptor == NULL || descriptors->GetProductStrDescriptor == NULL ||
        descriptors->GetSerialStrDescriptor == NULL || descriptors->GetConfigurationStrDescriptor == NULL ||
        descriptors->GetInterfaceStrDescriptor == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_initialized || s_started)
        return SYS_ERR_INVALID_STATE;
    config = driver->GetFSConfigDescriptor(&length);
    if (config == NULL || length < 9U || config[0] != 9U || config[1] != USB_DESC_TYPE_CONFIGURATION)
        return SYS_ERR_INVALID_ARG;
    //单Class模式替换注册，不累计旧Class数量
    s_device.NumClasses = 0U;
    if (USBD_RegisterClass(&s_device, driver) != USBD_OK)
        return SYS_ERR_IO;
    //绑定新Class配套的设备和字符串描述符
    s_device.pDesc = descriptors;
    return SYS_OK;
}
//清除上次配置状态，启动连接并等待主机重新枚举
SysResult_t USB_Device_Start(void)
{
    USBD_StatusTypeDef status;
    if (!s_initialized)
        return SYS_ERR_INVALID_STATE;
    if (s_started)
        return SYS_ERR_BUSY;
    s_last_result = SYS_OK;
    //重新连接前等待主机重新枚举，不沿用上次CONFIGURED状态
    s_device.dev_state = USBD_STATE_DEFAULT;
    s_device.dev_config = 0U;
    status = USBD_Start(&s_device);
    if (status != USBD_OK || s_last_result != SYS_OK)
        return s_last_result != SYS_OK ? s_last_result : SYS_ERR_IO;
    //启动成功后提交软件状态
    s_started = true;
    return SYS_OK;
}
//停止官方Core和底层PCD，保留底层失败原因
SysResult_t USB_Device_Stop(void)
{
    USBD_StatusTypeDef status;
    if (!s_initialized)
        return SYS_ERR_INVALID_STATE;
    s_last_result = SYS_OK;
    //官方Stop会忽略LL错误，本层保留真实底层结果
    status = USBD_Stop(&s_device);
    if (status != USBD_OK || s_last_result != SYS_OK)
        return s_last_result != SYS_OK ? s_last_result : SYS_ERR_IO;
    s_started = false;
    return SYS_OK;
}
//查询是否已启动、无已记录底层错误且主机已完成配置
bool USB_Device_IsConfigured(void)
{
    return s_started && s_last_result == SYS_OK && s_device.dev_state == USBD_STATE_CONFIGURED;
}
//读取最近记录的底层失败结果，供上层诊断
SysResult_t USB_Device_GetLastResult(void)
{
    return s_last_result;
}

//官方Core初始化入口，准备BSP并绑定硬件事件回调
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device)
{
    SysResult_t result;
    if (device != &s_device)
        return USBD_FAIL;
    result = BSP_USB_Init();
    if (result == SYS_OK) result = BSP_USB_SetCallbacks(&s_callbacks);
    return Usb_LlResult(result);
}
//官方Core释放入口，交给BSP关闭硬件资源
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_DeInit());
}
//官方Core启动入口，连接USB设备并开放硬件中断
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Start());
}
//官方Core停止入口，记录并转换BSP停止结果
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Stop());
}
//官方Core请求打开端点，转交BSP配置PCD
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t ep, uint8_t type, uint16_t size)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_OpenEp(ep, type, size));
}
//官方Core请求关闭端点，转交BSP执行
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_CloseEp(ep));
}
//官方Core请求清空端点FIFO
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_FlushEp(ep));
}
//官方Core拒绝请求时，通过BSP设置STALL
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_StallEp(ep));
}
//官方Core请求清除端点STALL
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_ClearStallEp(ep));
}
//把BSP的布尔STALL状态转换为官方Core使用的0或1
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t ep)
{
    return device == &s_device && BSP_USB_IsStallEp(ep) ? 1U : 0U;
}
//SET_ADDRESS处理时，将主机分配的地址写入PCD
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_SetAddress(address));
}
//控制传输发送入口，buffer在传输结束前必须有效
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Transmit(ep, buffer, size));
}
//控制传输接收入口，为下一次OUT阶段准备缓冲区
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *buffer, uint32_t size)
{
    if (device != &s_device)
        return USBD_FAIL;
    return Usb_LlResult(BSP_USB_Receive(ep, buffer, size));
}
//向官方Core返回本次OUT实际收到的字节数
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t ep)
{
    return device == &s_device ? BSP_USB_GetRxSize(ep) : 0U;
}
//提供官方Core所需的毫秒延时，不用于USB中断内等待
void USBD_LL_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}
//从唯一固定内存块分配Class空间，不使用malloc
void *USB_Device_Alloc(uint32_t size)
{
    if (size == 0U || size > sizeof(s_class_memory) || s_class_memory_used)
        return NULL;
    s_class_memory_used = true;
    return s_class_memory;
}
//释放本模块的固定内存块，忽略不属于本模块的地址
void USB_Device_Free(void *memory)
{
    if (memory == s_class_memory)
        s_class_memory_used = false;
}

//DEVICE阶段准备Core和Class，APPLICATION阶段才对主机启动连接
FRAMEWORK_INIT_REGISTER(USB_Device_Init, INIT_LEVEL_DEVICE);
FRAMEWORK_INIT_REGISTER(Usb_AutoStart, INIT_LEVEL_APPLICATION);

//内部功能函数实现
//在Application初始化等级启动USB，供主机开始枚举
static SysResult_t Usb_AutoStart(void)
{
    return USB_Device_Start();
}
//锁存底层失败原因，并转换为官方Core返回状态
static USBD_StatusTypeDef Usb_LlResult(SysResult_t result)
{
    if (result == SYS_OK)
        return USBD_OK;
    s_last_result = result;
    return result == SYS_ERR_BUSY ? USBD_BUSY : USBD_FAIL;
}
//PCD收到SETUP后进入这里，由官方Core分发设备或接口请求
static void Usb_Setup(uint8_t *data)
{
    (void)USBD_LL_SetupStage(&s_device, data);
}
//把OUT完成事件交给官方Core，推进接收或状态阶段
static void Usb_DataOut(uint8_t ep, uint8_t *data)
{
    (void)USBD_LL_DataOutStage(&s_device, ep, data);
}
//把IN完成事件交给官方Core，推进发送或状态阶段
static void Usb_DataIn(uint8_t ep, uint8_t *data)
{
    (void)USBD_LL_DataInStage(&s_device, ep, data);
}
//固定为Full-Speed，再由官方Core复位状态并打开EP0
static void Usb_Reset(void)
{
    (void)USBD_LL_SetSpeed(&s_device, USBD_SPEED_FULL);
    (void)USBD_LL_Reset(&s_device);
}
//把总线挂起事件转交官方Core
static void Usb_Suspend(void)
{
    (void)USBD_LL_Suspend(&s_device);
}
//把总线恢复事件转交官方Core
static void Usb_Resume(void)
{
    (void)USBD_LL_Resume(&s_device);
}
//把设备连接事件转交官方Core
static void Usb_Connect(void)
{
    (void)USBD_LL_DevConnected(&s_device);
}
//把设备断开事件转交官方Core
static void Usb_Disconnect(void)
{
    (void)USBD_LL_DevDisconnected(&s_device);
}
//返回设备描述符，仅发送有效的18字节，不发送尾部补齐空间
static uint8_t *Usb_DeviceDesc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = 18U;
    return s_device_desc;
}
//返回字符串支持的语言编号，0x0409表示英语
static uint8_t *Usb_LangDesc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(s_lang_desc);
    return s_lang_desc;
}
//将厂家名称转换为USB使用的UTF-16LE字符串描述符
static uint8_t *Usb_Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)"h7_mac", s_string_desc, length);
    return s_string_desc;
}
//返回主机设备列表中使用的产品名称
static uint8_t *Usb_Product(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)"h7_mac USB FS Test", s_string_desc, length);
    return s_string_desc;
}
//返回当前阶段固定测试序列号，不代表芯片唯一编号
static uint8_t *Usb_Serial(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)"H7MAC-STAGE1", s_string_desc, length);
    return s_string_desc;
}
//返回基础枚举配置的名称
static uint8_t *Usb_ConfigString(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)"Enumeration", s_string_desc, length);
    return s_string_desc;
}
//返回接口名称，说明本阶段仅有EP0控制通信
static uint8_t *Usb_InterfaceString(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)"EP0 only", s_string_desc, length);
    return s_string_desc;
}
//返回Full-Speed配置和接口描述符，共18字节
static uint8_t *Usb_ConfigDesc(uint16_t *length)
{
    *length = 18U;
    return s_config_desc;
}
//返回空描述符，本设备不提供高速和其他速度配置
static uint8_t *Usb_NoDesc(uint16_t *length)
{
    *length = 0U;
    return NULL;
}
//SET_CONFIGURATION时调用，基础Class只接受配置1
static uint8_t Usb_ClassInit(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device;
    return config == 1U ? USBD_OK : USBD_FAIL;
}
//取消配置时调用，基础Class没有额外端点或资源需要释放
static uint8_t Usb_ClassDeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device;
    (void)config;
    return USBD_OK;
}
//处理接口0的标准查询，其他请求通过EP0 STALL拒绝
static uint8_t Usb_ClassSetup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
    //应答由中断异步发送，使用静态存储保证返回后数据仍有效
    static uint16_t status;
    //仅有接口0、备用设置0，没有UVC切换或厂商命令
    if ((request->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD && request->wIndex == 0U)
    {
        if (request->bRequest == USB_REQ_GET_INTERFACE && request->wLength == 1U)
        {
            s_interface_alt = 0U;
            (void)USBD_CtlSendData(device, &s_interface_alt, 1U);
            return USBD_OK;
        }
        if (request->bRequest == USB_REQ_GET_STATUS && request->wLength == 2U)
        {
            status = 0U;
            (void)USBD_CtlSendData(device, (uint8_t *)&status, 2U);
            return USBD_OK;
        }
    }
    //未支持的请求设置STALL，不能伪装为处理成功
    USBD_CtlError(device, request);
    return USBD_FAIL;
}
