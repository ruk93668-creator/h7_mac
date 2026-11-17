//协议集成：真实Core/Platform/BSP/MSC，HAL模拟总线包，不代替实板
#include "../MscEnumeration/hal_fixture.c"
#include "../../CM7/Service/usb_msc_bot.c"
#include "usb_msc_class.h"
static uint32_t s_now;
uint32_t Platform_TimeGetMs(void) { return s_now; }
HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *device, uint8_t endpoint)
{ (void)device; (void)endpoint; return fault == 14U ? HAL_ERROR : HAL_OK; }
static void Protocol_Tick(void) { USB_MSC_Process(++s_now); }
static void Protocol_Out(const uint8_t *data, uint32_t length)
{
    memcpy(s_pcd.OUT_ep[0].xfer_buff, data, length);
    rx_size = length;
    HAL_PCD_DataOutStageCallback(&s_pcd, 2U);
}
static void Protocol_In(void)
{
    uint32_t i;
    for (i = 0; i < 32U && s_tx_transfer[2].active; ++i)
        HAL_PCD_DataInStageCallback(&s_pcd, 2U);
}
static void Protocol_Cbw(uint8_t opcode, uint32_t host, bool in, uint32_t blocks)
{
    uint8_t data[31];
    memset(data, 0, sizeof(data));
    MscBot_Write32(data, 0x43425355U); MscBot_Write32(data + 4U, 0x12345678U);
    MscBot_Write32(data + 8U, host); data[12] = in ? 0x80U : 0U;
    data[14] = opcode == 0x28U || opcode == 0x2AU || opcode == 0x25U || opcode == 0x35U ? 10U : 6U;
    data[15] = opcode;
    if (opcode == 0x12U || opcode == 3U || opcode == 0x1AU) data[19] = (uint8_t)blocks;
    else { data[22] = (uint8_t)(blocks >> 8); data[23] = (uint8_t)blocks; }
    Protocol_Out(data, 31U); Protocol_Tick();
}
static int Protocol_Init(void)
{
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48); RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK); EQ(USB_MSC_Register(), SYS_OK); EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd); Configured();
    EQ(USB_Device_IsConfigured(), true); Protocol_Tick(); EQ(s_bot.state, BOT_CBW);
    s_bot.scsi.unit_attention = false;
    s_bot.generation = 1U;
    return 0;
}
int Protocol_Run(uint32_t scenario)
{
    uint32_t i;
    uint8_t data[1024], cdb[16];
    MscScsiMedia_t media = {16U, true, true, false};
    MscScsiContext_t context = {0};
    MscScsiCommand_t command;
    MscBotCase_t matrix;
    if (scenario < 13U)
    {
        const uint32_t host[13] = {0,0,0,16,64,36,16,64,64,64,1024,512,256};
        const uint32_t dev[13] = {0,36,512,0,36,36,36,512,0,36,512,512,512};
        const bool hi[13] = {false,false,false,true,true,true,true,true,false,false,false,false,false};
        const bool di[13] = {false,true,false,false,true,true,true,false,false,true,false,false,false};
        const uint8_t status[13] = {0,2,2,0,0,0,2,2,0,2,0,0,2};
        matrix = USB_MSC_BotCase(host[scenario], hi[scenario], dev[scenario], di[scenario]);
        EQ(matrix.number, scenario + 1U); EQ(matrix.status, status[scenario]);
        if (Protocol_Init()) return 1;
        Protocol_Cbw(dev[scenario] == 0U ? 0U : (di[scenario] ? 0x12U : 0x2AU), host[scenario], hi[scenario], di[scenario] ? 36U : 1U);
        if (s_bot.state == BOT_IN) { Protocol_In(); Protocol_Tick(); }
        if (s_bot.state == BOT_OUT)
        {
            memset(data, 0xA5, sizeof(data)); Protocol_Out(data, s_bot.chunk); Protocol_Tick(); Protocol_Tick();
        }
        for (i = 0; i < 4U && s_bot.state != BOT_CSW; ++i)
        {
            if (s_endpoints[1][2].stalled) Request(2U, 1U, 0U, 0x82U, 0U);
            Protocol_Tick();
        }
        EQ(s_bot.state, BOT_CSW); EQ(s_bot.csw[12], status[scenario]);
        EQ(MscBot_Read32(s_bot.csw), 0x53425355U); EQ(MscBot_Read32(s_bot.csw + 4U), 0x12345678U);
        EQ(MscBot_Read32(s_bot.csw + 8U), host[scenario] - s_bot.transferred);
        EQ(s_endpoints[0][2].busy, false); Protocol_In();
        if (s_endpoints[0][2].stalled) Request(2U, 1U, 0U, 2U, 0U);
        Protocol_Tick(); EQ(s_bot.state, status[scenario] == 2U ? BOT_FAULT : BOT_CBW);
        return 0;
    }
    if (scenario >= 20U)
    {
        memset(cdb, 0, sizeof(cdb));
        switch (scenario)
        {
            case 20: cdb[0]=0x12; cdb[4]=255; EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),true); EQ(command.length,36); EQ(command.response[1],0x80); break;
            case 21: cdb[0]=0x25; EQ(USB_MSC_ScsiDecode(&context,&media,cdb,10,&command),true); EQ(command.response[3],15); EQ(command.response[6],2); break;
            case 22: cdb[0]=0x28;cdb[5]=15;cdb[8]=2; EQ(USB_MSC_ScsiDecode(&context,&media,cdb,10,&command),false);EQ(context.asc,0x21);break;
            case 23: cdb[0]=0x2A;cdb[8]=1;media.write_protected=true;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,10,&command),false);EQ(context.sense_key,7);break;
            case 24: cdb[0]=0x28;memset(cdb+2,255,4);EQ(USB_MSC_ScsiDecode(&context,&media,cdb,10,&command),true);EQ(command.length,0);break;
            case 25: cdb[0]=0xFF;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),false);EQ(context.asc,0x20);cdb[0]=3;cdb[4]=18;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),true);EQ(command.response[12],0x20);break;
            case 26: context.unit_attention=true;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),false);EQ(context.sense_key,6);EQ(context.unit_attention,false);break;
            case 27: media.present=false;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),false);EQ(context.asc,0x3A);break;
            case 28: cdb[0]=0x1A;cdb[2]=8;cdb[4]=255;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),true);EQ(command.length,24);EQ(command.response[6]&4,0);break;
            case 29: cdb[0]=0x1E;cdb[4]=1;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),true);cdb[0]=0x1B;cdb[4]=2;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),false);EQ(context.asc,0x53);break;
            case 30: cdb[0]=0x35;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,10,&command),true);EQ(command.operation,MSC_SCSI_SYNC);cdb[1]=2;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,10,&command),false);break;
            case 31: cdb[0]=0x28;cdb[8]=1;EQ(USB_MSC_ScsiDecode(&context,&media,cdb,6,&command),false);EQ(context.asc,0x24);break;
        }
        return 0;
    }
    if (Protocol_Init()) return 1;
    if (scenario == 13U || scenario == 14U)
    {
        memset(data,0,64);if(scenario==14U)MscBot_Write32(data,0x43425355U);
        Protocol_Out(data,scenario==13U?30U:31U);Protocol_Tick();EQ(s_bot.state,BOT_FAULT);EQ(s_endpoints[1][2].stalled,true);return 0;
    }
    if (scenario == 15U)
    {
        Protocol_Cbw(0x2A,1024,false,2);EQ(s_bot.state,BOT_OUT);
        for(i=0;i<1024;i++)data[i]=(uint8_t)(i*7U);
        Protocol_Out(data,1024);Protocol_Tick();Protocol_Tick();Protocol_Tick();EQ(s_bot.state,BOT_CSW);Protocol_In();Protocol_Tick();
        Protocol_Cbw(0x28,1024,true,2);Protocol_Tick();EQ(s_bot.state,BOT_IN);
        EQ(memcmp(data,s_bot.data,1024),0);Protocol_In();Protocol_Tick();Protocol_Tick();EQ(s_bot.state,BOT_CSW);EQ(s_bot.csw[12],0);return 0;
    }
    Request(0x21,0xFF,0,0,0);EQ(s_bot.state,BOT_RESET_DRAIN);EQ(s_endpoints[1][0].busy,false);
    if(scenario==16U)
    {
        Protocol_Tick();EQ(s_bot.state,BOT_RESET_STATUS);EQ(s_endpoints[1][0].busy,true);
        HAL_PCD_DataInStageCallback(&s_pcd,0); //不调用Task，立刻连续两个Clear HALT
        Request(2,1,0,0x82,0);Request(2,1,0,2,0);Protocol_Tick();EQ(s_bot.state,BOT_CBW);return 0;
    }
    if(scenario==17U)
    {
        Request(0x80,6,0x100,0,18);Protocol_Tick();EQ(s_bot.state,BOT_FAULT);return 0;
    }
    if(scenario==18U)
    {
        fault=14;Protocol_Tick();EQ(s_bot.state,BOT_RESET_DRAIN);s_now+=2001;Protocol_Tick();EQ(s_bot.state,BOT_FAULT);EQ(s_endpoints[1][0].stalled,true);return 0;
    }
    HAL_PCD_ResetCallback(&s_pcd);EQ(s_bot.active,false);Configured();Protocol_Tick();EQ(s_bot.state,BOT_CBW);return 0;
}
void __aeabi_memset(void *data, size_t length, int value) { volatile uint8_t *p=data; while(length--)*p++=(uint8_t)value; }

uint32_t Platform_SyncGetException(void) { return __get_IPSR(); }
