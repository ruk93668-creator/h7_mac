//MSC静态描述符：独立Full-Speed枚举，不提供BOT或存储能力
#include "usb_msc_desc.h"
#include "usbd_ctlreq.h"
#include <stddef.h>

//1209:0001仅供内部测试，不得作为生产或分发设备的身份
#define USB_MSC_TEST_VID 0x1209U
#define USB_MSC_TEST_PID 0x0001U

//有效长度18字节，额外空间只用于PIO尾部按Word读取
static uint8_t s_device_desc[20] __attribute__((aligned(4))) =
{
    18U, USB_DESC_TYPE_DEVICE, //设备描述符长度和类型
    0x00U, 0x02U,             //USB 2.00，小端编码
    0U, 0U, 0U,               //设备级不指定类别，由接口声明MSC
    64U,                      //EP0最大包长64字节
    LOBYTE(USB_MSC_TEST_VID), HIBYTE(USB_MSC_TEST_VID),
    LOBYTE(USB_MSC_TEST_PID), HIBYTE(USB_MSC_TEST_PID),
    0x00U, 0x01U,             //设备版本1.00
    1U, 2U, 0U,               //厂家和产品字符串，无序列号
    1U                        //一个配置
};

//配置、接口和两个端点共32字节，尾部填充不计入协议长度
static uint8_t s_config_desc[36] __attribute__((aligned(4))) =
{
    9U, USB_DESC_TYPE_CONFIGURATION,
    32U, 0U,                  //wTotalLength：9+9+7+7
    1U,                       //一个接口
    1U,                       //配置值1
    0U,                       //无配置名称字符串
    0x80U,                    //总线供电，不声明远程唤醒
    250U,                     //最大电流500mA，单位2mA

    9U, USB_DESC_TYPE_INTERFACE,
    0U, 0U,                   //接口0、Alt0
    2U,                       //两个非零端点
    0x08U,                    //Mass Storage Class
    0x06U,                    //SCSI transparent command set
    0x50U,                    //Bulk-Only Transport协议标识
    0U,                       //无接口名称字符串

    7U, USB_DESC_TYPE_ENDPOINT,
    USB_MSC_IN_EP,            //Bulk IN：EP2 IN
    0x02U,                    //Bulk传输
    USB_MSC_PACKET_SIZE, 0U,  //FS最大包长64字节，小端编码
    0U,                       //Bulk无周期调度间隔

    7U, USB_DESC_TYPE_ENDPOINT,
    USB_MSC_OUT_EP,           //Bulk OUT：EP2 OUT
    0x02U,                    //Bulk传输
    USB_MSC_PACKET_SIZE, 0U,  //FS最大包长64字节，小端编码
    0U                        //Bulk无周期调度间隔
};

//语言描述符：English (United States)，语言编号0x0409
static uint8_t s_lang_desc[4] = {4U, USB_DESC_TYPE_STRING, 0x09U, 0x04U};

//EP0串行查询复用静态字符串缓冲，避免返回栈内临时数据
static uint8_t s_string_desc[64] __attribute__((aligned(4)));

//只返回Full-Speed设备描述符，高速查询返回空描述符
static uint8_t *Msc_DeviceDesc(USBD_SpeedTypeDef speed, uint16_t *length);

//返回支持的字符串语言编号
static uint8_t *Msc_LangDesc(USBD_SpeedTypeDef speed, uint16_t *length);

//返回内部测试厂家字符串
static uint8_t *Msc_Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length);

//返回枚举测试产品字符串
static uint8_t *Msc_Product(USBD_SpeedTypeDef speed, uint16_t *length);

//静态描述符回调表不提供未声明的序列号、配置或接口字符串
static USBD_DescriptorsTypeDef s_descriptors =
{
    .GetDeviceDescriptor = Msc_DeviceDesc,
    .GetLangIDStrDescriptor = Msc_LangDesc,
    .GetManufacturerStrDescriptor = Msc_Manufacturer,
    .GetProductStrDescriptor = Msc_Product
};

//返回静态配置描述符及协议长度，不包含PIO尾部填充
uint8_t *USB_MSC_GetFSConfigDescriptor(uint16_t *length)
{
    if (length == NULL)
        return NULL;
    *length = 32U;
    return s_config_desc;
}

//返回静态设备与字符串回调表，由USB Device注册过程使用
USBD_DescriptorsTypeDef *USB_MSC_GetDescriptors(void)
{
    return &s_descriptors;
}

//内部功能函数实现

//只返回Full-Speed设备描述符，高速查询返回空描述符
static uint8_t *Msc_DeviceDesc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    if (length == NULL)
        return NULL;
    *length = speed == USBD_SPEED_FULL ? 18U : 0U;
    return *length != 0U ? s_device_desc : NULL;
}

//返回支持的字符串语言编号
static uint8_t *Msc_LangDesc(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    if (length == NULL)
        return NULL;
    *length = sizeof(s_lang_desc);
    return s_lang_desc;
}

//返回内部测试厂家字符串
static uint8_t *Msc_Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    if (length == NULL)
        return NULL;
    USBD_GetString((uint8_t *)"Internal test", s_string_desc, length);
    return s_string_desc;
}

//返回枚举测试产品字符串
static uint8_t *Msc_Product(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    if (length == NULL)
        return NULL;
    USBD_GetString((uint8_t *)"MSC-1 Enumeration", s_string_desc, length);
    return s_string_desc;
}
