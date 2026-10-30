#include <stdint.h>
#include "usbd_def.h"

//UVC全速配置描述符，当前填写配置头、IAD和VC、VS标准接口

//used保留尚未引用的数组，aligned保证4字节对齐
static uint8_t s_uvc_config_desc[] __attribute__((used, aligned(4))) =
{

    //Configuration Descriptor，共9字节

    0x09U,//bLength：配置描述符自身长度
    0x02U,//bDescriptorType：Configuration
    0x00U, 0x00U,//wTotalLength：小端总长度，待VC和VS描述符补齐后计算
    0x02U,//bNumInterfaces：VC和VS两个接口
    0x01U,//bConfigurationValue：主机选择配置时使用编号1
    0x00U,//iConfiguration：不引用配置名称字符串
    0x80U,//bmAttributes：总线供电
    0xFAU,//bMaxPower：单位2mA，声明最大500mA

    //Interface Association Descriptor，共8字节

    0x08U,//bLength：IAD自身长度
    0x0BU,//bDescriptorType：Interface Association
    0x00U,//bFirstInterface：功能从接口0开始
    0x02U,//bInterfaceCount：关联VC和VS两个接口
    0x0EU,//bFunctionClass：Video类
    0x03U,//bFunctionSubClass：Video Interface Collection
    0x00U,//bFunctionProtocol：未指定协议
    0x00U,//iFunction：不引用功能名称字符串

    //VideoControl Interface Descriptor，共9字节

    0x09U,//bLength：标准接口描述符长度
    0x04U,//bDescriptorType：Interface
    0x00U,//bInterfaceNumber：VC使用接口0
    0x00U,//bAlternateSetting：备用设置0
    0x00U,//bNumEndpoints：当前VC不声明中断端点
    0x0EU,//bInterfaceClass：Video类
    0x01U,//bInterfaceSubClass：VideoControl
    0x00U,//bInterfaceProtocol：未指定协议
    0x00U,//iInterface：不引用接口名称字符串

    //TODO：后续PTS和SCR计数必须匹配上述48MHz时间基准

    //VC Header Descriptor，共13字节，使用UVC 1.0

    0x0DU,//bLength：12字节固定字段加1个VS接口编号
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x01U,//bDescriptorSubtype：VC_HEADER
    0x00U, 0x01U,//bcdUVC：UVC 1.0，0x0100按小端存放
    0x00U, 0x00U,//wTotalLength：VC类专用总长度，待所有Unit和Terminal补齐后计算
    0x00U, 0x6CU, 0xDCU, 0x02U,//dwClockFrequency：声明48MHz的UVC时间戳参考时钟，小端存放

    0x01U,//bInCollection：当前VC管理1个VS接口
    0x01U,//baInterfaceNr：关联VS接口1

    //Camera Terminal Descriptor，共18字节

    0x12U,//bLength：15字节固定字段加3字节控制位图
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x02U,//bDescriptorSubtype：VC_INPUT_TERMINAL输入终端
    0x01U,//bTerminalID：摄像头终端编号1
    0x01U, 0x02U,//wTerminalType：Camera类型0x0201，小端存放
    0x00U,//bAssocTerminal：不关联其他终端
    0x00U,//iTerminal：不设置终端描述字符串
    0x00U, 0x00U,//wObjectiveFocalLengthMin：暂不声明最小物镜焦距
    0x00U, 0x00U,//wObjectiveFocalLengthMax：暂不声明最大物镜焦距
    0x00U, 0x00U,//wOcularFocalLength：暂不声明目镜焦距
    0x03U,//bControlSize：控制能力位图占3字节
    0x00U, 0x00U, 0x00U,//bmControls：暂不声明摄像头控制能力

    //Processing Unit Descriptor，共11字节，使用UVC 1.0

    0x0BU,//bLength：9字节固定字段加2字节控制位图
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x05U,//bDescriptorSubtype：VC_PROCESSING_UNIT处理单元
    0x02U,//bUnitID：处理单元编号2
    0x01U,//bSourceID：输入来自Camera Terminal 1
    0x00U, 0x00U,//wMaxMultiplier：暂不声明数字倍增能力
    0x02U,//bControlSize：控制能力位图占2字节
    0x00U, 0x00U,//bmControls：暂不声明图像处理控制能力
    0x00U,//iProcessing：不设置处理单元描述字符串

    //TODO：在此继续补齐VC的Unit和Terminal描述符

    //VideoStreaming Interface Descriptor，共9字节

    0x09U,//bLength：标准接口描述符长度
    0x04U,//bDescriptorType：Interface
    0x01U,//bInterfaceNumber：VS使用接口1，接口0预留给VC
    0x00U,//bAlternateSetting：备用设置0，暂不开启视频传输
    0x00U,//bNumEndpoints：备用设置0不声明视频数据端点
    0x0EU,//bInterfaceClass：Video类
    0x02U,//bInterfaceSubClass：VideoStreaming
    0x00U,//bInterfaceProtocol：未指定协议
    0x00U//iInterface：不引用接口名称字符串
};

//TODO：继续补齐VC和VS描述符，再回填wTotalLength
