#include "ipc_core.h"
#include <stddef.h>

#if defined(CORE_CM7) == defined(CORE_CM4)
#error IPC_requires_exactly_one_core_role
#endif

#if defined(CORE_CM7)
#define IPC_LOCAL_OWNER IPC_OWNER_CM7
#define IPC_PEER_OWNER  IPC_OWNER_CM4
#else
#define IPC_LOCAL_OWNER IPC_OWNER_CM4
#define IPC_PEER_OWNER  IPC_OWNER_CM7
#endif

//测试只在真实访问点插入交错，正式构建没有钩子
#if defined(IPC_CORE_TEST)
extern void IPC_TestAfterAccess(uint32_t operation, uint32_t word);
#define IPC_ACCESS(operation, word) IPC_TestAfterAccess(operation, word)
#else
#define IPC_ACCESS(operation, word) ((void)0)
#endif

//每个ELF只有一份本地上下文，指针和函数表不进入共享内存
static struct
{

    //保存共享窗口地址，只由所属核心更新自己的状态块
    volatile IpcShared_t *shared;

    //保存本核屏障和临界区接口，不引入RTOS任务类型
    const IpcCorePort_t *port;

    //记录端口已绑定，防止后续替换共享对象
    bool bound;

    //初始化发布仅在窗口内开放，运行期发布另行检查唯一执行者
    bool initialization_open;

    //记录本核状态块已经完成首次构造
    bool local_initialized;

    //只保护本核重入，不把关中断当成跨核锁
    bool writer_busy;

    //只保存在本核，授权回调在初始化窗口关闭后不可替换
    bool (*runtime_owner)(void *context);

    //保存唯一执行者校验所需的本核上下文
    void *runtime_context;

    //保存本地操作诊断，不向未知共享布局写入错误
    IpcDiagnostics_t diagnostics;
} s_core;

//保存本次结果和本地累计故障
static SysResult_t Ipc_Result(SysResult_t result, uint32_t faults);

//检查本地参数对齐、地址溢出及与整个4KiB窗口重叠
static bool Ipc_LocalMemoryValid(const void *memory, uint32_t bytes);

//检查协议头，失败时不访问状态块
static SysResult_t Ipc_CheckHeader(void);

//取得编译目标所属的状态块
static volatile IpcStatusBlock_t *Ipc_LocalBlock(void);

//取得另一个核心的状态块
static volatile IpcStatusBlock_t *Ipc_PeerBlock(void);

//申请本核初始化发布权
static SysResult_t Ipc_BeginWrite(bool runtime);

//复用同一份字段校验和序号提交，只区分初始化与运行期授权
static SysResult_t Ipc_Publish(const IpcState_t *state, bool runtime,
    const IpcApplicationStatus_t *application);

//释放本核发布权
static void Ipc_EndWrite(void);

//检查待发布的固定大小状态
static bool Ipc_StateValid(const IpcState_t *state);

//以对齐字段访问写入整块，sequence最后提交
static void Ipc_WriteBlock(volatile IpcStatusBlock_t *block,
    const IpcState_t *state, uint32_t committed_sequence,
    const IpcApplicationStatus_t *application);

//有限重试读取指定块，成功后才填写输出
static SysResult_t Ipc_ReadBlock(volatile IpcStatusBlock_t *block,
    uint32_t owner, IpcStatusSnapshot_t *snapshot);

//绑定唯一共享对象与静态端口
SysResult_t IPC_Core_Bind(volatile IpcShared_t *shared, const IpcCorePort_t *port)
{
    uintptr_t address = (uintptr_t)shared;
    uintptr_t port_address = (uintptr_t)port;

    if (s_core.bound)
        return Ipc_Result(SYS_ERR_ALREADY_EXISTS, 0U);

    //先校验地址，再访问参数内容，绑定错误不得修改共享区
    if (shared == NULL || (address & 31U) != 0U ||
        address > UINTPTR_MAX - (IPC_SHARED_WINDOW_BYTES - 1U) || port == NULL ||
        (port_address & 3U) != 0U || port_address > UINTPTR_MAX - (sizeof(*port) - 1U))
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);

    //端口表不能位于共享窗口，避免被另一核改写执行入口
    if (address <= port_address + sizeof(*port) - 1U &&
        port_address <= address + IPC_SHARED_WINDOW_BYTES - 1U)
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);

    //屏障和临界区接口必须齐全，不能省略一致性保护
    if (port->barrier == NULL || port->enter == NULL ||
        port->leave == NULL || port->is_thread == NULL)
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);

    //绑定只允许在正常线程上下文进行
    if (!port->is_thread())
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);

    //本地保存端口，后续接口不能再换成另一个对象或另一核角色
    s_core.shared = shared;
    s_core.port = port;
    s_core.initialization_open = true;
    s_core.bound = true;
    return Ipc_Result(SYS_OK, 0U);
}

#if defined(CORE_CM7)

//CM4保持原STOP等待时建立当前协议，不清零对端块
SysResult_t IPC_Core_ColdStart(void)
{
    uint32_t i;
    const IpcState_t initial = {IPC_CORE_NOT_READY, 0U, 0U, SYS_OK};
    SysResult_t result = Ipc_BeginWrite(false);

    if (result != SYS_OK)
        return result;
    if (s_core.local_initialized)
    {
        Ipc_EndWrite();
        return Ipc_Result(SYS_ERR_ALREADY_EXISTS, 0U);
    }

    //这是受控冷启动构造入口，普通读写接口不能重建未知协议
    s_core.shared->header.magic = 0U;
    s_core.port->barrier();
    s_core.shared->header.version = IPC_VERSION;
    s_core.shared->header.layout_bytes = IPC_LAYOUT_BYTES;
    s_core.shared->header.header_bytes = IPC_HEADER_BYTES;
    s_core.shared->header.status_block_bytes = IPC_STATUS_BYTES;
    s_core.shared->header.feature_flags = IPC_FEATURE_FLAGS;
    for (i = 0U; i < 10U; i++)
        s_core.shared->header.reserved[i] = 0U;

    //只初始化M7块，M4在验证协议后自己初始化M4块
    Ipc_WriteBlock(Ipc_LocalBlock(), &initial, 2U, NULL);
    s_core.local_initialized = true;
    s_core.port->barrier();
    s_core.shared->header.magic = IPC_MAGIC;
    s_core.port->barrier();
    Ipc_EndWrite();
    return Ipc_Result(SYS_OK, 0U);
}
#endif

//验证协议后初始化本核状态块
SysResult_t IPC_Core_InitializeLocal(const IpcState_t *state)
{
    SysResult_t result;

    if (!Ipc_LocalMemoryValid(state, sizeof(*state)))
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);
    result = Ipc_BeginWrite(false);
    if (result != SYS_OK)
        return result;
    if (s_core.local_initialized)
        result = SYS_ERR_ALREADY_EXISTS;
    else
    {
        result = Ipc_CheckHeader();
        if (result == SYS_OK && !Ipc_StateValid(state))
            result = SYS_ERR_INVALID_ARG;
        if (result == SYS_OK)
        {

            //UNINIT中的旧sequence不可直接使用，冷启动由所属核首次构造
            Ipc_WriteBlock(Ipc_LocalBlock(), state, 2U, NULL);
            s_core.local_initialized = true;
        }
    }
    Ipc_EndWrite();
    return Ipc_Result(result, 0U);
}

//通过对应授权路径提交本核状态
static SysResult_t Ipc_Publish(const IpcState_t *state, bool runtime,
    const IpcApplicationStatus_t *application)
{
    volatile IpcStatusBlock_t *block;
    uint32_t sequence;
    SysResult_t result;

    if (!Ipc_LocalMemoryValid(state, sizeof(*state)))
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);

    //附带业务载荷时先校验本地内存及协议字段
    if (application != NULL && (!Ipc_LocalMemoryValid(application, sizeof(*application)) ||
        !IPC_Application_Validate(application)))
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);

    //先取得本核发布权，防止未授权调用或发布重入
    result = Ipc_BeginWrite(runtime);
    if (result != SYS_OK)
        return result;
    result = Ipc_CheckHeader();

    //状态块尚未构造时，不能直接开始递增序号发布
    if (result == SYS_OK && !s_core.local_initialized)
        result = SYS_ERR_INVALID_STATE;
    if (result == SYS_OK && !Ipc_StateValid(state))
        result = SYS_ERR_INVALID_ARG;
    if (result == SYS_OK)
    {
        block = Ipc_LocalBlock();
        sequence = block->sequence;

        //确认块归属和长度，禁止覆盖其他核心或未知布局
        if (block->owner_id != IPC_LOCAL_OWNER || block->block_bytes != IPC_STATUS_BYTES)
            result = Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);

        //零序号表示尚未提交，不能作为更新基线
        else if (sequence == 0U)
            result = SYS_ERR_INVALID_STATE;

        //奇数表示已有更新未完成，此时拒绝叠加写入
        else if ((sequence & 1U) != 0U)
            result = SYS_ERR_BUSY;

        //提交序号接近上限时拒绝复用旧值，避免读者误认同一版本
        else if (sequence >= UINT32_MAX - 1U)
            result = SYS_ERR_NO_RESOURCE;
        else
        {

            //拒绝回绕，不复用可能仍被读者持有的旧提交序号
            Ipc_WriteBlock(block, state, sequence + 2U, application);
        }
    }
    Ipc_EndWrite();
    return Ipc_Result(result, 0U);
}

//结束Service初始化写入窗口
SysResult_t IPC_Core_EndInitialization(void)
{
    uint32_t mask;

    if (!s_core.bound || !s_core.port->is_thread() || !s_core.local_initialized)
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    mask = s_core.port->enter();
    if (s_core.writer_busy)
    {
        s_core.port->leave(mask);

        //本核发布尚未结束，不能提前关闭它正在使用的初始化窗口
        return Ipc_Result(SYS_ERR_BUSY, 0U);
    }
    if (!s_core.initialization_open)
    {
        s_core.port->leave(mask);
        return Ipc_Result(SYS_ERR_ALREADY_EXISTS, 0U);
    }
    s_core.initialization_open = false;
    s_core.port->leave(mask);
    return Ipc_Result(SYS_OK, 0U);
}

//读取本核完整快照
SysResult_t IPC_Core_ReadLocal(IpcStatusSnapshot_t *snapshot)
{
    if (!s_core.bound)
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    return Ipc_ReadBlock(Ipc_LocalBlock(), IPC_LOCAL_OWNER, snapshot);
}

//读取对端完整快照
SysResult_t IPC_Core_ReadPeer(IpcStatusSnapshot_t *snapshot)
{
    if (!s_core.bound)
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    return Ipc_ReadBlock(Ipc_PeerBlock(), IPC_PEER_OWNER, snapshot);
}

//读取本地诊断，保留最近一次操作结果
SysResult_t IPC_Core_GetDiagnostics(IpcDiagnostics_t *diagnostics)
{
    if (!Ipc_LocalMemoryValid(diagnostics, sizeof(*diagnostics)))
        return SYS_ERR_INVALID_ARG;

    //诊断也是本地多字段副本，短暂保护，避免任务切换造成混读
    if (s_core.bound)
    {
        uint32_t mask = s_core.port->enter();
        diagnostics->fault_flags = s_core.diagnostics.fault_flags;
        diagnostics->last_result = s_core.diagnostics.last_result;
        s_core.port->leave(mask);
    }
    else
    {
        diagnostics->fault_flags = s_core.diagnostics.fault_flags;
        diagnostics->last_result = s_core.diagnostics.last_result;
    }
    return SYS_OK;
}

//保存本地操作结果，不在这里写共享故障字段
static SysResult_t Ipc_Result(SysResult_t result, uint32_t faults)
{

    //多个读取调用者可能交错，仅保护本地诊断，不锁另一核
    if (s_core.bound)
    {
        uint32_t mask = s_core.port->enter();
        s_core.diagnostics.last_result = result;
        s_core.diagnostics.fault_flags |= faults;
        s_core.port->leave(mask);
    }
    else
    {
        s_core.diagnostics.last_result = result;
        s_core.diagnostics.fault_flags |= faults;
    }
    return result;
}

//校验普通内存参数，连同4KiB窗口内的预留空间一起保护
static bool Ipc_LocalMemoryValid(const void *memory, uint32_t bytes)
{
    uintptr_t address = (uintptr_t)memory;
    uintptr_t shared = (uintptr_t)s_core.shared;

    //先排除非法地址及整数溢出，后续才能安全检查区间重叠
    if (memory == NULL || bytes == 0U || (address & 3U) != 0U ||
        address > UINTPTR_MAX - (bytes - 1U))
        return false;

    //本地参数不得覆盖共享窗口，包括尚未使用的预留区域
    if (s_core.bound && address <= shared + IPC_SHARED_WINDOW_BYTES - 1U &&
        shared <= address + bytes - 1U)
        return false;
    return true;
}

//验证固定协议头，不根据未知布局寻找状态块
static SysResult_t Ipc_CheckHeader(void)
{
    uint32_t i;
    volatile IpcHeader_t *header = &s_core.shared->header;

    //Magic不匹配时立即拒绝，不解释后续未知状态块
    if (header->magic != IPC_MAGIC)
        return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);
    s_core.port->barrier();

    //版本不匹配单独报错，禁止按当前布局读取旧固件数据
    if (header->version != IPC_VERSION)
        return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_VERSION_MISMATCH);

    //总长度、块长度和功能位必须全部匹配，防止协议误读
    if (header->layout_bytes != IPC_LAYOUT_BYTES || header->header_bytes != IPC_HEADER_BYTES ||
        header->status_block_bytes != IPC_STATUS_BYTES || header->feature_flags != IPC_FEATURE_FLAGS)
        return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);

    //预留字段必须为零，不能暗中携带未声明的协议扩展
    for (i = 0U; i < 10U; i++)
    {
        if (header->reserved[i] != 0U)
            return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);
    }

    //冷启动发布期间仍可能看到无效头，末尾再次核对固定字段
    s_core.port->barrier();
    if (header->magic != IPC_MAGIC || header->version != IPC_VERSION ||
        header->layout_bytes != IPC_LAYOUT_BYTES || header->header_bytes != IPC_HEADER_BYTES ||
        header->status_block_bytes != IPC_STATUS_BYTES || header->feature_flags != IPC_FEATURE_FLAGS)
        return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);
    return SYS_OK;
}

//选择本核唯一可写目标
static volatile IpcStatusBlock_t *Ipc_LocalBlock(void)
{
#if defined(CORE_CM7)
    return &s_core.shared->m7;
#else
    return &s_core.shared->m4;
#endif
}

//选择对端只读目标
static volatile IpcStatusBlock_t *Ipc_PeerBlock(void)
{
#if defined(CORE_CM7)
    return &s_core.shared->m4;
#else
    return &s_core.shared->m7;
#endif
}

//检查初始化或运行期授权，并保护本核发布重入
static SysResult_t Ipc_BeginWrite(bool runtime)
{
    uint32_t mask;

    if (!s_core.bound || !s_core.port->is_thread())
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);

    //身份检查必须在屏蔽调度前执行，避免合法任务被PRIMASK条件误拒绝
    if (runtime && (s_core.initialization_open || s_core.runtime_owner == NULL ||
        !s_core.runtime_owner(s_core.runtime_context)))
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    mask = s_core.port->enter();
    if ((!runtime && !s_core.initialization_open) ||
        (runtime && s_core.initialization_open))
    {
        s_core.port->leave(mask);
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    }
    if (s_core.writer_busy)
    {
        s_core.port->leave(mask);
        return Ipc_Result(SYS_ERR_BUSY, 0U);
    }
    s_core.writer_busy = true;
    s_core.port->leave(mask);
    return SYS_OK;
}

//释放本核发布重入保护
static void Ipc_EndWrite(void)
{
    uint32_t mask = s_core.port->enter();

    s_core.writer_busy = false;
    s_core.port->leave(mask);
}

//检查状态值，不把正数伪装成SysResult错误码
static bool Ipc_StateValid(const IpcState_t *state)
{
    return state->ready <= IPC_CORE_READY && state->last_result <= SYS_OK;
}

//逐字段提交本核块，避免普通memcpy绕过volatile访问
static void Ipc_WriteBlock(volatile IpcStatusBlock_t *block,
    const IpcState_t *state, uint32_t committed_sequence,
    const IpcApplicationStatus_t *application)
{
    uint32_t i;
    IpcState_t value = *state;
    const IpcApplicationStatus_t empty = {0};
    IpcApplicationStatus_t data = application != NULL ? *application : empty;

    //奇数先发布，读者在完整提交前只能重试
    block->sequence = committed_sequence - 1U;
    IPC_ACCESS(1U, 0U);
    s_core.port->barrier();
    block->owner_id = IPC_LOCAL_OWNER;
    IPC_ACCESS(1U, 1U);
    block->block_bytes = IPC_STATUS_BYTES;
    IPC_ACCESS(1U, 2U);
    block->ready = value.ready;
    IPC_ACCESS(1U, 3U);
    block->heartbeat = value.heartbeat;
    IPC_ACCESS(1U, 4U);
    block->fault_flags = value.fault_flags;
    IPC_ACCESS(1U, 5U);
    block->last_result = value.last_result;
    IPC_ACCESS(1U, 6U);

    //写入来源位和业务状态，与心跳处于同一提交序号内
    block->application.valid_mask = data.valid_mask;
    IPC_ACCESS(1U, 7U);
    block->application.camera_state = data.camera_state;
    IPC_ACCESS(1U, 8U);
    block->application.usb_state = data.usb_state;
    IPC_ACCESS(1U, 9U);
    block->application.control_state = data.control_state;
    IPC_ACCESS(1U, 10U);

    //写入实际测量的FPS与帧计数，不使用目标配置冒充实测
    block->application.camera_fps_milli = data.camera_fps_milli;
    IPC_ACCESS(1U, 11U);
    block->application.usb_fps_milli = data.usb_fps_milli;
    IPC_ACCESS(1U, 12U);
    block->application.captured_frames = data.captured_frames;
    IPC_ACCESS(1U, 13U);
    block->application.dropped_frames = data.dropped_frames;
    IPC_ACCESS(1U, 14U);
    block->application.usb_completed_frames = data.usb_completed_frames;
    IPC_ACCESS(1U, 15U);

    //写入实际图像配置，使接收方能解释对应运行状态
    block->application.width = data.width;
    IPC_ACCESS(1U, 16U);
    block->application.height = data.height;
    IPC_ACCESS(1U, 17U);
    block->application.format = data.format;
    IPC_ACCESS(1U, 18U);

    //保留历史故障与各模块最近操作结果，两者含义不同
    block->application.last_fault = data.last_fault;
    IPC_ACCESS(1U, 19U);
    block->application.camera_result = data.camera_result;
    IPC_ACCESS(1U, 20U);
    block->application.usb_result = data.usb_result;
    IPC_ACCESS(1U, 21U);
    block->application.control_result = data.control_result;
    IPC_ACCESS(1U, 22U);

    //传递来源年龄而非绝对Tick，避免跨核时钟比较
    block->application.camera_age_ms = data.camera_age_ms;
    IPC_ACCESS(1U, 23U);
    block->application.usb_age_ms = data.usb_age_ms;
    IPC_ACCESS(1U, 24U);
    block->application.control_age_ms = data.control_age_ms;
    IPC_ACCESS(1U, 25U);
    block->application.snapshot_id = data.snapshot_id;
    IPC_ACCESS(1U, 26U);

    //预留区每次明确清零，不把历史残留当成业务字段
    for (i = 0U; i < 5U; i++)
    {
        block->reserved[i] = 0U;
        IPC_ACCESS(1U, 27U + i);
    }

    //完整数据排在偶数提交之前，末尾屏障也用于后续启动通知
    s_core.port->barrier();
    block->sequence = committed_sequence;
    IPC_ACCESS(2U, 0U);
    s_core.port->barrier();
}

//有限重试读取整块，绝不把更新中的局部结果交付出去
static SysResult_t Ipc_ReadBlock(volatile IpcStatusBlock_t *block,
    uint32_t owner, IpcStatusSnapshot_t *snapshot)
{
    IpcStatusSnapshot_t value;
    uint32_t first, second, attempt, i;
    SysResult_t result;

    if (!Ipc_LocalMemoryValid(snapshot, sizeof(*snapshot)))
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);
    if (!s_core.port->is_thread())
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    result = Ipc_CheckHeader();
    if (result != SYS_OK)
        return result;
    for (attempt = 0U; attempt < IPC_READ_ATTEMPTS; attempt++)
    {

        //首次读取提交序号，未提交或更新中时只重试
        first = block->sequence;
        IPC_ACCESS(3U, 0U);
        if (first == 0U || (first & 1U) != 0U)
            continue;
        s_core.port->barrier();
        value.sequence = first;
        value.owner_id = block->owner_id;
        IPC_ACCESS(3U, 1U);
        value.block_bytes = block->block_bytes;
        IPC_ACCESS(3U, 2U);
        value.ready = block->ready;
        IPC_ACCESS(3U, 3U);
        value.heartbeat = block->heartbeat;
        IPC_ACCESS(3U, 4U);
        value.fault_flags = block->fault_flags;
        IPC_ACCESS(3U, 5U);
        value.last_result = block->last_result;
        IPC_ACCESS(3U, 6U);

        //逐字段复制来源与状态，保留对齐volatile访问顺序
        value.application.valid_mask = block->application.valid_mask;
        IPC_ACCESS(3U, 7U);
        value.application.camera_state = block->application.camera_state;
        IPC_ACCESS(3U, 8U);
        value.application.usb_state = block->application.usb_state;
        IPC_ACCESS(3U, 9U);
        value.application.control_state = block->application.control_state;
        IPC_ACCESS(3U, 10U);

        //复制FPS和帧计数，此时仍不能向调用者交付
        value.application.camera_fps_milli = block->application.camera_fps_milli;
        IPC_ACCESS(3U, 11U);
        value.application.usb_fps_milli = block->application.usb_fps_milli;
        IPC_ACCESS(3U, 12U);
        value.application.captured_frames = block->application.captured_frames;
        IPC_ACCESS(3U, 13U);
        value.application.dropped_frames = block->application.dropped_frames;
        IPC_ACCESS(3U, 14U);
        value.application.usb_completed_frames = block->application.usb_completed_frames;
        IPC_ACCESS(3U, 15U);

        //复制图像配置，随后与来源字段一起校验
        value.application.width = block->application.width;
        IPC_ACCESS(3U, 16U);
        value.application.height = block->application.height;
        IPC_ACCESS(3U, 17U);
        value.application.format = block->application.format;
        IPC_ACCESS(3U, 18U);

        //复制故障与操作结果，保留各自独立语义
        value.application.last_fault = block->application.last_fault;
        IPC_ACCESS(3U, 19U);
        value.application.camera_result = block->application.camera_result;
        IPC_ACCESS(3U, 20U);
        value.application.usb_result = block->application.usb_result;
        IPC_ACCESS(3U, 21U);
        value.application.control_result = block->application.control_result;
        IPC_ACCESS(3U, 22U);

        //复制来源年龄和采样编号，用于本地新鲜度判断
        value.application.camera_age_ms = block->application.camera_age_ms;
        IPC_ACCESS(3U, 23U);
        value.application.usb_age_ms = block->application.usb_age_ms;
        IPC_ACCESS(3U, 24U);
        value.application.control_age_ms = block->application.control_age_ms;
        IPC_ACCESS(3U, 25U);
        value.application.snapshot_id = block->application.snapshot_id;
        IPC_ACCESS(3U, 26U);
        for (i = 0U; i < 5U; i++)
        {
            value.reserved[i] = block->reserved[i];
            IPC_ACCESS(3U, 27U + i);
        }
        s_core.port->barrier();

        //复制后再次读序号，确认期间没有被写者更新
        second = block->sequence;
        IPC_ACCESS(4U, 0U);

        //首尾不一致或仍为奇数时，丢弃本次局部副本
        if (first != second || (second & 1U) != 0U)
            continue;
        result = Ipc_CheckHeader();
        if (result != SYS_OK)
            return result;

        //提交完整也必须检查归属、大小和状态值，防止交付非法协议数据
        if (value.owner_id != owner || value.block_bytes != IPC_STATUS_BYTES ||
            value.ready > IPC_CORE_READY || value.last_result > SYS_OK)
            return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);
        for (i = 0U; i < 5U; i++)
        {
            if (value.reserved[i] != 0U)
                return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);
        }

        //只有本地副本经过全部校验后，才更新调用者提供的普通内存
        if (!IPC_Application_Validate(&value.application) ||
            (owner == IPC_OWNER_CM4 && !IPC_Application_IsEmpty(&value.application)))
            return Ipc_Result(SYS_ERR_PROTOCOL, IPC_FAULT_SHARED_INVALID);

        //全部校验成功后才写输出，失败不会覆盖调用者原有快照
        *snapshot = value;
        return Ipc_Result(SYS_OK, 0U);
    }

    //有限重试仍未取得稳定序号时返回BUSY，不无限等待另一核
    return Ipc_Result(SYS_ERR_BUSY, 0U);
}

//发布初始化状态，不因增加运行期能力而重新打开初始化窗口
SysResult_t IPC_Core_PublishLocal(const IpcState_t *state)
{
    return Ipc_Publish(state, false, NULL);
}

//发布运行期状态，每次调用都验证唯一执行者
SysResult_t IPC_Core_PublishRuntime(const IpcState_t *state)
{
    return Ipc_Publish(state, true, NULL);
}

//在初始化阶段冻结运行期发布者
SysResult_t IPC_Core_ConfigureRuntime(bool (*is_owner)(void *context), void *context)
{
    uint32_t mask;

    //授权只接受本核静态回调，未知布局或关闭窗口都不能重新授权
    if (is_owner == NULL)
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);
    if (!s_core.bound || !s_core.local_initialized || !s_core.port->is_thread())
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);

    //授权上下文也必须留在本核普通内存，不接受共享区中的可变身份数据
    if (context != NULL && (uintptr_t)context >= (uintptr_t)s_core.shared &&
        (uintptr_t)context < (uintptr_t)s_core.shared + IPC_SHARED_WINDOW_BYTES)
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);
    if (Ipc_CheckHeader() != SYS_OK)
        return s_core.diagnostics.last_result;
    mask = s_core.port->enter();
    if (!s_core.initialization_open || s_core.writer_busy)
    {
        s_core.port->leave(mask);
        return Ipc_Result(SYS_ERR_INVALID_STATE, 0U);
    }
    if (s_core.runtime_owner != NULL)
    {
        s_core.port->leave(mask);
        return Ipc_Result(SYS_ERR_ALREADY_EXISTS, 0U);
    }
    s_core.runtime_owner = is_owner;
    s_core.runtime_context = context;
    s_core.port->leave(mask);
    return Ipc_Result(SYS_OK, 0U);
}

#if defined(CORE_CM7)

//由唯一IPC Task将心跳与业务数据作为同一份快照提交
SysResult_t IPC_Core_PublishRuntimeApplication(const IpcState_t *state,
    const IpcApplicationStatus_t *application)
{
    if (application == NULL)
        return Ipc_Result(SYS_ERR_INVALID_ARG, 0U);
    return Ipc_Publish(state, true, application);
}
#endif
