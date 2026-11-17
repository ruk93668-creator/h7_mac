#include "../../CM7/Service/usb_msc_storage.c"
#include <string.h>
volatile struct {uint32_t fail_line,checks;} g_result;
static uint32_t s_exception,s_mask,s_calls,s_case,s_gen=1U;
static bool s_quiet=true,s_wp;
static uint8_t s_data[1024];
static MscBlockRequest_t s_test_request;
static int Eq(uint32_t line,uint32_t a,uint32_t b){g_result.checks++;if(a!=b){g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Eq(__LINE__,(uint32_t)(a),(uint32_t)(b)))return 1;}while(0)
void *memset(void *p,int v,size_t n){volatile uint8_t *d=p;while(n--)*d++=(uint8_t)v;return p;}
void __aeabi_memclr(void *p,size_t n){memset(p,0,n);}
void __aeabi_memclr4(void *p,size_t n){volatile uint8_t *d=p;while(n--)*d++=0;}
void __aeabi_memclr8(void *p,size_t n){volatile uint8_t *d=p;while(n--)*d++=0;}
void __aeabi_memcpy4(void *p,const void *q,size_t n){volatile uint8_t *d=p;const uint8_t *s=q;while(n--)*d++=*s++;}
uint32_t Platform_SyncGetException(void){return s_exception;}
uint32_t Platform_SyncGetPrimask(void){return s_mask;}
void Platform_SyncDisableIrq(void){s_mask=1;}
void Platform_SyncRestoreIrq(uint32_t mask){s_mask=mask;}
void Platform_SyncMemoryBarrier(void){}
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target,uint32_t events){(void)target;(void)events;return s_case==14U?SYS_ERR_BUSY:SYS_OK;}
SysResult_t RuntimeEvent_PostFromIsr(RuntimeEventTarget_t target,uint32_t events){return RuntimeEvent_Post(target,events);}
static SysResult_t Adapter_Init(void){return s_case==13U?SYS_ERR_IO:SYS_OK;}
static void Adapter_Media(MscScsiMedia_t *m,uint32_t *g){m->present=true;m->ready=true;m->write_protected=s_wp;m->block_count=100;*g=s_gen;}
static void Adapter_Execute(const MscBlockRequest_t *r,MscBlockCompletion_t *c)
{
    s_calls++;c->result=s_case==6U?SYS_ERR_IO:SYS_OK;c->completed_blocks=s_case==6U?1U:r->blocks;
    if(s_case==4U){s_quiet=false;MscStorage_Cancel(r->epoch);}
}
static bool Adapter_Quiet(void){return s_quiet;}
static const MscStorageDeviceOps_t s_device={Adapter_Init,Adapter_Media,Adapter_Execute,Adapter_Quiet};
int Adapter_Run(uint32_t scenario)
{
    const MscBackendOps_t *ops=USB_MSC_StorageBackend();MscBlockCompletion_t done;MscBlockRequest_t other;
    s_case=scenario;
    s_test_request.epoch=1;s_test_request.token=1;s_test_request.generation=1;s_test_request.operation=MSC_SCSI_READ;
    s_test_request.blocks=2;s_test_request.buffer=s_data;
    if(scenario==0U){EQ(ops->submit(&s_test_request),SYS_ERR_INVALID_STATE);return 0;}
    if(scenario==13U){EQ(USB_MSC_StorageInit(&s_device),SYS_ERR_IO);EQ(ops->submit(&s_test_request),SYS_ERR_INVALID_STATE);EQ(USB_MSC_StorageInit(&s_device),SYS_ERR_INVALID_STATE);return 0;}
    EQ(USB_MSC_StorageInit(&s_device),SYS_OK);
    if(scenario==8U){s_exception=16;EQ(ops->submit(&s_test_request),SYS_ERR_INVALID_STATE);return 0;}
    if(scenario==9U){s_wp=true;USB_MSC_StorageProcess();s_test_request.operation=MSC_SCSI_WRITE;EQ(ops->submit(&s_test_request),SYS_ERR_NOT_SUPPORTED);return 0;}
    if(scenario==10U){s_test_request.blocks=3;EQ(ops->submit(&s_test_request),SYS_ERR_INVALID_ARG);return 0;}
    if(scenario==11U){s_test_request.operation=MSC_SCSI_RESPONSE;EQ(ops->submit(&s_test_request),SYS_ERR_INVALID_ARG);return 0;}
    if(scenario==12U){EQ(ops->submit(NULL),SYS_ERR_INVALID_ARG);s_test_request.lba=0xFFFFFFFFU;EQ(ops->submit(&s_test_request),SYS_ERR_INVALID_ARG);return 0;}
    EQ(ops->submit(&s_test_request),SYS_OK);EQ(ops->drained(1),false);EQ(s_calls,0);
    EQ(ops->submit(&s_test_request),SYS_ERR_BUSY);EQ(ops->poll(&s_test_request,&done),SYS_ERR_BUSY);
    if(scenario==3U||scenario==16U)
    {
        ops->cancel(1);EQ(ops->drained(1),true);USB_MSC_StorageProcess();EQ(s_calls,0);
        if(scenario==16U){s_test_request.epoch=2;s_test_request.token=2;EQ(ops->submit(&s_test_request),SYS_OK);ops->cancel(1);EQ(ops->drained(2),false);USB_MSC_StorageProcess();EQ(ops->poll(&s_test_request,&done),SYS_OK);}
        return 0;
    }
    if(scenario==7U)s_gen=2;
    if(scenario==17U){s_exception=16;USB_MSC_StorageProcess();EQ(s_calls,0);s_exception=0;}
    if(scenario==15U)ops->cancel(9);
    USB_MSC_StorageProcess();
    if(scenario==4U)
    {
        EQ(s_storage.state,MSC_MAIL_DRAIN);EQ(ops->drained(1),false);EQ(ops->submit(&s_test_request),SYS_ERR_BUSY);
        EQ(ops->poll(&s_test_request,&done),SYS_ERR_BUSY);s_quiet=true;USB_MSC_StorageProcess();
        EQ(ops->drained(1),true);EQ(ops->poll(&s_test_request,&done),SYS_ERR_INVALID_STATE);return 0;
    }
    if(scenario==5U)
    {
        other=s_test_request;other.epoch++;EQ(ops->poll(&other,&done),SYS_ERR_INVALID_STATE);
        other=s_test_request;other.token++;EQ(ops->poll(&other,&done),SYS_ERR_INVALID_STATE);
        other=s_test_request;other.generation++;EQ(ops->poll(&other,&done),SYS_ERR_INVALID_STATE);
    }
    EQ(ops->drained(1),true);EQ(ops->poll(&s_test_request,&done),SYS_OK);
    EQ(done.result,scenario==6U?SYS_ERR_IO:(scenario==7U?SYS_ERR_INVALID_STATE:SYS_OK));
    EQ(done.completed_blocks,scenario==6U?1U:(scenario==7U?0U:2U));
    EQ(s_calls,scenario==7U?0U:1U);EQ(s_storage.state,MSC_MAIL_EMPTY);return 0;
}
const uint32_t g_fixture_init __attribute__((section(".framework_init"))) = 0;
