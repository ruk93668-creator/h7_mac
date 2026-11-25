#include "oled_ui_status.h"
#include <stddef.h>
#include <string.h>

//文本只追加到固定行缓存，避免使用需要额外栈空间的格式化库。
static void OledUi_StatusAppend(char *line, const char *text)
{
    uint32_t i = 0U;
    while (i + 1U < OLED_UI_STATUS_LINE_BYTES && line[i] != '\0')
        i++;
    while (i + 1U < OLED_UI_STATUS_LINE_BYTES && *text != '\0')
        line[i++] = *text++;
    line[i] = '\0';
}

static void OledUi_StatusNumber(char *line, uint32_t value)
{
    char text[11];
    uint32_t offset = sizeof(text) - 1U;
    text[offset] = '\0';
    do
    {
        text[--offset] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    OledUi_StatusAppend(line, &text[offset]);
}

static void OledUi_StatusResult(char *line, int32_t result)
{
    if (result < 0)
        OledUi_StatusAppend(line, "-");
    OledUi_StatusNumber(line, result < 0 ? 0U - (uint32_t)result : (uint32_t)result);
}

static const char *OledUi_StatusFormat(uint32_t value)
{
    switch (value)
    {
        case IPC_IMAGE_RGB565: return "RGB565";
        case IPC_IMAGE_YUY2: return "YUY2";
        case IPC_IMAGE_JPEG: return "MJPG";
        case IPC_IMAGE_Y8: return "Y8";
        default: return "FORMAT N/A";
    }
}

static const char *OledUi_StatusPeer(uint32_t peer)
{
    switch (peer)
    {
        case IPC_PEER_ONLINE: return "ONLINE";
        case IPC_PEER_STARTING: return "STARTING";
        case IPC_PEER_PROTOCOL_ERROR: return "PROTOCOL ERROR";
        case IPC_PEER_TIMEOUT:
        case IPC_PEER_STARTUP_TIMEOUT: return "OFFLINE";
        default: return "PEER N/A";
    }
}

static const char *OledUi_StatusAvailability(const IpcApplicationView_t *view, uint32_t source)
{
    if (view->peer_state != IPC_PEER_ONLINE)
        return OledUi_StatusPeer(view->peer_state);
    if (view->read_result != SYS_OK && view->read_result != SYS_ERR_BUSY)
        return "IPC READ ERROR";
    if ((view->valid_mask & source) == 0U)
        return "NO DATA";
    if ((view->fresh_mask & source) == 0U)
        return "STALE";
    return NULL;
}

static void OledUi_StatusCameraDimensions(char *line, const IpcApplicationStatus_t *ipc)
{
    if (ipc->width == 0U || ipc->height == 0U)
        OledUi_StatusAppend(line, "SIZE N/A ");
    else
    {
        OledUi_StatusNumber(line, ipc->width);
        OledUi_StatusAppend(line, "x");
        OledUi_StatusNumber(line, ipc->height);
        OledUi_StatusAppend(line, " ");
    }
    OledUi_StatusAppend(line, OledUi_StatusFormat(ipc->format));
}

static void OledUi_StatusStream(const IpcApplicationView_t *view, bool usb, OledUiStatus_t *status)
{
    const IpcApplicationStatus_t *ipc = &view->application;
    const char *why = OledUi_StatusAvailability(view, usb ? IPC_APP_USB : IPC_APP_CAMERA);
    uint32_t state = usb ? ipc->usb_state : ipc->camera_state;
    uint32_t fps = usb ? ipc->usb_fps_milli : ipc->camera_fps_milli;
    int32_t result = usb ? ipc->usb_result : ipc->camera_result;

    //先判链路、有效性与新鲜度，失效数据不进入数值行。
    if (why != NULL)
    {
        OledUi_StatusAppend(status->lines[0], why);
        return;
    }
    if (result < SYS_OK)
    {
        OledUi_StatusAppend(status->lines[0], usb ? "USB ERROR " : "CAMERA ERROR ");
        OledUi_StatusResult(status->lines[0], result);
        return;
    }
    if (state == 1U)
    {
        OledUi_StatusAppend(status->lines[0], "RUN ");
        OledUi_StatusNumber(status->lines[0], fps / 1000U);
        OledUi_StatusAppend(status->lines[0], ".");
        OledUi_StatusNumber(status->lines[0], fps % 1000U / 100U);
        OledUi_StatusAppend(status->lines[0], " FPS");
    }
    else
        OledUi_StatusAppend(status->lines[0], state == 0U ? "STOPPED" :
            (state == 2U ? "STOPPING" : "STATE N/A"));

    if (usb)
    {
        OledUi_StatusAppend(status->lines[1], "SOURCE ");
        if (OledUi_StatusAvailability(view, IPC_APP_CAMERA) == NULL && ipc->camera_result == SYS_OK)
            OledUi_StatusAppend(status->lines[1], OledUi_StatusFormat(ipc->format));
        else
            OledUi_StatusAppend(status->lines[1], "N/A");
        OledUi_StatusAppend(status->lines[2], "SENT ");
        OledUi_StatusNumber(status->lines[2], ipc->usb_completed_frames);
    }
    else
    {
        OledUi_StatusCameraDimensions(status->lines[1], ipc);
        OledUi_StatusAppend(status->lines[2], "DROP ");
        OledUi_StatusNumber(status->lines[2], ipc->dropped_frames);
    }
}

bool OledUi_StatusBuild(OledUiPageId_t page, const IpcApplicationView_t *view,
    const OledUiStatusCounters_t *counters, OledUiStatus_t *status)
{
    if (view == NULL || counters == NULL || status == NULL)
        return false;
    switch (page)
    {
        case OLED_UI_PAGE_CAMERA_STATUS:
        case OLED_UI_PAGE_CAMERA_FORMAT:
        case OLED_UI_PAGE_USB:
        case OLED_UI_PAGE_NETWORK:
        case OLED_UI_PAGE_STORAGE_INFO:
        case OLED_UI_PAGE_ABOUT:
            break;
        default:
            return false;
    }

    memset(status, 0, sizeof(*status));
    status->first_row = page == OLED_UI_PAGE_NETWORK ? 2U : 1U;
    if (page == OLED_UI_PAGE_NETWORK || page == OLED_UI_PAGE_STORAGE_INFO)
    {
        OledUi_StatusAppend(status->lines[0], page == OLED_UI_PAGE_NETWORK ? "ETHERNET N/A" : "SD / RECORDING N/A");
        OledUi_StatusAppend(status->lines[1], "NOT EXPOSED BY CURRENT IPC");
    }
    else if (page == OLED_UI_PAGE_ABOUT)
    {
        //20字符范围内完整显示最长的十位心跳计数
        if (view->peer_state == IPC_PEER_ONLINE)
            OledUi_StatusAppend(status->lines[0], "M7 OK");
        else
        {
            OledUi_StatusAppend(status->lines[0], "CM7 ");
            OledUi_StatusAppend(status->lines[0], OledUi_StatusPeer(view->peer_state));
        }
        OledUi_StatusAppend(status->lines[0], " HB ");
        if (counters->heartbeat_valid && view->peer_state == IPC_PEER_ONLINE)
            OledUi_StatusNumber(status->lines[0], counters->peer_heartbeat);
        else
            OledUi_StatusAppend(status->lines[0], "N/A");
        OledUi_StatusAppend(status->lines[1], "V ");
        OledUi_StatusNumber(status->lines[1], view->valid_mask & IPC_APP_ALL);
        OledUi_StatusAppend(status->lines[1], " F ");
        OledUi_StatusNumber(status->lines[1], view->fresh_mask & IPC_APP_ALL);
        OledUi_StatusAppend(status->lines[1], " IPC ");
        OledUi_StatusResult(status->lines[1], view->read_result);
        OledUi_StatusAppend(status->lines[2], "OK ");
        OledUi_StatusNumber(status->lines[2], counters->completed);
        OledUi_StatusAppend(status->lines[2], " IO ");
        OledUi_StatusNumber(status->lines[2], counters->io_errors);
        OledUi_StatusAppend(status->lines[2], " R ");
        OledUi_StatusNumber(status->lines[2], counters->render_errors);
    }
    else
        OledUi_StatusStream(view, page == OLED_UI_PAGE_USB, status);
    return true;
}

//快照先检查固定容量和生成标识，任何异常都不使用其中的部分字段。
static bool OledUi_SystemValid(const OledUiSystemStatus_t *system)
{
    uint32_t i, j;
    if (system == NULL || system->version != OLED_UI_SYSTEM_VERSION || system->session == 0U ||
        system->count > OLED_UI_TASK_CAPACITY || system->count > system->total ||
        ((system->flags & OLED_UI_SYSTEM_TASKS) == 0U && system->count != 0U))
        return false;

    for (i = 0U; i < system->count; i++)
    {
        if (system->tasks[i].id == 0U)
            return false;
        for (j = 0U; j < i; j++)
            if (system->tasks[j].id == system->tasks[i].id)
                return false;
    }
    return true;
}

//按会话和任务ID恢复选中项并限制索引
uint8_t OledUi_SystemTaskSelect(const OledUiSystemStatus_t *system, OledUiTaskCursor_t *cursor,
    int32_t direction)
{
    uint32_t i, selected;
    if (cursor == NULL || !OledUi_SystemValid(system) || system->count == 0U)
        return 0U;

    selected = cursor->session == system->session ? cursor->index : 0U;
    if (selected >= system->count)
        selected = system->count - 1U;
    if (cursor->session == system->session)
        for (i = 0U; i < system->count; i++)
            if (system->tasks[i].id == cursor->id)
            {
                selected = i;
                break;
            }
    if (direction > 0 && selected + 1U < system->count)
        selected++;
    if (direction < 0 && selected != 0U)
        selected--;
    cursor->index = (uint8_t)selected;
    cursor->id = system->tasks[selected].id;
    cursor->session = system->session;
    return cursor->index;
}

static void OledUi_SystemValue(char *line, const char *label, bool valid, uint32_t value)
{
    OledUi_StatusAppend(line, label);
    if (valid)
        OledUi_StatusNumber(line, value);
    else
        OledUi_StatusAppend(line, "N/A");
}

static bool OledUi_SystemStackValid(const OledUiTaskStatus_t *task)
{
    return (task->flags & OLED_UI_TASK_STACK) != 0U && task->stack_bytes != 0U &&
        task->hwm_bytes <= task->stack_bytes;
}

static void OledUi_SystemTask(const OledUiSystemStatus_t *system,
    const OledUiTaskCursor_t *cursor, OledUiStatus_t *status)
{
    static const char * const names[] = {"RUN", "READY", "BLOCK", "SUSP", "EXIT"};
    OledUiTaskCursor_t current = {0};
    const OledUiTaskStatus_t *task;
    uint32_t i, shown = 0U;
    char name[17];

    OledUi_SystemValue(status->lines[0],
        system->total > system->count ? "TRUNCATED " : "TASKS ", true, system->total);
    if (system->count == 0U)
        return;
    if (cursor != NULL)
        current = *cursor;
    task = &system->tasks[OledUi_SystemTaskSelect(system, &current, 0)];
    for (i = 0U; i < sizeof(task->name) && task->name[i] != '\0'; i++)
    {
        unsigned char ch = (unsigned char)task->name[i];
        name[shown++] = ch >= 32U && ch <= 126U ? (char)ch : '?';
    }
    name[shown] = '\0';
    OledUi_StatusAppend(status->lines[1], name);
    OledUi_SystemValue(status->lines[1], " #", true, task->id);

    if (current.stack_detail)
    {
        bool valid = OledUi_SystemStackValid(task);
        if (valid && (task->hwm_bytes < 128U || task->hwm_bytes <= task->stack_bytes / 10U))
            OledUi_StatusAppend(status->lines[2], "! ");
        OledUi_SystemValue(status->lines[2], "B ", valid, task->stack_bytes);
        OledUi_SystemValue(status->lines[2], " MIN ", valid, task->hwm_bytes);
        OledUi_SystemValue(status->lines[2], " PK ", valid,
            valid ? task->stack_bytes - task->hwm_bytes : 0U);
    }
    else
    {
        OledUi_StatusAppend(status->lines[2], task->state < sizeof(names) / sizeof(names[0]) ?
            names[task->state] : "STATE N/A");
        OledUi_SystemValue(status->lines[2], " P", true, task->priority);
        OledUi_SystemValue(status->lines[2], " CPU ", (task->flags & OLED_UI_TASK_CPU) != 0U &&
            task->cpu_permille <= 1000U, task->cpu_permille / 10U);
        if ((task->flags & OLED_UI_TASK_CPU) != 0U && task->cpu_permille <= 1000U)
            OledUi_StatusAppend(status->lines[2], "%");
    }
}

//根据会话、时效和有效位组织系统诊断文本
bool OledUi_SystemBuild(OledUiPageId_t page, const IpcApplicationView_t *ipc,
    const OledUiSystemStatus_t *system, const OledUiTaskCursor_t *cursor,
    uint32_t now, OledUiStatus_t *status)
{
    bool valid, heap;
    const char *issue = NULL;
    uint32_t i, total = 0U, peak = 0U;
    if (ipc == NULL || status == NULL || page < OLED_UI_PAGE_TASKS || page > OLED_UI_PAGE_RUNTIME)
        return false;

    memset(status, 0, sizeof(*status));
    status->first_row = 1U;
    if (ipc->peer_state != IPC_PEER_ONLINE)
        issue = OledUi_StatusPeer(ipc->peer_state);
    else if (ipc->read_result != SYS_OK && ipc->read_result != SYS_ERR_BUSY)
        issue = "IPC READ ERROR";
    else if (!OledUi_SystemValid(system))
        issue = system == NULL ? "NOT PUBLISHED" : "INVALID SNAPSHOT";
    else if (now - system->received_ms >= OLED_UI_SYSTEM_MAX_AGE_MS)
        issue = "STALE SNAPSHOT";
    valid = issue == NULL;

    if (page == OLED_UI_PAGE_RUNTIME)
    {
        OledUi_SystemValue(status->lines[0], "CM4 UP ", true, now / 1000U);
        OledUi_StatusAppend(status->lines[0], "s");
        OledUi_StatusAppend(status->lines[1], "CM7 ");
        OledUi_StatusAppend(status->lines[1], OledUi_StatusPeer(ipc->peer_state));
        OledUi_SystemValue(status->lines[2], "CM7 UP ",
            valid && (system->flags & OLED_UI_SYSTEM_UPTIME) != 0U,
            valid ? system->uptime_ms / 1000U : 0U);
        return true;
    }
    if (!valid)
    {
        OledUi_StatusAppend(status->lines[0], issue);
        OledUi_StatusAppend(status->lines[1], page == OLED_UI_PAGE_TASKS ? "TASKS N/A" :
            (page == OLED_UI_PAGE_CPU ? "CPU N/A" : "RTOS HEAP N/A"));
        return true;
    }
    if (page == OLED_UI_PAGE_TASKS)
    {
        if ((system->flags & OLED_UI_SYSTEM_TASKS) != 0U)
            OledUi_SystemTask(system, cursor, status);
        else
            OledUi_StatusAppend(status->lines[0], "TASKS N/A");
    }
    else if (page == OLED_UI_PAGE_CPU)
    {
        valid = (system->flags & OLED_UI_SYSTEM_CPU) != 0U && system->cpu_permille <= 1000U;
        OledUi_SystemValue(status->lines[0], "CPU ", valid, system->cpu_permille / 10U);
        if (valid)
            OledUi_StatusAppend(status->lines[0], "%");
        OledUi_StatusAppend(status->lines[1], "TASK CPU IN TASKS");
    }
    else
    {
        heap = (system->flags & OLED_UI_SYSTEM_HEAP) != 0U && system->heap_min <= system->heap_free &&
            system->heap_free <= system->heap_total;
        OledUi_SystemValue(status->lines[0], "HEAP ", heap, system->heap_free);
        OledUi_SystemValue(status->lines[0], "/", heap, system->heap_total);
        OledUi_SystemValue(status->lines[1], "MIN ", heap, system->heap_min);
        OledUi_SystemValue(status->lines[1], " FAIL ", (system->flags & OLED_UI_SYSTEM_ALLOC_FAILURES) != 0U,
            system->alloc_failures);
        for (i = 0U; i < system->count; i++)
            if (OledUi_SystemStackValid(&system->tasks[i]))
            {
                uint32_t used = system->tasks[i].stack_bytes - system->tasks[i].hwm_bytes;
                peak = peak > UINT32_MAX - used ? UINT32_MAX : peak + used;
                total++;
            }
        OledUi_SystemValue(status->lines[2], "SUM PEAK ", total != 0U, peak);
        OledUi_SystemValue(status->lines[2], " N", total != 0U, total);
    }
    return true;
}
