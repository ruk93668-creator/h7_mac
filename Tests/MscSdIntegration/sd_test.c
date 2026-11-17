#include "../../CM7/Service/usb_msc_sd.c"
volatile struct {uint32_t fail_line,checks;} g_result;
static uint32_t s_case,s_calls,s_recovers,s_claims;
static bool s_present=true,s_ready=true;
static BspSdmmcIoDiagnostics_t s_io_mock;
static uint8_t s_test_data[1024];
static int Eq(uint32_t line,uint32_t a,uint32_t b){g_result.checks++;if(a!=b){g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Eq(__LINE__,(uint32_t)(a),(uint32_t)(b)))return 1;}while(0)
void *memset(void *p,int v,size_t n){volatile uint8_t*d=p;while(n--)*d++=(uint8_t)v;return p;}
void __aeabi_memclr4(void *p,size_t n){volatile uint8_t*d=p;while(n--)*d++=0;}
void __aeabi_memclr8(void *p,size_t n){volatile uint8_t*d=p;while(n--)*d++=0;}
void __aeabi_memcpy4(void*p,const void*q,size_t n){volatile uint8_t*d=p;const uint8_t*s=q;while(n--)*d++=*s++;}
int memcmp(const void *p,const void *q,size_t n){const uint8_t*a=p,*b=q;while(n--){if(*a!=*b)return *a-*b;a++;b++;}return 0;}
SysResult_t SD_Block_Claim(SdBlockClient_t client){s_claims++;return client!=SD_BLOCK_CLIENT_RAW||s_case==2?SYS_ERR_BUSY:SYS_OK;}
SysResult_t SD_Block_Init(void){return s_case==19?SYS_ERR_IO:SYS_OK;}
SysResult_t SD_Block_GetInfo(BspSdmmcCardInfo_t *info){info->logical_block_size=s_case==1?1024:512;info->logical_block_count=100;return SYS_OK;}
SysResult_t SD_Block_GetIdentity(uint32_t cid[4]){uint32_t i;for(i=0;i<4;i++)cid[i]=i+(s_case==13&&s_calls?1:0);return SYS_OK;}
SysResult_t SD_Block_GetStatus(SdBlockStatus_t *s){s->present=s_present;s->initialized=s_ready;s->busy=false;s->fault=false;return SYS_OK;}
SysResult_t SD_Block_GetIoDiagnostics(BspSdmmcIoDiagnostics_t *io){*io=s_io_mock;return SYS_OK;}
SysResult_t SD_Block_Recover(void){s_recovers++;if(s_case==11)return SYS_ERR_IO;s_io_mock.active=0;s_ready=false;return SYS_OK;}
SysResult_t SD_Block_Sync(void){s_calls++;return s_case==20?SYS_ERR_IO:SYS_OK;}
static SysResult_t SdTest_IO(uint32_t lba,uint32_t count,bool write)
{
    s_calls++;s_io_mock.lba=lba;s_io_mock.requested_blocks=count;s_io_mock.write=write;s_io_mock.completed_blocks=count;
    s_io_mock.result=SYS_OK;
    if(s_case==9||s_case==10||s_case==11||s_case==15||s_case==16){s_io_mock.result=SYS_ERR_IO;s_io_mock.completed_blocks=1;}
    if(s_case==16)s_io_mock.result=SYS_OK;
    if(s_case==10||s_case==11)s_io_mock.active=1;
    return s_case==9||s_case==10||s_case==11||s_case==15||s_case==16?SYS_ERR_IO:SYS_OK;
}
SysResult_t SD_Block_Read(uint32_t lba,uint32_t count,void *buffer,uint32_t ms){(void)buffer;(void)ms;return SdTest_IO(lba,count,false);}
SysResult_t SD_Block_Write(uint32_t lba,uint32_t count,const void *buffer,uint32_t ms){(void)buffer;(void)ms;return SdTest_IO(lba,count,true);}
int SdTest_Run(uint32_t scenario)
{
    MscScsiMedia_t media;uint32_t generation;
    MscBlockRequest_t request={1,1,1,MSC_SCSI_READ,0,2,s_test_data};MscBlockCompletion_t done;
    const MscStorageDeviceOps_t *device=USB_MSC_SdDevice();s_case=scenario;
    if(scenario==1||scenario==2||scenario==19){EQ(device->init(),scenario==1?SYS_ERR_NOT_SUPPORTED:(scenario==2?SYS_ERR_BUSY:SYS_ERR_IO));return 0;}
    EQ(device->init(),SYS_OK);device->media(&media,&generation);EQ(media.ready,true);EQ(media.block_count,100);EQ(generation,1);
    if(scenario==0){EQ(media.write_protected,!USB_MSC_SD_WRITE_ENABLE);return 0;}
    if(scenario==4)request.lba=0xFFFFFFFFU;
    if(scenario==5){request.lba=99;request.blocks=1;}
    if(scenario==6||scenario==14||scenario==15)request.operation=MSC_SCSI_WRITE;
    if(scenario==7)request.operation=MSC_SCSI_SYNC;
    if(scenario==8)request.operation=MSC_SCSI_EJECT;
    if(scenario==17)request.generation=2;
    if(scenario==20)request.operation=MSC_SCSI_SYNC;
    if(scenario==21)request.blocks=0;
    if(scenario==22){request.lba=99;request.blocks=2;}
    if(scenario==23)request.buffer=NULL;
    if(scenario==12)
    {
        s_present=false;device->media(&media,&generation);EQ(media.ready,false);EQ(generation,2);s_present=true;device->media(&media,&generation);EQ(media.ready,false);return 0;
    }
    if(scenario==13){s_calls=1;device->media(&media,&generation);EQ(media.ready,false);return 0;}
    device->execute(&request,&done);
    if(scenario==4||scenario==21||scenario==22||scenario==23){EQ(done.result,SYS_ERR_INVALID_ARG);EQ(s_calls,0);return 0;}
    if(scenario==17){EQ(done.result,SYS_ERR_INVALID_STATE);EQ(s_calls,0);return 0;}
    if((scenario==6||scenario==14||scenario==15)&&!USB_MSC_SD_WRITE_ENABLE){EQ(done.result,SYS_ERR_NOT_SUPPORTED);EQ(s_calls,0);return 0;}
    if(scenario==9||scenario==10||scenario==11||scenario==15||scenario==16||scenario==20)
    {
        EQ(done.result,SYS_ERR_IO);EQ(done.completed_blocks,scenario==16||scenario==20?0:1);
        if(scenario==10||scenario==11)
        {
            EQ(device->quiescent(),scenario==10);EQ(s_recovers,1);EQ(device->quiescent(),scenario==10);EQ(s_recovers,1);
        }
        device->media(&media,&generation);EQ(media.ready,false);EQ(s_claims,1);return 0;
    }
    EQ(done.result,SYS_OK);EQ(done.completed_blocks,scenario==7||scenario==8?0:request.blocks);EQ(s_calls,1);EQ(device->quiescent(),true);return 0;
}
const uint32_t g_fixture_init __attribute__((section(".framework_init"))) = 0;
