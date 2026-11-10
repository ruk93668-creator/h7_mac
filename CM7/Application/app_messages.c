#include "app_messages.h"
#include "app_events.h"
#include "queue.h"
#include "platform_sync.h"
#include <stddef.h>

#define APP_MESSAGE_QUEUE_LENGTH 4U

//æ¯ä¸ªæ–¹å‘ä½¿ç”¨ç‹¬ç«‹é™æ€é˜Ÿåˆ—ï¼Œå‘½ä»¤ä¸ä¸å›¾åƒæ¶ˆæ¯äº‰ç”¨æ§½ä½
static StaticQueue_t s_message_queues[APP_CHANNEL_COUNT];

//é˜Ÿåˆ—å†…åªå­˜æè¿°å’Œå‡­æ®ï¼Œå›¾åƒä»ç”±Mediaç®¡ç†
static uint8_t s_message_storage[APP_CHANNEL_COUNT][APP_MESSAGE_QUEUE_LENGTH * sizeof(AppMessage_t)]
    __attribute__((aligned(8)));

//ä¿å­˜å„é€šé“çš„å†…æ ¸é˜Ÿåˆ—å¥æŸ„
static QueueHandle_t s_message_handles[APP_CHANNEL_COUNT];

//è®°å½•é˜Ÿåˆ—åˆå§‹åŒ–å°è¯•ï¼Œéƒ¨åˆ†å¤±è´¥åä¸å¤ç”¨é™æ€æ§åˆ¶å—
static bool s_messages_attempted;

//ä¿å­˜å„é€šé“å”¯ä¸€çš„æ¥æ”¶ä»»åŠ¡
static const RuntimeEventTarget_t s_message_receivers[APP_CHANNEL_COUNT] =
{
    RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB, RUNTIME_TARGET_CONTROL,
    RUNTIME_TARGET_CAMERA, RUNTIME_TARGET_USB
};

//æ£€æŸ¥å½“å‰ä»»åŠ¡æ˜¯å¦ä¸ºæŒ‡å®šä¸šåŠ¡æ‰€æœ‰è€…
bool App_MessagesIsOwner(RuntimeEventTarget_t target)
{
    return App_EventsIsOwner(target);
}

//å‡†å¤‡é™æ€æ¶ˆæ¯é˜Ÿåˆ—
SysResult_t App_MessagesInit(void)
{
    uint32_t i;

    //åªå…è®¸è°ƒåº¦å‰åˆ›å»ºä¸€æ¬¡ï¼Œä»»ä½•éƒ¨åˆ†å¤±è´¥éƒ½ç”±mainç»ˆæ­¢å¯åŠ¨
    if (s_messages_attempted || Platform_SyncGetException() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    s_messages_attempted = true;
    for (i = 0U; i < APP_CHANNEL_COUNT; i++)
    {
        s_message_handles[i] = xQueueCreateStatic(APP_MESSAGE_QUEUE_LENGTH,
            sizeof(AppMessage_t), s_message_storage[i], &s_message_queues[i]);
        if (s_message_handles[i] == NULL)
            return SYS_ERR_NO_RESOURCE;
    }
    return SYS_OK;
}

//éé˜»å¡æäº¤æ¶ˆæ¯
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message)
{
    bool allowed;

    if ((uint32_t)channel >= APP_CHANNEL_COUNT || message == NULL)
        return SYS_ERR_INVALID_ARG;
    if (s_message_handles[channel] == NULL || Platform_SyncGetException() != 0U)
        return SYS_ERR_INVALID_STATE;

    //å‘½ä»¤åªç”±Controlå‘é€ï¼Œå¸§æ¶ˆæ¯åªå…è®¸ç›¸åº”æ‰€æœ‰è€…å‘é€
    if (channel == APP_CHANNEL_CAMERA_COMMAND || channel == APP_CHANNEL_USB_COMMAND)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_CONTROL);
    else if (channel == APP_CHANNEL_TO_CAMERA)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_USB);
    else if (channel == APP_CHANNEL_TO_USB)
        allowed = App_MessagesIsOwner(RUNTIME_TARGET_CAMERA);
    else
        allowed = App_MessagesIsOwner(message->source);
    if (!allowed)
        return SYS_ERR_INVALID_STATE;

    //æˆåŠŸå…¥é˜Ÿå°±æ˜¯æ¶ˆæ¯å·²æäº¤ï¼Œé€šçŸ¥ä½åªè´Ÿè´£å”¤é†’ï¼Œä¸ä½œä¸ºäº¤ä»˜å‡­æ®
    if (xQueueSend(s_message_handles[channel], message, 0U) != pdPASS)
        return SYS_ERR_NO_RESOURCE;
    (void)RuntimeEvent_Post(s_message_receivers[channel], RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//å–å¾—å½“å‰ä»»åŠ¡æ‰€å±é€šé“çš„ä¸‹ä¸€æ¡æ¶ˆæ¯
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message)
{
    if ((uint32_t)channel >= APP_CHANNEL_COUNT || message == NULL ||
        s_message_handles[channel] == NULL || !App_MessagesIsOwner(s_message_receivers[channel]))
        return false;
    if (xQueueReceive(s_message_handles[channel], message, 0U) != pdPASS)
        return false;

    //è…¾å‡ºæ§½ä½åå”¤é†’å‘é€æ–¹ï¼Œé˜Ÿåˆ—æ»¡æ—¶ä¿å­˜çš„æ¶ˆæ¯æ‰èƒ½ç»§ç»­æäº¤
    if (channel == APP_CHANNEL_TO_CAMERA)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_TO_USB)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
    else if (channel == APP_CHANNEL_CONTROL)
    {
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
    }
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_WAKE);
    return true;
}

//C1¶ÀÁ¢Í¨µÀ½ö×¼±¸»ù´¡ÉèÊ©£¬Ô­ÓĞÎåÌõ¶ÓÁĞºÍÊÕ·¢º¯Êı±£³Ö²»±ä
#include "framework_init.h"
#define APP_LIFECYCLE_QUEUE_LENGTH 4U

//±£´æÈıÌõÉúÃüÖÜÆÚ¶ÓÁĞµÄ¾²Ì¬¿ØÖÆ¿é
static StaticQueue_t s_lifecycle_queues[APP_LIFECYCLE_CHANNEL_COUNT];

//Ã¿Ïî¹Ì¶¨56B£¬²»À©´ó¾ÉAppMessageµÄ¸´ÖÆ·¶Î§
static uint8_t s_lifecycle_storage[APP_LIFECYCLE_CHANNEL_COUNT]
    [APP_LIFECYCLE_QUEUE_LENGTH * sizeof(AppLifecycleMessage_t)] __attribute__((aligned(8)));

//±£´æ¸÷ÉúÃüÖÜÆÚÍ¨µÀµÄÄÚºË¾ä±ú
static QueueHandle_t s_lifecycle_handles[APP_LIFECYCLE_CHANNEL_COUNT];

//²¿·Ö³õÊ¼»¯Ê§°ÜºóÒ²²»ÖØ¸´Ê¹ÓÃÍ¬Ò»·İ¾²Ì¬¿ØÖÆ¿é
static bool s_lifecycle_attempted;

//Ö»ÓĞÈ«²¿Í¨µÀ³É¹¦´´½¨ºó²Å¿ª·ÅÊÕ·¢
static bool s_lifecycle_ready;

//½«ÆÕÍ¨CĞ­Òé½ÇÉ«Ó³Éäµ½ÏÖÓĞÈÎÎñÉí·İ
static RuntimeEventTarget_t Lifecycle_Task(uint32_t role);

//²éÑ¯Ö¸¶¨Í¨µÀÎ¨Ò»µÄ½ÓÊÕ½ÇÉ«
static uint32_t Lifecycle_Receiver(AppLifecycleChannel_t channel);

//¼ì²éÆÕÍ¨ÈÎÎñÊÇ·ñ¿ÉÒÔÊ¹ÓÃ·Ç×èÈûRTOS¶ÓÁĞ
static bool Lifecycle_Context(void);

//½«Ğ­Òé½ÇÉ«Ó³Éäµ½ÒÑµÇ¼ÇÈÎÎñ£¬Ğ­ÒéÖĞ²»±£´æTaskHandle
static RuntimeEventTarget_t Lifecycle_Task(uint32_t role)
{
    switch (role)
    {
        case APP_LIFECYCLE_CAMERA: return RUNTIME_TARGET_CAMERA;
        case APP_LIFECYCLE_USB: return RUNTIME_TARGET_USB;
        case APP_LIFECYCLE_STORAGE: return RUNTIME_TARGET_STORAGE;
        default: return RUNTIME_TARGET_COUNT;
    }
}

//Ã¿ÌõÍ¨µÀÖ»·şÎñÒ»¸ö½ÓÊÕÈÎÎñ£¬²»ÄÜ°´ÏûÏ¢ÄÚÈİÈÎÒâ¸ÄÂ·ÓÉ
static uint32_t Lifecycle_Receiver(AppLifecycleChannel_t channel)
{
    switch (channel)
    {
        case APP_LIFECYCLE_CHANNEL_CAMERA: return APP_LIFECYCLE_CAMERA;
        case APP_LIFECYCLE_CHANNEL_USB: return APP_LIFECYCLE_USB;
        case APP_LIFECYCLE_CHANNEL_STORAGE: return APP_LIFECYCLE_STORAGE;
        default: return 0U;
    }
}

//²»ÔÊĞíISR¡¢ÆÁ±ÎÖĞ¶Ï»òµ÷¶ÈÆ÷Î´ÔËĞĞÊ±½øÈëÈÎÎñ²àÊÕ·¢
static bool Lifecycle_Context(void)
{
    return Platform_SyncGetException() == 0U && Platform_SyncGetPrimask() == 0U &&
        Platform_SyncGetBasepri() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

//Framework×¼±¸¶ÀÁ¢¾²Ì¬¶ÓÁĞ£¬²»ÇëÇó²É¼¯¡¢USB»òÎÄ¼şÏµÍ³
SysResult_t App_LifecycleMessagesInit(void)
{
    uint32_t i;

    //³õÊ¼»¯Ö»ÄÜ·¢ÉúÒ»´Î£¬Ê§°Üºó±£³ÖËùÓĞĞÂÍ¨µÀ¹Ø±Õ
    if (s_lifecycle_attempted || Platform_SyncGetException() != 0U ||
        Platform_SyncGetPrimask() != 0U || Platform_SyncGetBasepri() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    s_lifecycle_attempted = true;
    for (i = 0U; i < APP_LIFECYCLE_CHANNEL_COUNT; i++)
    {
        s_lifecycle_handles[i] = xQueueCreateStatic(APP_LIFECYCLE_QUEUE_LENGTH,
            sizeof(AppLifecycleMessage_t), s_lifecycle_storage[i], &s_lifecycle_queues[i]);
        if (s_lifecycle_handles[i] == NULL)
            return SYS_ERR_NO_RESOURCE;
    }
    s_lifecycle_ready = true;
    return SYS_OK;
}

//Ö»ÓĞÏûÏ¢ÉùÃ÷Óëµ±Ç°ÈÎÎñÉí·İÍ¬Ê±ºÏ·¨²ÅÔÊĞíÈë¶Ó
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t channel, const AppLifecycleMessage_t *message)
{
    if ((uint32_t)channel >= APP_LIFECYCLE_CHANNEL_COUNT || !App_LifecycleValidate(message))
        return SYS_ERR_INVALID_ARG;
    if (!s_lifecycle_ready || !Lifecycle_Context())
        return SYS_ERR_INVALID_STATE;

    //²»ÄÜÃ°³äÁíÒ»Ïû·ÑÕß£¬Ò²²»ÄÜ°ÑºÏ·¨ÏûÏ¢Í¶Ïò´íÎóÈÎÎñ
    if (message->target != Lifecycle_Receiver(channel) ||
        !App_EventsIsOwner(Lifecycle_Task(message->source)))
        return SYS_ERR_INVALID_STATE;
    if (xQueueSend(s_lifecycle_handles[channel], message, 0U) != pdPASS)
        return SYS_ERR_NO_RESOURCE;

    //Èë¶Óºó¼´ÒÑ½»¸¶¸ø¶ÓÁĞ£¬Í¨ÖªÊ§°ÜÒ²²»ÄÜÒªÇóÖØ¸´Èë¶Ó
    (void)RuntimeEvent_Post(Lifecycle_Task(message->target), RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//È¡×ßÏûÏ¢ºó»½ĞÑ¿ÉÄÜµÈ´ı²ÛÎ»µÄËùÓĞºÏ·¨·¢ËÍÕß
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t channel, AppLifecycleMessage_t *message)
{
    if ((uint32_t)channel >= APP_LIFECYCLE_CHANNEL_COUNT || message == NULL ||
        !s_lifecycle_ready || !Lifecycle_Context() ||
        !App_EventsIsOwner(Lifecycle_Task(Lifecycle_Receiver(channel))))
        return false;
    if (xQueueReceive(s_lifecycle_handles[channel], message, 0U) != pdPASS)
        return false;

    //Á½¸öÏû·ÑÕß¹²ÏíCameraÊÕ¼şÏä£¬ÌÚ³ö²ÛÎ»ºó¶¼ĞèÒª»ñµÃÖØÊÔ»ú»á
    if (channel == APP_LIFECYCLE_CHANNEL_CAMERA)
    {
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_WAKE);
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    }
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
    return true;
}

FRAMEWORK_INIT_REGISTER(App_LifecycleMessagesInit, INIT_LEVEL_APPLICATION);
