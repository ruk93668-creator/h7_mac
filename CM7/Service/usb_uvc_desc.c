#include <stdint.h>
#include "usbd_def.h"

//UVC全速配置描述符，当前填写配置头、IAD和VC、VS标准接口

//used保留尚未引用的数组，aligned保证4字节对齐
static uint8_t s_uvc_config_desc[] __attribute__((used, aligned(4))) =
{

    //Configuration Descriptor，共9字节

    //bLength：配置描述符自身长度
    0x09U,

    //bDescriptorType：Configuration
    0x02U,

    //wTotalLength：小端总长度，待VC和VS描述符补齐后计算
    0x00U, 0x00U,

    //bNumInterfaces：VC和VS两个接口
    0x02U,

    //bConfigurationValue：主机选择配置时使用编号1
    0x01U,

    //iConfiguration：不引用配置名称字符串
    0x00U,

    //bmAttributes：总线供电
    0x80U,

    //bMaxPower：单位2mA，声明最大500mA
    0xFAU,

    //Interface Association Descriptor，共8字节

    //bLength：IAD自身长度
    0x08U,

    //bDescriptorType：Interface Association
    0x0BU,

    //bFirstInterface：功能从接口0开始
    0x00U,

    //bInterfaceCount：关联VC和VS两个接口
    0x02U,

    //bFunctionClass：Video类
    0x0EU,

    //bFunctionSubClass：Video Interface Collection
    0x03U,

    //bFunctionProtocol：未指定协议
    0x00U,

    //iFunction：不引用功能名称字符串
    0x00U,

    //VideoControl Interface Descriptor，共9字节

    //bLength：标准接口描述符长度
    0x09U,

    //bDescriptorType：Interface
    0x04U,

    //bInterfaceNumber：VC使用接口0
    0x00U,

    //bAlternateSetting：备用设置0
    0x00U,

    //bNumEndpoints：当前VC不声明中断端点
    0x00U,

    //bInterfaceClass：Video类
    0x0EU,

    //bInterfaceSubClass：VideoControl
    0x01U,

    //bInterfaceProtocol：未指定协议
    0x00U,

    //iInterface：不引用接口名称字符串
    0x00U,

    //TODO：在此补齐VC类专用描述符

    //VideoStreaming Interface Descriptor，共9字节

    //bLength：标准接口描述符长度
    0x09U,

    //bDescriptorType：Interface
    0x04U,

    //bInterfaceNumber：VS使用接口1，接口0预留给VC
    0x01U,

    //bAlternateSetting：备用设置0，暂不开启视频传输
    0x00U,

    //bNumEndpoints：备用设置0不声明视频数据端点
    0x00U,

    //bInterfaceClass：Video类
    0x0EU,

    //bInterfaceSubClass：VideoStreaming
    0x02U,

    //bInterfaceProtocol：未指定协议
    0x00U,

    //iInterface：不引用接口名称字符串
    0x00U
};

//TODO：继续补齐VC和VS描述符，再回填wTotalLength
