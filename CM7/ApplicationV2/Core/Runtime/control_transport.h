#ifndef V2_CONTROL_TRANSPORT_H
#define V2_CONTROL_TRANSPORT_H

#include "control_service.h"

#define CSA_OPEN 1U
#define CSA_SUBMIT 2U
#define CSA_QUERY 3U
#define CSA_CONFIRM 4U
#define CSA_CANCEL 5U
#define CSA_CLOSE 6U

/* 本地值消息，没有wire布局。route由可信传输端提供，exchange用于该端口回送。 */
typedef struct
{
    CSPeer peer;
    uint32_t exchange;
} CSRoute;
typedef struct
{
    CSRoute route;
    uint32_t type;
    union
    {
        CSRequest request;
        CSIdentity id;
        CSReply reply;
    } body;
} CSIngress;
typedef struct
{
    CSRoute route;
    uint32_t type, boot, session;
    SysResult_t result;
    CSReply reply;
} CSEgress;
typedef struct
{
    int (*owner)(void *);
    SysResult_t (*receive)(void *, CSIngress *);
    SysResult_t (*send)(void *, const CSEgress *); /* OK只表示入队 */
} CSAdapterOps;
typedef struct
{
    uint32_t magic, channel, pending, busy;
    CSService *service;
    const CSAdapterOps *ops;
    void *port;
    CSEgress output;
} CSAdapter;

SysResult_t CSAdapter_Init(CSAdapter *, CSService *, uint32_t channel, const CSAdapterOps *,
                           void *);
SysResult_t CSAdapter_Process(CSAdapter *, uint32_t now);
#endif
