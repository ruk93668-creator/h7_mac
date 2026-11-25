static uint32_t Ui5Case(uint32_t id)
{
    IpcApplicationView_t view = {0};
    OledUiStatus_t status;
    OledUiStatusCounters_t counters = {27U, 2U, 1U, 123U, true};

    view.peer_state = IPC_PEER_ONLINE;
    view.read_result = SYS_OK;
    view.valid_mask = IPC_APP_CAMERA | IPC_APP_USB;
    view.fresh_mask = IPC_APP_CAMERA | IPC_APP_USB;
    view.application.camera_state = 1U;
    view.application.usb_state = 1U;
    view.application.camera_fps_milli = 30050U;
    view.application.usb_fps_milli = 22500U;
    view.application.width = 640U;
    view.application.height = 480U;
    view.application.format = IPC_IMAGE_YUY2;
    view.application.dropped_frames = 11U;
    view.application.usb_completed_frames = 45U;
    if (id == 79U)
    {
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_CAMERA_STATUS, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "RUN 30.0 FPS") == 0);
        CHECK(strcmp(status.lines[1], "640x480 YUY2") == 0);
        CHECK(strcmp(status.lines[2], "DROP 11") == 0);
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_USB, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "RUN 22.5 FPS") == 0 && strcmp(status.lines[2], "SENT 45") == 0);
        view.fresh_mask = IPC_APP_USB;
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_USB, &view, &counters, &status));
        CHECK(strcmp(status.lines[1], "SOURCE N/A") == 0);
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_CAMERA_STATUS, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "STALE") == 0 && status.lines[1][0] == 0);
        view.valid_mask = 0U;
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_USB, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "NO DATA") == 0);
        view.peer_state = IPC_PEER_TIMEOUT;
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_USB, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "OFFLINE") == 0);
        view.peer_state = IPC_PEER_ONLINE;
        view.read_result = SYS_ERR_IO;
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_USB, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "IPC READ ERROR") == 0);
        view.valid_mask = IPC_APP_USB;
        view.read_result = SYS_OK;
        view.application.usb_result = (-2147483647 - 1);
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_USB, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "USB ERROR -2147483648") == 0);
    }
    else
    {
        CHECK(!OledUi_StatusBuild(OLED_UI_PAGE_MONITOR, &view, &counters, &status));
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_NETWORK, &view, &counters, &status));
        CHECK(status.first_row == 2U && strcmp(status.lines[0], "ETHERNET N/A") == 0);
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_STORAGE_INFO, &view, &counters, &status));
        CHECK(status.first_row == 1U && strcmp(status.lines[0], "SD / RECORDING N/A") == 0);
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_ABOUT, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "M7 OK HB 123") == 0 && strcmp(status.lines[1], "V 3 F 3 IPC 0") == 0);
        counters.peer_heartbeat = UINT32_MAX;
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_ABOUT, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "M7 OK HB 4294967295") == 0);
        counters.heartbeat_valid = false;
        CHECK(OledUi_StatusBuild(OLED_UI_PAGE_ABOUT, &view, &counters, &status));
        CHECK(strcmp(status.lines[0], "M7 OK HB N/A") == 0);
        CHECK(strcmp(status.lines[2], "OK 27 IO 2 R 1") == 0);
    }
    return 0U;
}
