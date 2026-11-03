#include "usb_uvc_desc.h"
#include <stddef.h>
#include <stdbool.h>

//UVC 1.1描述符，当前仅提供数据，不注册或启动Class

//UVC设备描述符；TEST描述符由独立usb_test_device模块维护
static uint8_t s_uvc_device_desc[20] __attribute__((aligned(4))) =
{
    0x12U,//bLength：设备描述符有效长度18字节
    0x01U,//bDescriptorType：Device
    0x00U, 0x02U,//bcdUSB：USB 2.00
    0xEFU,//bDeviceClass：Miscellaneous，配合IAD
    0x02U,//bDeviceSubClass：Common Class
    0x01U,//bDeviceProtocol：Interface Association Descriptor
    0x40U,//bMaxPacketSize0：EP0最大包长64字节
    0x09U, 0x12U,//idVendor：沿用内部测试VID 0x1209
    0x01U, 0x00U,//idProduct：沿用内部测试PID 0x0001
    0x00U, 0x01U,//bcdDevice：设备版本1.00
    0x00U,//iManufacturer：本模块暂不提供厂商字符串
    0x00U,//iProduct：本模块暂不提供产品字符串
    0x00U,//iSerialNumber：本模块暂不提供序列号字符串
    0x01U//bNumConfigurations：1个配置
};

//语言描述符，English (United States)
static uint8_t s_uvc_langid_desc[4] __attribute__((aligned(4))) =
{
    0x04U, 0x03U, 0x09U, 0x04U
};

//功能名称有效24字节，已满足32位PIO读取边界
static uint8_t s_uvc_function_desc[] __attribute__((aligned(4))) =
{
    USB_UVC_FUNCTION_STR_DESC_SIZE, 0x03U,               // 有效长度24，STRING类型
    0x52U, 0x00U,               // R
    0x6FU, 0x00U,               // o
    0x78U, 0x00U,               // x
    0x79U, 0x00U,               // y
    0x20U, 0x00U,               // 空格
    0x43U, 0x00U,               // C
    0x61U, 0x00U,               // a
    0x6DU, 0x00U,               // m
    0x65U, 0x00U,               // e
    0x72U, 0x00U,               // r
    0x61U, 0x00U                // a
};
typedef char UvcFunctionLengthCheck[
    sizeof(s_uvc_function_desc) == USB_UVC_FUNCTION_STR_DESC_SIZE ? 1 : -1];

//180字节有效数据，保留显式padding，结构按4字节对齐
static struct __attribute__((aligned(4)))
{
    uint8_t descriptor[USB_UVC_FS_CONFIG_DESC_SIZE];
    uint8_t padding;
} s_uvc_config_storage __attribute__((aligned(4))) =
{
{

    //Configuration Descriptor，共9字节

    0x09U,//bLength：配置描述符自身长度
    0x02U,//bDescriptorType：Configuration
    0xB4U, 0x00U,//wTotalLength：整个Configuration共180字节，小端存放
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
    0x05U,//iFunction：索引5，UVC Camera功能名称

    //VideoControl Interface Descriptor，共9字节

    0x09U,//bLength：标准接口描述符长度
    0x04U,//bDescriptorType：Interface
    0x00U,//bInterfaceNumber：VC使用接口0
    0x00U,//bAlternateSetting：备用设置0
    0x00U,//bNumEndpoints：当前VC不声明中断端点
    0x0EU,//bInterfaceClass：Video类
    0x01U,//bInterfaceSubClass：VideoControl
    0x00U,//bInterfaceProtocol：未指定协议
    0x05U,//iInterface：与IAD共用索引5的功能名称
	
    //VC Header Descriptor，共13字节，使用UVC 1.1

    0x0DU,//bLength：12字节固定字段加1个VS接口编号
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x01U,//bDescriptorSubtype：VC_HEADER
    0x10U, 0x01U,//bcdUVC：UVC 1.1，0x0110按小端存放
    0x34U, 0x00U,//wTotalLength：VC类专用13+18+12+9=52字节，小端存放
    0x00U, 0x6CU, 0xDCU, 0x02U,//dwClockFrequency：1.1已废弃，保留旧值，不作为Probe/Commit时钟依据
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

    //Processing Unit Descriptor，共12字节，使用UVC 1.1

    0x0CU,//bLength：10字节固定字段加2字节控制位图
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x05U,//bDescriptorSubtype：VC_PROCESSING_UNIT处理单元
    0x02U,//bUnitID：处理单元编号2
    0x01U,//bSourceID：输入来自Camera Terminal 1
    0x00U, 0x00U,//wMaxMultiplier：暂不声明数字倍增能力
    0x02U,//bControlSize：控制能力位图占2字节
    0x00U, 0x00U,//bmControls：暂不声明图像处理控制能力
    0x00U,//iProcessing：不设置处理单元描述字符串
    0x00U,//bmVideoStandards：值为0，忽略模拟视频制式位图

    //Output Terminal Descriptor，共9字节

    0x09U,//bLength：输出终端描述符长度
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x03U,//bDescriptorSubtype：VC_OUTPUT_TERMINAL输出终端
    0x03U,//bTerminalID：输出终端编号3
    0x01U, 0x01U,//wTerminalType：USB Streaming类型0x0101，小端存放
    0x00U,//bAssocTerminal：不关联其他终端
    0x02U,//bSourceID：输入来自Processing Unit 2
    0x00U,//iTerminal：不设置终端描述字符串

    //VideoStreaming Interface Descriptor，共9字节

    0x09U,//bLength：标准接口描述符长度
    0x04U,//bDescriptorType：Interface
    0x01U,//bInterfaceNumber：VS使用接口1，接口0预留给VC
    0x00U,//bAlternateSetting：设置0，不开启视频传输
    0x00U,//bNumEndpoints：设置0，不作为声明视频数据端点
    0x0EU,//bInterfaceClass：Video类
    0x02U,//bInterfaceSubClass：VideoStreaming
    0x00U,//bInterfaceProtocol：未指定协议
    0x00U,//iInterface：不引用接口名称字符串

    //VS Input Header Descriptor，共14字节，暂按1个视频格式填写

    0x0EU,//bLength：13字节固定字段加1字节格式控制位图
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x01U,//bDescriptorSubtype：VS_INPUT_HEADER
    0x01U,//bNumFormats：后续填写1个视频格式描述符
    0x4DU, 0x00U,//wTotalLength：VS类专用14+27+30+6=77字节，小端存放
    0x81U,//bEndpointAddress：视频数据使用EP1 IN，后续接入端点
    0x00U,//bmInfo：不支持动态切换格式
    0x03U,//bTerminalLink：关联VC中的Output Terminal 3
    0x00U,//bStillCaptureMethod：不支持静态图像捕获
    0x00U,//bTriggerSupport：不支持硬件触发
    0x00U,//bTriggerUsage：未启用硬件触发
    0x01U,//bControlSize：每个格式的控制位图占1字节
    0x00U,//bmaControls：第1个格式暂不声明控制能力

    //Uncompressed Format Descriptor，共27字节，YUY2格式

    0x1BU,//bLength：非压缩格式描述符长度
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x04U,//bDescriptorSubtype：VS_FORMAT_UNCOMPRESSED
    0x01U,//bFormatIndex：格式编号1
    0x01U,//bNumFrameDescriptors：本格式包含1个Frame描述符
    0x59U, 0x55U, 0x59U, 0x32U,
	0x00U, 0x00U, 
	0x10U, 0x00U, 
	0x80U, 0x00U, 
	0x00U, 0xAAU, 0x00U, 0x38U, 0x9BU, 0x71U,//guidFormat：YUY2标准GUID
    0x10U,//bBitsPerPixel：每像素16位
    0x01U,//bDefaultFrameIndex：默认使用Frame 1
    0x00U,//bAspectRatioX：不声明宽高比横向分量
    0x00U,//bAspectRatioY：不声明宽高比纵向分量
    0x00U,//bmInterlaceFlags：逐行扫描，非交错
    0x00U,//bCopyProtect：不限制复制

    //Uncompressed Frame Descriptor，共30字节，启动时填入视频配置

    0x1EU,//bLength：26字节固定字段加1个4字节离散帧间隔
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x05U,//bDescriptorSubtype：VS_FRAME_UNCOMPRESSED
    0x01U,//bFrameIndex：帧配置编号1
    0x00U,//bmCapabilities：保留当前帧能力位图为0
    0x00U, 0x00U,//wWidth：配置写入
    0x00U, 0x00U,//wHeight：配置写入
    0x00U, 0x00U, 0x00U, 0x00U,//dwMinBitRate：配置计算
    0x00U, 0x00U, 0x00U, 0x00U,//dwMaxBitRate：单一帧率
    0x00U, 0x00U, 0x00U, 0x00U,//dwMaxVideoFrameBufferSize
    0x00U, 0x00U, 0x00U, 0x00U,//dwDefaultFrameInterval
    0x01U,//bFrameIntervalType：一个离散间隔
    0x00U, 0x00U, 0x00U, 0x00U,//dwFrameInterval

    //Color Matching Descriptor，共6字节

    0x06U,//bLength：颜色匹配描述符长度
    0x24U,//bDescriptorType：CS_INTERFACE类专用接口描述符
    0x0DU,//bDescriptorSubtype：VS_COLORFORMAT
    0x00U,//bColorPrimaries：未指定
    0x00U,//bTransferCharacteristics：未指定
    0x00U,//bMatrixCoefficients：未指定

    //VideoStreaming Alt 1 Interface Descriptor，共9字节

    0x09U,//bLength：标准接口描述符长度
    0x04U,//bDescriptorType：Interface
    0x01U,//bInterfaceNumber：VS接口1
    0x01U,//bAlternateSetting：备用设置1
    0x01U,//bNumEndpoints：使用1个视频数据端点
    0x0EU,//bInterfaceClass：Video类
    0x02U,//bInterfaceSubClass：VideoStreaming
    0x00U,//bInterfaceProtocol：未指定协议
    0x00U,//iInterface：无字符串索引

    //VideoStreaming Endpoint Descriptor，共7字节

    0x07U,//bLength：端点描述符长度
    0x05U,//bDescriptorType：Endpoint
    0x81U,//bEndpointAddress：EP1 IN，设备向主机发送视频
    0x05U,//bmAttributes：等时传输，异步同步类型，数据端点
    0xFFU, 0x03U,//wMaxPacketSize：1023字节，小端存放
    0x01U//bInterval：全速模式每1个USB帧提供一次传输机会
},
    0U
};


//编译期核对有效长度和PIO补齐空间
typedef char UvcConfigLengthCheck[
    sizeof(s_uvc_config_storage.descriptor) == USB_UVC_FS_CONFIG_DESC_SIZE ? 1 : -1];

typedef char UvcConfigPaddingCheck[
    sizeof(s_uvc_config_storage) == 184U ? 1 : -1];

static bool s_video_descriptor_configured;

//按小端写入双字节字段
static void UvcDesc_Write16(uint8_t *p, uint16_t value);

//按小端写入四字节字段
static void UvcDesc_Write32(uint8_t *p, uint32_t value);

SysResult_t USB_UVC_ValidateFSConfig(const VideoStreamConfig_t *config)
{
    uint32_t bytes;
    uint32_t packets;
    SysResult_t result;

    if (config == NULL || config->fps == 0U)
        return SYS_ERR_INVALID_ARG;
    result = CameraConfig_GetFrameSize(&config->camera, &bytes);
    if (result != SYS_OK)
        return result;
    if (config->camera.format != CAMERA_PIXEL_FORMAT_YUV422 || config->fps > 1000U)
        return SYS_ERR_NOT_SUPPORTED;
    if (bytes > UINT32_MAX / 8U / config->fps)
        return SYS_ERR_INVALID_ARG;

    //每SOF最多一包，2字节UVC头也占用空间
    packets = (bytes - 1U) / (USB_UVC_VIDEO_PACKET_SIZE - 2U) + 1U;
    if (packets > 1000U / config->fps)
        return SYS_ERR_NOT_SUPPORTED;
    return SYS_OK;
}

SysResult_t USB_UVC_ConfigureFSDescriptor(const VideoStreamConfig_t *config)
{
    uint32_t bytes;
    uint32_t interval;
    uint32_t rate;
    uint16_t offset;
    uint8_t interface_number = 0U;
    uint8_t *data = s_uvc_config_storage.descriptor;
    SysResult_t result = USB_UVC_ValidateFSConfig(config);

    if (result != SYS_OK)
        return result;
    if (s_video_descriptor_configured)
        return SYS_ERR_INVALID_STATE;
    (void)CameraConfig_GetFrameSize(&config->camera, &bytes);
    interval = 10000000UL / config->fps;
    rate = bytes * 8U * config->fps;

    //按边界查找VS Frame，不依赖整个数组的固定偏移
    for (offset = 0U; offset + 2U <= USB_UVC_FS_CONFIG_DESC_SIZE; offset += data[offset])
    {
        uint8_t length = data[offset];
        uint8_t *entry = data + offset;
        if (length < 2U || offset + length > USB_UVC_FS_CONFIG_DESC_SIZE)
            return SYS_ERR_PROTOCOL;
        if (entry[1] == 0x04U && length >= 9U)
            interface_number = entry[2];
        if (interface_number == 1U && entry[1] == 0x24U && length == 30U && entry[2] == 0x05U)
        {
            UvcDesc_Write16(entry + 5U, config->camera.width);
            UvcDesc_Write16(entry + 7U, config->camera.height);
            UvcDesc_Write32(entry + 9U, rate);
            UvcDesc_Write32(entry + 13U, rate);
            UvcDesc_Write32(entry + 17U, bytes);
            UvcDesc_Write32(entry + 21U, interval);
            UvcDesc_Write32(entry + 26U, interval);
            s_video_descriptor_configured = true;
            return SYS_OK;
        }
    }
    return SYS_ERR_PROTOCOL;
}

//提供Class配置描述符，返回指针不转移存储所有权
uint8_t *USB_UVC_GetFSConfigDescriptor(uint16_t *length)
{
    if (length == NULL)
        return NULL;
    *length = 0U;
    if (!s_video_descriptor_configured)
        return NULL;

    *length = sizeof(s_uvc_config_storage.descriptor);
    return s_uvc_config_storage.descriptor;
}

//提供UVC设备描述符，TEST描述符单独维护
uint8_t *USB_UVC_GetDeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    if (length == NULL)
        return NULL;

    *length = 0U;
    if (speed != USBD_SPEED_FULL)
        return NULL;

    *length = USB_UVC_DEVICE_DESC_SIZE;
    return s_uvc_device_desc;
}

//返回静态语言描述符
uint8_t *USB_UVC_GetLangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    if (length == NULL)
        return NULL;

    *length = 0U;
    if (speed != USBD_SPEED_FULL)
        return NULL;

    *length = USB_UVC_LANGID_DESC_SIZE;
    return s_uvc_langid_desc;
}

//返回静态功能名称，有效数据不包含PIO补齐空间
uint8_t *USB_UVC_GetInterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    if (length == NULL)
        return NULL;

    *length = 0U;
    if (speed != USBD_SPEED_FULL)
        return NULL;

    *length = USB_UVC_FUNCTION_STR_DESC_SIZE;
    return s_uvc_function_desc;
}

//内部功能函数实现
static void UvcDesc_Write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void UvcDesc_Write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
