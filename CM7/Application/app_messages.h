#ifndef CM7_APP_MESSAGES_H
#define CM7_APP_MESSAGES_H

#include "runtime_event.h"
#include "camera_config.h"
#include <stdbool.h>

typedef enum
{
    APP_CHANNEL_CAMERA_COMMAND = 0,
    APP_CHANNEL_USB_COMMAND,
    APP_CHANNEL_CONTROL,
    APP_CHANNEL_TO_CAMERA,
    APP_CHANNEL_TO_USB,
    APP_CHANNEL_COUNT
} AppChannel_t;

typedef enum
{
    APP_MESSAGE_START = 1,
    APP_MESSAGE_STOP,
    APP_MESSAGE_ACK,
    APP_MESSAGE_FAULT,
    APP_MESSAGE_FRAME_REQUEST,
    APP_MESSAGE_FRAME_OFFER,
    APP_MESSAGE_FRAME_RETURN,
    APP_MESSAGE_RETURN_ACK,

    //å–æ¶ˆå°šæœªå–å¾—å¸§çš„è¯·æ±‚ï¼Œä¸å–æ¶ˆå·²æœ‰ç§Ÿå€Ÿ
    APP_MESSAGE_FRAME_CANCEL_REQUEST
} AppMessageType_t;

//æ¶ˆæ¯åªå¤åˆ¶æè¿°ä¸å‡­æ®ï¼Œä¸å¤åˆ¶SDRAMå›¾åƒ
typedef struct
{
    AppMessageType_t type;
    RuntimeEventTarget_t source;
    uint32_t command_id;
    uint32_t stream_epoch;
    uint32_t lease_id;
    SysResult_t result;
    //å®Œæ•´å¸§çš„åˆ†å‘å…ƒæ•°æ®ï¼Œä¸å‚ä¸åŸç§Ÿçº¦ä¸‰å…ƒç»„çš„åŒ¹é…
    uint32_t frame_id;
    uint32_t timestamp_ms;
    CameraFrame_t frame;
} AppMessage_t;

//å‡†å¤‡é™æ€æ¶ˆæ¯é˜Ÿåˆ—
SysResult_t App_MessagesInit(void);

//éé˜»å¡æäº¤æ¶ˆæ¯ï¼Œé˜Ÿåˆ—æ»¡æ—¶ç”±å‘é€æ–¹ä¿ç•™çŠ¶æ€å¹¶é‡è¯•
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message);

//å–å¾—å½“å‰ä»»åŠ¡æ‰€å±é€šé“çš„ä¸‹ä¸€æ¡æ¶ˆæ¯
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message);

//æ£€æŸ¥å½“å‰ä»»åŠ¡æ˜¯å¦ä¸ºæŒ‡å®šä¸šåŠ¡æ‰€æœ‰è€…
bool App_MessagesIsOwner(RuntimeEventTarget_t target);

//ÉúÃüÖÜÆÚÏûÏ¢Ê¹ÓÃ¶ÀÁ¢ÀàĞÍºÍ¶ÓÁĞ£¬¾ÉÖ¡ÏûÏ¢²¼¾ÖÓëÍ¨µÀ±àºÅ±£³Ö²»±ä
#include "app_camera_lifecycle.h"

typedef enum
{
    APP_LIFECYCLE_CHANNEL_CAMERA = 0,
    APP_LIFECYCLE_CHANNEL_USB,
    APP_LIFECYCLE_CHANNEL_STORAGE,
    APP_LIFECYCLE_CHANNEL_COUNT
} AppLifecycleChannel_t;

//Ö»×¼±¸ÈıÌõËÄÏî¾²Ì¬¶ÓÁĞ£¬²»Ö´ĞĞÈÎºÎÒµÎñSTART»òSTOP
SysResult_t App_LifecycleMessagesInit(void);

//¼ì²éÏûÏ¢·½Ïò¼°ÕæÊµÈÎÎñÉí·İ£¬³É¹¦Ö»±íÊ¾ÍêÕûÏûÏ¢ÒÑ¾­Èë¶Ó
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t channel, const AppLifecycleMessage_t *message);

//Ö»ÓĞ¶ÔÓ¦ËùÓĞÕßÈÎÎñ¿ÉÒÔÈ¡ÏûÏ¢£¬Í¨ÖªÎ»²»×÷Îª½»¸¶ÒÀ¾İ
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t channel, AppLifecycleMessage_t *message);

#endif
