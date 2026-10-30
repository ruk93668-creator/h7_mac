//原有TEST枚举兼容模块，仅迁移既有行为
#include "usb_device.h"
#include "framework_init.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
#include <stddef.h>

//1209:0001只供内部测试，不唯一，不得用于生产或分发
#define USB_TEST_VID 0x1209U
#define USB_TEST_PID 0x0001U

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

//SERVICE阶段登记原有TEST，不依赖Mode实现
static SysResult_t Usb_TestRegister(void);

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

//Core已在DEVICE阶段初始化，此处只注册原有TEST
static SysResult_t Usb_TestRegister(void)
{
    return USB_Device_RegisterClass(&s_base_class, &s_descriptors);
}

FRAMEWORK_INIT_REGISTER(Usb_TestRegister, INIT_LEVEL_SERVICE);

//内部功能函数实现

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
