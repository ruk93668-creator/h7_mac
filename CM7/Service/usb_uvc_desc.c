#include <stdint.h>
#include "usbd_def.h"

//UVC全速配置描述符，当前只填写配置头和IAD

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
    0x00U
};

//TODO：继续补齐VC和VS描述符，再回填wTotalLength
