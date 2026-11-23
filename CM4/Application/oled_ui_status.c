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
        OledUi_StatusAppend(status->lines[0], "CM7 ");
        OledUi_StatusAppend(status->lines[0], OledUi_StatusPeer(view->peer_state));
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
