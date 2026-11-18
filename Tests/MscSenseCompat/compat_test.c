//真实Core/Platform/BOT集成，复用现有HAL包级桩；覆盖捕获到的CDB补零
#define Protocol_Run SenseCompat_PreviousRun
#include "../MscProtocol/protocol_test.c"
#undef Protocol_Run
static void SenseCompat_Send(uint8_t opcode,uint8_t length,uint8_t allocation,uint32_t host,uint8_t bad)
{
    uint8_t cbw[31];memset(cbw,0,sizeof(cbw));MscBot_Write32(cbw,0x43425355);MscBot_Write32(cbw+4,0xABCDEF01);
    MscBot_Write32(cbw+8,host);cbw[12]=host?0x80:0;cbw[14]=length;cbw[15]=opcode;
    if(opcode==0x23)cbw[23]=252;else cbw[19]=allocation;
    if(bad)cbw[15+bad]=1;
    Protocol_Out(cbw,31);Protocol_Tick();
}
static int SenseCompat_Csw(uint8_t status,uint32_t residue)
{
    Protocol_Tick();
    if(s_endpoints[1][2].stalled){Request(2,1,0,0x82,0);Protocol_Tick();}
    EQ(s_bot.state,BOT_CSW);EQ(s_bot.csw[12],status);EQ(MscBot_Read32(s_bot.csw+8),residue);
    EQ(MscBot_Read32(s_bot.csw+4),0xABCDEF01);return 0;
}
int SenseCompat_Run(uint32_t scenario)
{
    uint8_t length=12,allocation=18,bad=0;uint32_t host=18,actual;bool valid=true;
    if(Protocol_Init())return 1;
    //复现Windows：0x23失败，随后取出原来的ILLEGAL REQUEST/20/00
    SenseCompat_Send(0x23,10,0,252,0);if(SenseCompat_Csw(1,252))return 1;
    Protocol_In();Protocol_Tick();EQ(s_bot.state,BOT_CBW);EQ(s_bot.scsi.sense_key,5);EQ(s_bot.scsi.asc,0x20);
    switch(scenario)
    {
        case 0:length=6;break;
        case 1:break;
        case 2:length=16;break;
        case 3:bad=6;valid=false;break;
        case 4:length=5;valid=false;break;
        case 5:length=17;valid=false;break;
        case 6:allocation=8;host=8;break;
        case 7:allocation=8;break;
        case 8:allocation=0;host=0;break;
        case 9:allocation=255;break;
        case 10:bad=1;valid=false;break;
        case 11:bad=2;valid=false;break;
        case 12:bad=3;valid=false;break;
        case 13:bad=5;valid=false;break;
        case 14:bad=11;valid=false;break;
        case 15:length=16;bad=15;valid=false;break;
        case 16:break; //CSW发送失败不得清Sense
        case 17:valid=false;break; //其它命令仍要求原有长度
    }
    SenseCompat_Send(scenario==17?0x12:3,length,allocation,host,bad);
    if(scenario==5){EQ(s_bot.state,BOT_FAULT);EQ(s_endpoints[1][2].stalled,true);EQ(s_endpoints[0][2].stalled,true);EQ(s_bot.scsi.asc,0x20);return 0;}
    if(!valid)
    {
        EQ(s_bot.state,BOT_FINISH);if(SenseCompat_Csw(1,host))return 1;
        EQ(s_bot.scsi.asc,0x24);EQ(s_bot.transferred,0);return 0;
    }
    actual=allocation<18?allocation:18;
    if(actual)
    {
        EQ(s_bot.state,BOT_IN);EQ(tx_size,actual);EQ(s_bot.chunk,actual);
        EQ(tx_data[0],0x70);EQ(tx_data[2],5);if(actual>7)EQ(tx_data[7],0x0A);
        if(actual>13){EQ(tx_data[12],0x20);EQ(tx_data[13],0);}
        EQ(s_bot.scsi.sense_key,5);Protocol_In();EQ(s_bot.scsi.sense_key,5);Protocol_Tick();
    }
    if(scenario==16){fault=9;Protocol_Tick();EQ(s_bot.state,BOT_FAULT);EQ(s_bot.scsi.sense_key,5);EQ(s_bot.scsi.asc,0x20);return 0;}
    if(SenseCompat_Csw(0,host-actual))return 1;
    EQ(s_bot.scsi.sense_key,5);Protocol_In();EQ(s_bot.scsi.sense_key,5);Protocol_Tick();
    EQ(s_bot.state,BOT_CBW);EQ(s_bot.scsi.sense_key,0);EQ(s_bot.scsi.asc,0);return 0;
}
