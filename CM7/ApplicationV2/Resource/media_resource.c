// 关联授权、交接和真实媒体持有，协调归还、收据和退出核验。

#include "media_resource.h"
#include <string.h>
#define MAGIC 0x4D525632U
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(MRContext *resource)
{
    //核对调用对象和所有者条件，避免越权操作资源账本。
    if (!resource || resource->magic != MAGIC)
        return SYS_ERR_INVALID_STATE;
    if (!resource->source.ops->is_owner(resource->source.context))
        return SYS_ERR_INVALID_STATE;
    if (resource->busy)
        return SYS_ERR_BUSY;
    resource->busy = 1;
    return SYS_OK;
}
// 退出当前调用的重入保护。
static SysResult_t leave(MRContext *resource, SysResult_t result)
{
    resource->busy = 0;
    return result;
}
// 检查 Grant 的必要身份字段。
static int valid_g(const MRGrant *grant)
{
    return grant && grant->session && grant->consumer && grant->generation && grant->acquire;
}
// 查找指定 Grant 的准入记录。
static MRAdmission *admission(MRContext *resource, const MRGrant *grant)
{
    uint32_t admission_index;
    //在固定容量准入表查找匹配授权或空闲槽。
    for (admission_index = 0; admission_index < MR_GRANT_MAX; admission_index++)
        if (resource->admission[admission_index].occupied && MRGrant_Equal(&resource->admission[admission_index].g, grant))
            return &resource->admission[admission_index];
    return NULL;
}
// 按完整身份定位已有记录。
static MRRecord *record(MRContext *resource, const MRRequest *request)
{
    uint32_t record_index;
    if (!request)
        return NULL;
    for (record_index = 0; record_index < MR_HANDOFF_MAX; record_index++)
        if (resource->records[record_index].state && !memcmp(&resource->records[record_index].request, request, sizeof(*request)))
            return &resource->records[record_index];
    return NULL;
}
// 按请求身份查找对应的终结收据。
static MRReceiptRecord *receipt(MRContext *resource, const MRReceipt *terminal_receipt)
{
    uint32_t receipt_index;
    if (!terminal_receipt)
        return NULL;
    for (receipt_index = 0; receipt_index < MR_RECEIPT_MAX; receipt_index++)
        if (resource->receipts[receipt_index].occupied && !memcmp(&resource->receipts[receipt_index].value, terminal_receipt, sizeof(*terminal_receipt)))
            return &resource->receipts[receipt_index];
    return NULL;
}
// 初始化有界资源关联账本并绑定可信物理后端。
SysResult_t MR_Init(MRContext *resource, const MRSource *source)
{
    MRSourceCaps caps;
    SysResult_t result;
    //核对调用对象和所有者条件，避免越权操作资源账本。
    if (!resource || resource->magic || V2_Overlap(resource, sizeof(*resource), source, sizeof(*source)))
        return SYS_ERR_INVALID_ARG;
    //先取得物理后端能力，再建立唯一资源上下文。
    result = MRSource_Query(source, &caps);
    if (result)
        return result;
    memset(resource, 0, sizeof(*resource));
    resource->source = *source;
    resource->caps = caps;
    resource->magic = MAGIC;
    return SYS_OK;
}
// 关联来自权威资格方的 Grant，建立准入上下文。
SysResult_t MR_AssociateGrant(MRContext *resource, const MRGrant *grant)
{
    uint32_t admission_index, handoff_index, role;
    MRAdmission *admission_entry = NULL;
    SysResult_t result;
    //先核对授权代次和消费者身份，拒绝空凭据。
    if (!valid_g(grant))
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    //由真实后端把消费者映射为物理所有者类型。
    result = resource->source.ops->role(resource->source.context, grant, &role);
    if (result)
        return leave(resource, result);
    if ((!role || (role & (role - 1U)) || role == MR_CAP_INTERNAL) || !(resource->caps.owners & role))
        return leave(resource, SYS_ERR_NOT_SUPPORTED);
    //在固定容量准入表查找匹配授权或空闲槽。
    for (admission_index = 0; admission_index < MR_GRANT_MAX; admission_index++)
        if (resource->admission[admission_index].occupied && resource->admission[admission_index].g.consumer == grant->consumer)
        {
            admission_entry = &resource->admission[admission_index];
            //同代授权只允许重复关联，已经关闭的授权不能重开。
            if (MRGrant_Equal(&admission_entry->g, grant))
                return leave(resource, admission_entry->closed ? SYS_ERR_INVALID_STATE : SYS_OK);
            //只有新一代能替换已关闭的授权。
            if (!admission_entry->closed || grant->session < admission_entry->g.session ||
                (grant->session == admission_entry->g.session && grant->generation <= admission_entry->g.generation))
                return leave(resource, SYS_ERR_INVALID_STATE);
            for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
                if (resource->records[handoff_index].state && MRGrant_Equal(&resource->records[handoff_index].request.g, &admission_entry->g))
                    return leave(resource, SYS_ERR_BUSY);
            //旧授权必须有真实后端退出证据后才能替换。
            result = resource->source.ops->exit_safe(resource->source.context, &admission_entry->g);
            if (result)
                return leave(resource, result);
            break;
        }
    if (!admission_entry)
        //在固定容量准入表查找匹配授权或空闲槽。
        for (admission_index = 0; admission_index < MR_GRANT_MAX; admission_index++)
            if (!resource->admission[admission_index].occupied)
            {
                admission_entry = &resource->admission[admission_index];
                break;
            }
    if (!admission_entry)
        return leave(resource, SYS_ERR_NO_RESOURCE);
    //写入或获取之前重新核验授权，避免旧身份进入物理后端。
    result = resource->source.ops->grant_valid(resource->source.context, grant);
    if (result)
        return leave(resource, result);
    memset(admission_entry, 0, sizeof(*admission_entry));
    //所有校验成功后再登记合法授权。
    admission_entry->g = *grant;
    admission_entry->occupied = 1;
    return leave(resource, SYS_OK);
}
// 登记原交接身份和元数据，尚未提交队列时保持可本地取消。
SysResult_t MR_Stage(MRContext *resource, const MRGrant *grant, const MRBridge *bridge, const MRSlot *slot,
                     MRRequest *out)
{
    MRRecord *handoff_record = NULL;
    MRAdmission *admission_entry;
    uint32_t handoff_index, role;
    SysResult_t result;
    if (!valid_g(grant) || !bridge || !slot || !out || !resource || V2_Overlap(resource, sizeof(*resource), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    //由真实后端把消费者映射为物理所有者类型。
    result = resource->source.ops->role(resource->source.context, grant, &role);
    if (result)
        return leave(resource, result);
    if ((role == MR_CAP_USB && (!bridge->epoch || !bridge->command)) ||
        (role == MR_CAP_SD && (!slot->session || !slot->generation)))
        return leave(resource, SYS_ERR_INVALID_ARG);
    admission_entry = admission(resource, grant);
    if (!admission_entry || admission_entry->closed)
        return leave(resource, SYS_ERR_INVALID_STATE);
    if (resource->issued == UINT32_MAX)
        return leave(resource, SYS_ERR_NO_RESOURCE);
    for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
        if (!resource->records[handoff_index].state)
        {
            handoff_record = &resource->records[handoff_index];
            break;
        }
    if (!handoff_record)
        return leave(resource, SYS_ERR_NO_RESOURCE);
    memset(handoff_record, 0, sizeof(*handoff_record));
    handoff_record->request.g = *grant;
    handoff_record->request.b = *bridge;
    handoff_record->request.s = *slot;
    handoff_record->request.h.session = grant->session;
    MRNumber_Advance(&resource->issued);
    handoff_record->request.h.sequence = resource->issued;
    handoff_record->state = MR_LOCAL;
    *out = handoff_record->request;
    return leave(resource, SYS_OK);
}
// 记录原请求已提交，后续按同一身份办理终结。
SysResult_t MR_Commit(MRContext *resource, const MRRequest *request)
{
    MRRecord *handoff_record;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record)
        return leave(resource, SYS_ERR_INVALID_ARG);
    if (handoff_record->state == MR_COMMITTED)
        return leave(resource, SYS_OK);
    if (handoff_record->state != MR_LOCAL)
        return leave(resource, SYS_ERR_INVALID_STATE);
    handoff_record->state = MR_COMMITTED;
    return leave(resource, SYS_OK);
}
// 取消尚未提交的交接，不生成 Camera 终结收据。
SysResult_t MR_CancelLocal(MRContext *resource, const MRRequest *request)
{
    MRRecord *handoff_record;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record || handoff_record->state != MR_LOCAL)
        return leave(resource, SYS_ERR_INVALID_STATE);
    memset(handoff_record, 0, sizeof(*handoff_record));
    return leave(resource, SYS_OK);
}

// 确认请求身份与已有交接记录相符。
static MRReceiptRecord *for_record(MRContext *resource, const MRRecord *handoff_record)
{
    uint32_t receipt_index;
    for (receipt_index = 0; receipt_index < MR_RECEIPT_MAX; receipt_index++)
        if (resource->receipts[receipt_index].occupied && MRGrant_Equal(&resource->receipts[receipt_index].value.g, &handoff_record->request.g) &&
            !memcmp(&resource->receipts[receipt_index].value.h, &handoff_record->request.h, sizeof(MRHandoff)))
            return &resource->receipts[receipt_index];
    return NULL;
}
// 固定已结束交接的结果并生成待发送收据。
static void terminal(MRRecord *handoff_record, MRReceiptRecord *receipt_entry, SysResult_t result)
{
    handoff_record->state = MR_TERMINAL;
    receipt_entry->value.m = handoff_record->media;
    receipt_entry->value.result = result;
    receipt_entry->value.physical_returned = handoff_record->media.id != 0U;
    receipt_entry->value.bridge = handoff_record->bridge;
}
// 在 Camera 所有者上下文核验准入并预留终结所需空间。
SysResult_t MR_Admit(MRContext *resource, const MRRequest *request)
{
    MRRecord *handoff_record;
    MRAdmission *admission_entry;
    MRReceiptRecord *receipt_entry = NULL;
    uint32_t receipt_index;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record)
        return leave(resource, SYS_ERR_INVALID_ARG);
    if (handoff_record->state >= MR_ACCEPTED)
        return leave(resource, handoff_record->state == MR_BLOCKED ? handoff_record->last_release : SYS_OK);
    if (handoff_record->state != MR_COMMITTED)
        return leave(resource, SYS_ERR_INVALID_STATE);
    if (resource->receipt_issued == UINT32_MAX)
        return leave(resource, SYS_ERR_NO_RESOURCE);
    for (receipt_index = 0; receipt_index < MR_RECEIPT_MAX; receipt_index++)
        if (!resource->receipts[receipt_index].occupied)
        {
            receipt_entry = &resource->receipts[receipt_index];
            break;
        }
    if (!receipt_entry)
        return leave(resource, SYS_ERR_NO_RESOURCE);
    memset(receipt_entry, 0, sizeof(*receipt_entry));
    receipt_entry->occupied = 1;
    receipt_entry->value.g = request->g;
    receipt_entry->value.h = request->h;
    MRNumber_Advance(&resource->receipt_issued);
    receipt_entry->value.serial = resource->receipt_issued;
    handoff_record->state = MR_ACCEPTED;
    admission_entry = admission(resource, &request->g);
    result = (!admission_entry || admission_entry->closed) ? SYS_ERR_INVALID_STATE
                          : resource->source.ops->grant_valid(resource->source.context, &request->g);
    if (result)
        terminal(handoff_record, receipt_entry, result);
    return leave(resource, SYS_OK);
}
// 关闭指定 Grant 的准入，保留已受理交接的归还路径。
SysResult_t MR_Close(MRContext *resource, const MRRelease *release)
{
    MRAdmission *admission_entry;
    SysResult_t result;
    if (!release || !release->request || !valid_g(&release->grant))
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    admission_entry = admission(resource, &release->grant);
    if (!admission_entry)
        return leave(resource, SYS_ERR_INVALID_STATE);
    if (admission_entry->closed)
        return leave(resource, !memcmp(&admission_entry->closing, release, sizeof(*release)) ? SYS_OK : SYS_ERR_BUSY);
    admission_entry->closing = *release;
    admission_entry->closed = 1;
    return leave(resource, SYS_OK);
}
// 真实 Get 成功后才由 Bridge 分配 B。Stage 不得提前分配 B 或再次取帧。
static SysResult_t bind_bridge(MRContext *resource, MRRecord *handoff_record, const MRBridge *bridge)
{
    uint32_t handoff_index, role;
    SysResult_t result;
    if (!bridge || !bridge->epoch || !bridge->lease || !bridge->command || bridge->epoch != handoff_record->request.b.epoch ||
        bridge->command != handoff_record->request.b.command)
        return SYS_ERR_INVALID_ARG;
    //由真实后端把消费者映射为物理所有者类型。
    result = resource->source.ops->role(resource->source.context, &handoff_record->request.g, &role);
    if (result)
        return result;
    if (role != MR_CAP_USB || handoff_record->state != MR_BOUND)
        return SYS_ERR_INVALID_STATE;
    if (handoff_record->bridge.lease)
        return memcmp(&handoff_record->bridge, bridge, sizeof(*bridge)) ? SYS_ERR_PROTOCOL : SYS_OK;
    for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
        if (&resource->records[handoff_index] != handoff_record && resource->records[handoff_index].state &&
            !memcmp(&resource->records[handoff_index].bridge, bridge, sizeof(*bridge)))
            return SYS_ERR_ALREADY_EXISTS;
    handoff_record->bridge = *bridge;
    return SYS_OK;
}
// 为已取得的媒体绑定 Bridge 身份。
SysResult_t MR_BindBridge(MRContext *resource, const MRRequest *request, const MRBridge *bridge)
{
    MRRecord *handoff_record;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record)
        return leave(resource, SYS_ERR_INVALID_ARG);
    result = bind_bridge(resource, handoff_record, bridge);
    return leave(resource, result);
}
// 按完整 Bridge 身份查找关联的交接信息。
SysResult_t MR_LookupBridge(MRContext *resource, const MRBridge *bridge, MRRequest *request, MRMedia *media)
{
    uint32_t handoff_index;
    SysResult_t result;
    if (!resource || !bridge || !bridge->lease || !request || !media || V2_Overlap(resource, sizeof(*resource), request, sizeof(*request)) ||
        V2_Overlap(resource, sizeof(*resource), media, sizeof(*media)) || V2_Overlap(request, sizeof(*request), media, sizeof(*media)))
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
        if (resource->records[handoff_index].state && resource->records[handoff_index].bridge.lease &&
            !memcmp(&resource->records[handoff_index].bridge, bridge, sizeof(*bridge)))
        {
            *request = resource->records[handoff_index].request;
            *media = resource->records[handoff_index].media;
            return leave(resource, SYS_OK);
        }
    return leave(resource, SYS_ERR_NOT_FOUND);
}
// 通过可信后端取得物理媒体持有并关联到交接。
SysResult_t MR_Obtain(MRContext *resource, const MRRequest *request, MRMedia *media, MRView *view)
{
    MRRecord *handoff_record;
    MRAdmission *admission_entry;
    MRReceiptRecord *receipt_entry;
    uint32_t handoff_index, live_count = 0;
    SysResult_t status_result;
    if (!media || !view || !resource || V2_Overlap(resource, sizeof(*resource), media, sizeof(*media)) ||
        V2_Overlap(resource, sizeof(*resource), view, sizeof(*view)) || V2_Overlap(media, sizeof(*media), view, sizeof(*view)))
        return SYS_ERR_INVALID_ARG;
    status_result = enter(resource);
    if (status_result)
        return status_result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record)
        return leave(resource, SYS_ERR_INVALID_ARG);
    if (handoff_record->state == MR_BOUND)
    {
        *media = handoff_record->media;
        *view = handoff_record->view;
        return leave(resource, SYS_OK);
    }
    if (handoff_record->state == MR_BLOCKED)
        return leave(resource, handoff_record->last_release);
    if (handoff_record->state != MR_ACCEPTED)
        return leave(resource, SYS_ERR_INVALID_STATE);
    receipt_entry = for_record(resource, handoff_record);
    if (!receipt_entry)
        return leave(resource, SYS_ERR_INVALID_STATE);
    admission_entry = admission(resource, &request->g);
    if (!admission_entry || admission_entry->closed)
    {
        terminal(handoff_record, receipt_entry, SYS_ERR_INVALID_STATE);
        return leave(resource, SYS_ERR_INVALID_STATE);
    }
    status_result = resource->source.ops->grant_valid(resource->source.context, &request->g);
    if (status_result)
    {
        terminal(handoff_record, receipt_entry, status_result);
        return leave(resource, status_result);
    }
    for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
        if (resource->records[handoff_index].state == MR_BOUND || resource->records[handoff_index].state == MR_BLOCKED ||
            resource->records[handoff_index].state == MR_RETURN_PENDING || resource->records[handoff_index].state == MR_RELEASED)
            live_count++;
    if (live_count >= resource->caps.concurrent)
        return leave(resource, SYS_ERR_BUSY);
    status_result = resource->source.ops->obtain(resource->source.context, &request->g, &request->h, &handoff_record->media, &handoff_record->view);
    if (status_result)
    {
        // 仅当返回 NOT_FOUND 且没有凭据时，端口才能明确证明未获取资源。
        if (!handoff_record->media.id && status_result == SYS_ERR_NOT_FOUND)
            terminal(handoff_record, receipt_entry, status_result);
        else
        {
            handoff_record->state = MR_BLOCKED;
            handoff_record->last_release = status_result;
        }
        return leave(resource, status_result);
    }
    handoff_record->state = MR_BLOCKED; // 输出异常时保留后端可能已经取得的持有。
    if (handoff_record->media.backend != resource->caps.backend || !handoff_record->media.session || !handoff_record->media.id ||
        !handoff_record->media.generation || !handoff_record->view.data || !handoff_record->view.bytes ||
        handoff_record->view.bytes > UINT32_MAX - handoff_record->view.bytes2 ||
        (resource->caps.spans == 1 && (handoff_record->view.data2 || handoff_record->view.bytes2)) ||
        ((handoff_record->view.bytes2 != 0) != (handoff_record->view.data2 != NULL)))
    {
        handoff_record->last_release = SYS_ERR_INVALID_STATE;
        return leave(resource, handoff_record->last_release);
    }
    status_result = resource->source.ops->validate(resource->source.context, &request->g, &handoff_record->media, &handoff_record->view);
    if (status_result)
    {
        handoff_record->last_release = status_result;
        return leave(resource, status_result);
    }
    handoff_record->state = MR_BOUND;
    // 兼容已预留 B 的调用者。Legacy 顺序传入 lease 为 0，取得媒体后再绑定 B 并发布。
    if (request->b.lease)
    {
        uint32_t role;
        status_result = resource->source.ops->role(resource->source.context, &request->g, &role);
        if (status_result)
            return leave(resource, status_result);
        if (role == MR_CAP_USB)
        {
            status_result = bind_bridge(resource, handoff_record, &request->b);
            if (status_result)
                return leave(resource, status_result);
        }
    }
    *media = handoff_record->media;
    *view = handoff_record->view;
    return leave(resource, SYS_OK);
}

// 在读取者退场后调用后端真实归还，保留失败后的持有事实。
SysResult_t MR_Return(MRContext *resource, const MRRequest *request, const MRMedia *media)
{
    MRRecord *handoff_record;
    MRReceiptRecord *receipt_entry;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record || !MRMedia_Equal(&handoff_record->media, media))
        return leave(resource, SYS_ERR_INVALID_ARG);
    //终结后的重复归还只读冻结结果，不再次释放后端。
    if (handoff_record->state == MR_TERMINAL)
        return leave(resource, for_record(resource, handoff_record)->value.result);
    if (handoff_record->state != MR_BOUND && handoff_record->state != MR_RETURN_PENDING && handoff_record->state != MR_RELEASED)
        return leave(resource, SYS_ERR_INVALID_STATE);
    receipt_entry = for_record(resource, handoff_record);
    if (!receipt_entry)
        return leave(resource, SYS_ERR_INVALID_STATE);
    if (handoff_record->state != MR_RELEASED)
    {
        //标记待归还并等待所有读取者退出，尚未确认物理归还。
        handoff_record->state = MR_RETURN_PENDING;
        result = resource->source.ops->readers_quiescent(resource->source.context, &request->g, media);
        if (result)
            return leave(resource, result);
        // 报错前后端可能已经改变持有状态。重试前核对权威凭据。
        result = resource->source.ops->validate(resource->source.context, &request->g, media, &handoff_record->view);
        if (result)
        {
            if (!handoff_record->last_release)
                return leave(resource, result);
            //核对权威归还证据，不能凭归还调用返回推断已释放。
            result = resource->source.ops->released(resource->source.context, &request->g, media);
            if (result)
                return leave(resource, result);
            handoff_record->state = MR_RELEASED;
        }
        else
        {
            //只有后端确认凭据仍有效时才能执行实际释放。
            result = resource->source.ops->release(resource->source.context, &request->g, media);
            if (result)
            {
                if (result != SYS_ERR_BUSY && !handoff_record->last_release)
                    handoff_record->last_release = result;
                return leave(resource, result);
            }
            handoff_record->state = MR_RELEASED;
        }
    }
    //核对权威归还证据，不能凭归还调用返回推断已释放。
    result = resource->source.ops->released(resource->source.context, &request->g, media);
    if (result)
        return leave(resource, result);
    //物理归还得到证明后才冻结终结收据及首错。
    terminal(handoff_record, receipt_entry, handoff_record->last_release);
    return leave(resource, handoff_record->last_release);
}
// 按匹配身份读取终结收据。
SysResult_t MR_ReadReceipt(MRContext *resource, const MRRequest *request, MRReceipt *out)
{
    MRRecord *handoff_record;
    MRReceiptRecord *receipt_entry;
    SysResult_t result;
    if (!resource || !out || V2_Overlap(resource, sizeof(*resource), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    //按完整交接身份查原记录，不通过帧地址推断身份。
    handoff_record = record(resource, request);
    if (!handoff_record || handoff_record->state != MR_TERMINAL)
        return leave(resource, SYS_ERR_BUSY);
    receipt_entry = for_record(resource, handoff_record);
    if (!receipt_entry)
        return leave(resource, SYS_ERR_INVALID_STATE);
    //输出已冻结的收据，后续运输不能改变其中事实。
    *out = receipt_entry->value;
    return leave(resource, SYS_OK);
}
// 运送冻结的终结收据，入队不证明消费。
SysResult_t MR_FlushReceipt(MRContext *resource, const MRReceipt *terminal_receipt, MRSendReceipt send, void *context)
{
    MRReceiptRecord *receipt_entry;
    MRRecord *handoff_record;
    uint32_t handoff_index;
    SysResult_t result;
    if (!send)
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    receipt_entry = receipt(resource, terminal_receipt);
    if (!receipt_entry)
        return leave(resource, SYS_ERR_INVALID_ARG);
    handoff_record = NULL;
    for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
        if (resource->records[handoff_index].state == MR_TERMINAL && for_record(resource, &resource->records[handoff_index]) == receipt_entry)
        {
            handoff_record = &resource->records[handoff_index];
            break;
        }
    if (!handoff_record)
        return leave(resource, SYS_ERR_INVALID_STATE);
    if (receipt_entry->enqueued)
        return leave(resource, SYS_OK);
    result = send(context, &receipt_entry->value);
    if (!result)
        receipt_entry->enqueued = 1;
    return leave(resource, result);
}
// 登记客户端实际消费了匹配的归还收据。
SysResult_t MR_ConfirmConsumed(MRContext *resource, const MRReceipt *terminal_receipt)
{
    MRReceiptRecord *receipt_entry;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    receipt_entry = receipt(resource, terminal_receipt);
    if (!receipt_entry || !receipt_entry->enqueued)
        return leave(resource, SYS_ERR_INVALID_STATE);
    receipt_entry->consumed = 1;
    return leave(resource, SYS_OK);
}
// 在终结及消费门禁成立后退休交接身份。
SysResult_t MR_Retire(MRContext *resource, const MRReceipt *terminal_receipt)
{
    MRReceiptRecord *receipt_entry;
    uint32_t handoff_index;
    SysResult_t result = enter(resource);
    if (result)
        return result;
    receipt_entry = receipt(resource, terminal_receipt);
    if (!receipt_entry || !receipt_entry->consumed)
        return leave(resource, SYS_ERR_BUSY);
    for (handoff_index = 0; handoff_index < MR_HANDOFF_MAX; handoff_index++)
        if (resource->records[handoff_index].state == MR_TERMINAL && for_record(resource, &resource->records[handoff_index]) == receipt_entry)
        {
            memset(&resource->records[handoff_index], 0, sizeof(resource->records[handoff_index]));
            memset(receipt_entry, 0, sizeof(*receipt_entry));
            return leave(resource, SYS_OK);
        }
    return leave(resource, SYS_ERR_INVALID_STATE);
}

// 核验指定 Grant 已关闭准入、无未完成交接且无物理持有。
SysResult_t MR_VerifyExit(MRContext *resource, const MRRelease *release)
{
    MRAdmission *admission_entry;
    uint32_t entry_index;
    SysResult_t result;
    if (!release)
        return SYS_ERR_INVALID_ARG;
    result = enter(resource);
    if (result)
        return result;
    admission_entry = admission(resource, &release->grant);
    if (!admission_entry || !admission_entry->closed || memcmp(&admission_entry->closing, release, sizeof(*release)))
        return leave(resource, SYS_ERR_INVALID_STATE);
    for (entry_index = 0; entry_index < MR_HANDOFF_MAX; entry_index++)
        if (resource->records[entry_index].state && MRGrant_Equal(&resource->records[entry_index].request.g, &release->grant))
            return leave(resource, SYS_ERR_BUSY);
    for (entry_index = 0; entry_index < MR_RECEIPT_MAX; entry_index++)
        if (resource->receipts[entry_index].occupied && MRGrant_Equal(&resource->receipts[entry_index].value.g, &release->grant))
            return leave(resource, SYS_ERR_BUSY);
    //旧授权必须有真实后端退出证据后才能替换。
    result = resource->source.ops->exit_safe(resource->source.context, &release->grant);
    return leave(resource, result);
}
