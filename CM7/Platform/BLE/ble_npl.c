#include "nimble/nimble_npl.h"
#include "stm32h7xx.h"
#include <string.h>
#include <assert.h>
static struct ble_npl_callout *s_callouts;
uint32_t ble_npl_hw_enter_critical(void){uint32_t p=__get_PRIMASK();__disable_irq();return p;}
void ble_npl_hw_exit_critical(uint32_t p){__DMB();__set_PRIMASK(p);}
bool ble_npl_hw_is_in_critical(void){return __get_PRIMASK()!=0U;}
bool ble_npl_os_started(void){return xTaskGetSchedulerState()==taskSCHEDULER_RUNNING;}
void *ble_npl_get_current_task_id(void){return xTaskGetCurrentTaskHandle();}
void ble_npl_eventq_init(struct ble_npl_eventq *q){memset(q,0,sizeof(*q));q->wake=xQueueCreateStatic(1U,1U,&q->byte,&q->storage);assert(q->wake);}
void ble_npl_event_init(struct ble_npl_event *e,ble_npl_event_fn *fn,void *arg){memset(e,0,sizeof(*e));e->fn=fn;e->arg=arg;}
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *e)
{
    uint8_t token=1U;uint32_t p=ble_npl_hw_enter_critical();
    if(!e->queued){e->queued=true;e->next=NULL;if(q->tail)q->tail->next=e;else q->head=e;q->tail=e;}
    ble_npl_hw_exit_critical(p);(void)xQueueSend(q->wake,&token,0U);
}
struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *q,ble_npl_time_t tmo)
{
    struct ble_npl_event *e;uint32_t p,start=xTaskGetTickCount(),remaining;uint8_t token;
    for(;;)
    {
        p=ble_npl_hw_enter_critical();e=q->head;
        if(e){q->head=e->next;if(!q->head)q->tail=NULL;e->next=NULL;e->queued=false;}
        ble_npl_hw_exit_critical(p);if(e)return e;
        remaining=tmo;
        if(tmo!=BLE_NPL_TIME_FOREVER){uint32_t elapsed=xTaskGetTickCount()-start;if(elapsed>=tmo)return NULL;remaining=tmo-elapsed;}
        if(xQueueReceive(q->wake,&token,remaining)!=pdPASS)return NULL;
    }
}
void ble_npl_eventq_remove(struct ble_npl_eventq *q,struct ble_npl_event *e)
{
    struct ble_npl_event *v,*prev=NULL;uint32_t p=ble_npl_hw_enter_critical();
    for(v=q->head;v;v=v->next){if(v==e){if(prev)prev->next=v->next;else q->head=v->next;if(q->tail==v)q->tail=prev;v->next=NULL;v->queued=false;break;}prev=v;}
    ble_npl_hw_exit_critical(p);
}
bool ble_npl_eventq_is_empty(struct ble_npl_eventq *q){uint32_t p=ble_npl_hw_enter_critical();bool empty=q->head==NULL;ble_npl_hw_exit_critical(p);return empty;}
void ble_npl_event_run(struct ble_npl_event *e){assert(e&&e->fn);e->fn(e);}
bool ble_npl_event_is_queued(struct ble_npl_event *e){return e->queued;}
void *ble_npl_event_get_arg(struct ble_npl_event *e){return e->arg;}
void ble_npl_event_set_arg(struct ble_npl_event *e,void *arg){e->arg=arg;}
ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *m)
{
    uint8_t token=1U;memset(m,0,sizeof(*m));m->token=xQueueCreateStatic(1U,1U,&m->byte,&m->storage);
    if(!m->token)return BLE_NPL_ENOMEM;(void)xQueueSend(m->token,&token,0U);return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *m,ble_npl_time_t tmo)
{
    uint8_t token;TaskHandle_t me=xTaskGetCurrentTaskHandle();if(__get_IPSR())return BLE_NPL_ERR_IN_ISR;
    if(m->owner==me&&m->depth){m->depth++;return BLE_NPL_OK;}
    if(xQueueReceive(m->token,&token,tmo)!=pdPASS)return BLE_NPL_TIMEOUT;m->owner=me;m->depth=1U;return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *m)
{
    uint8_t token=1U;if(m->owner!=xTaskGetCurrentTaskHandle()||!m->depth)return BLE_NPL_BAD_MUTEX;
    if(--m->depth==0U){m->owner=NULL;if(xQueueSend(m->token,&token,0U)!=pdPASS)return BLE_NPL_ERROR;}return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *s,uint16_t count)
{
    uint8_t token=1U;uint32_t i;if(count>128U)return BLE_NPL_EINVAL;
    s->tokens=xQueueCreateStatic(128U,1U,s->bytes,&s->storage);if(!s->tokens)return BLE_NPL_ENOMEM;
    for(i=0U;i<count;i++)(void)xQueueSend(s->tokens,&token,0U);return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *s,ble_npl_time_t tmo){uint8_t token;return xQueueReceive(s->tokens,&token,tmo)==pdPASS?BLE_NPL_OK:BLE_NPL_TIMEOUT;}
ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *s){uint8_t token=1U;return xQueueSend(s->tokens,&token,0U)==pdPASS?BLE_NPL_OK:BLE_NPL_ENOMEM;}
uint16_t ble_npl_sem_get_count(struct ble_npl_sem *s){return (uint16_t)uxQueueMessagesWaiting(s->tokens);}
void ble_npl_callout_init(struct ble_npl_callout *c,struct ble_npl_eventq *q,ble_npl_event_fn *fn,void *arg)
{
    struct ble_npl_callout *v;uint32_t p=ble_npl_hw_enter_critical();bool found=false;
    for(v=s_callouts;v;v=v->next)if(v==c){found=true;break;}
    if(!found){c->next=s_callouts;s_callouts=c;}c->active=false;c->queue=q;ble_npl_event_init(&c->event,fn,arg);ble_npl_hw_exit_critical(p);
}
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *c,ble_npl_time_t ticks)
{
    uint32_t p;if(ticks>INT32_MAX)return BLE_NPL_EINVAL;
    p=ble_npl_hw_enter_critical();c->expiry=xTaskGetTickCount()+(ticks?ticks:1U);c->active=true;ble_npl_hw_exit_critical(p);return BLE_NPL_OK;
}
void ble_npl_callout_stop(struct ble_npl_callout *c){uint32_t p=ble_npl_hw_enter_critical();c->active=false;if(c->queue)ble_npl_eventq_remove(c->queue,&c->event);ble_npl_hw_exit_critical(p);}
bool ble_npl_callout_is_active(struct ble_npl_callout *c){return c->active;}
ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *c){return c->expiry;}
ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *c,ble_npl_time_t now){return c->active&&(int32_t)(c->expiry-now)>0?c->expiry-now:0U;}
void ble_npl_callout_set_arg(struct ble_npl_callout *c,void *arg){c->event.arg=arg;}
void BleNpl_PollCallouts(void)
{
    struct ble_npl_callout *c;uint32_t p=ble_npl_hw_enter_critical(),now=xTaskGetTickCount();
    for(c=s_callouts;c;c=c->next)if(c->active&&(int32_t)(now-c->expiry)>=0){c->active=false;if(c->queue)ble_npl_eventq_put(c->queue,&c->event);}
    ble_npl_hw_exit_critical(p);
}
uint32_t ble_npl_time_get(void){return xTaskGetTickCount();}
ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms,ble_npl_time_t *out){if(!out)return BLE_NPL_EINVAL;*out=ms;return BLE_NPL_OK;}
ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t t,uint32_t *out){if(!out)return BLE_NPL_EINVAL;*out=t;return BLE_NPL_OK;}
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms){return ms;}
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t t){return t;}
void ble_npl_time_delay(ble_npl_time_t t){vTaskDelay(t);}

//仅记录静态格式串和次数，不在实时路径printf；参数格式化留给外部诊断
volatile uint32_t g_ble_npl_log_count;
const char * volatile g_ble_npl_last_log;
void BleNpl_Log(const char *format){g_ble_npl_last_log=format;g_ble_npl_log_count++;}
