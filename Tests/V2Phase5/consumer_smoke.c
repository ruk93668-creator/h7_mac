#include "../../CM7/ApplicationV2/Modules/USB/usb_consumer.h"
#include <string.h>
uint32_t g_failure;
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static VUSBConsumer c;
static AppLifecycleMessage_t sent;
static uint32_t full, ready, flags, sends, wrong;
static int owner(void *p)
{
    (void)p;
    return 1;
}
static SysResult_t send(void *p, const AppLifecycleMessage_t *m)
{
    (void)p;
    if (full)
        return SYS_ERR_NO_RESOURCE;
    sent = *m;
    ready = 1;
    sends++;
    return 0;
}
static SysResult_t recv(void *p, AppLifecycleMessage_t *m)
{
    (void)p;
    if (!ready)
        return SYS_ERR_NOT_FOUND;
    ready = 0;
    if (App_LifecycleMakeReply(&sent, 9, 0, m))
        return SYS_ERR_PROTOCOL;
    if (m->operation == APP_LIFECYCLE_RELEASE)
        m->completion_flags = flags;
    if (wrong)
        m->identity.request_id++;
    return 0;
}
static const VUSBConsumerOps ops = {owner, send, recv};
int USB_Smoke(uint32_t id)
{
    uint32_t i;
    MRGrant g;
    memset(&c, 0, sizeof(c));
    full = ready = flags = sends = wrong = 0;
    g_failure = 0;
    CHECK(VUSBConsumer_Init(&c, &ops, 0) == 0);
    if (id == 0)
    {
        CHECK(VUSBConsumer_Begin(&c) == 0);
        for (i = 0; i < 3; i++)
            VUSBConsumer_Process(&c);
        CHECK(c.phase == VUC_GRANTED && c.grant.session == 9 && c.grant.acquire == 2 &&
              c.grant.generation == 1);
        return 0;
    }
    if (id == 1)
    {
        full = 1;
        VUSBConsumer_Begin(&c);
        for (i = 0; i < 4; i++)
            VUSBConsumer_Process(&c);
        CHECK(c.request.identity.request_id == 1 && c.tx.issued_through == 1 && sends == 0);
        return 0;
    }
    if (id == 2)
    {
        wrong = 1;
        VUSBConsumer_Begin(&c);
        VUSBConsumer_Process(&c);
        CHECK(c.phase == VUC_QUERY && !c.grant.session);
        return 0;
    }
    CHECK(VUSBConsumer_Begin(&c) == 0);
    for (i = 0; i < 3; i++)
        VUSBConsumer_Process(&c);
    CHECK(c.phase == VUC_GRANTED);
    g = c.grant;
    if (id == 3)
    {
        CHECK(VUSBConsumer_Begin(&c) == SYS_ERR_BUSY);
        CHECK(MRGrant_Equal(&g, &c.grant));
        return 0;
    }
    if (id == 4)
    {
        CHECK(VUSBConsumer_End(&c, SYS_ERR_IO) == 0);
        VUSBConsumer_Process(&c);
        CHECK(c.phase == VUC_BLOCKED && MRGrant_Equal(&g, &c.grant));
        return 0;
    }
    if (id == 5)
    {
        flags = APP_LIFECYCLE_QUALIFICATION_RELEASED;
        CHECK(VUSBConsumer_End(&c, SYS_ERR_IO) == 0);
        VUSBConsumer_Process(&c);
        CHECK(!c.grant.session && c.release.request == 3 && c.release.grant.acquire == 2);
        VUSBConsumer_Process(&c);
        CHECK(c.phase == VUC_IDLE);
        return 0;
    }
    if (id == 6)
    {
        full = 1;
        CHECK(VUSBConsumer_End(&c, 0) == 0);
        for (i = 0; i < 4; i++)
            VUSBConsumer_Process(&c);
        CHECK(c.phase == VUC_RELEASE && MRGrant_Equal(&g, &c.grant) && c.release.request == 3 &&
              c.tx.issued_through == 3);
        return 0;
    }
    if (id == 7)
    {
        CHECK(VUSBConsumer_Init(&c, &ops, 0) == SYS_ERR_INVALID_ARG);
        return 0;
    }
    return 2;
}

/* 非本烟测路径的任务/Legacy入口触及即失败，不伪造所属任务。 */
__asm void USB_UnboundTrap(void) { BKPT 0
 B USB_UnboundTrap
}
void App_CameraQualificationAccess(void){USB_UnboundTrap();}
void App_MessagesIsOwner(void){USB_UnboundTrap();}
void Platform_SyncGetException(void){USB_UnboundTrap();}
