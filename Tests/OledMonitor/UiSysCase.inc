static uint32_t UiSysCase(uint32_t id)
{
    OledUiSystemStatus_t system = {0};
    OledUiTaskCursor_t cursor = {0};
    OledUiStatus_t status;
    IpcApplicationView_t ipc = {0};
    OledUiPageState_t state;
    uint32_t now = 120U;
    uint32_t i;

    ipc.peer_state = IPC_PEER_ONLINE;
    system.version = OLED_UI_SYSTEM_VERSION;
    system.session = 1U;
    system.received_ms = 0xfffffff0U;
    system.flags = OLED_UI_SYSTEM_TASKS | OLED_UI_SYSTEM_HEAP;
    system.count = 2U;
    system.total = 2U;
    system.tasks[0].id = 101U;
    memcpy(system.tasks[0].name, "Camera", 7U);
    system.tasks[0].flags = OLED_UI_TASK_STACK;
    system.tasks[0].stack_bytes = 4096U;
    system.tasks[0].hwm_bytes = 64U;
    system.tasks[1].id = 202U;
    memcpy(system.tasks[1].name, "USB", 4U);

    if (id == 83U)
    {
        CHECK(OledUi_PageInit(&state) == SYS_OK);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_OPEN_MENU) == SYS_OK);
        for (i = 0U; i < 5U; i++)
            CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_SELECT) == SYS_OK);
        CHECK(state.current == OLED_UI_PAGE_SYSTEM_MONITOR);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_SELECT) == SYS_OK);
        CHECK(state.current == OLED_UI_PAGE_TASKS);
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_TASKS, &ipc, NULL, NULL, now, &status));
        CHECK(strcmp(status.lines[0], "NOT PUBLISHED") == 0);
        CHECK(OledUi_PageBack(&state) == SYS_OK && state.current == OLED_UI_PAGE_SYSTEM_MONITOR);
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_RUNTIME, &ipc, NULL, NULL, 3200U, &status));
        CHECK(strcmp(status.lines[0], "CM4 UP 3s") == 0);
        CHECK(strcmp(status.lines[1], "CM7 ONLINE") == 0);
        CHECK(strcmp(status.lines[2], "CM7 UP N/A") == 0);
    }
    else if (id == 84U)
    {
        cursor.stack_detail = true;
        CHECK(OledUi_SystemTaskSelect(&system, &cursor, 0) == 0U);
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_TASKS, &ipc, &system, &cursor, now, &status));
        CHECK(strcmp(status.lines[1], "Camera #101") == 0);
        CHECK(strcmp(status.lines[2], "! B 4096 MIN 64 PK 4032") == 0);
        CHECK(OledUi_SystemTaskSelect(&system, &cursor, 1) == 1U && cursor.id == 202U);
        system.tasks[0] = system.tasks[1];
        system.tasks[1].id = 303U;
        CHECK(OledUi_SystemTaskSelect(&system, &cursor, 0) == 0U && cursor.id == 202U);
        system.tasks[0].id = 404U; //旧实例退出，同名新任务不可沿用ID。
        CHECK(OledUi_SystemTaskSelect(&system, &cursor, 0) == 0U && cursor.id == 404U);
        system.session++;
        system.tasks[0].id = 505U;
        CHECK(OledUi_SystemTaskSelect(&system, &cursor, 0) == 0U && cursor.id == 505U);
        system.count = OLED_UI_TASK_CAPACITY;
        for (i = 1U; i < system.count; i++)
            system.tasks[i].id = 100U + i;
        system.total = 19U;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_TASKS, &ipc, &system, &cursor, now, &status));
        CHECK(strcmp(status.lines[0], "TRUNCATED 19") == 0);
        ipc.peer_state = IPC_PEER_TIMEOUT;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_TASKS, &ipc, &system, &cursor, now, &status));
        CHECK(strcmp(status.lines[0], "OFFLINE") == 0 && status.lines[2][0] == 0);
        ipc.peer_state = IPC_PEER_ONLINE;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_TASKS, &ipc, &system, &cursor, 1984U, &status));
        CHECK(strcmp(status.lines[0], "STALE SNAPSHOT") == 0);
        system.count = OLED_UI_TASK_CAPACITY+1U;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_TASKS, &ipc, &system, &cursor, now, &status));
        CHECK(strcmp(status.lines[0], "INVALID SNAPSHOT") == 0);
    }
    else
    {
        system.heap_total = 6000U;
        system.heap_free = 4500U;
        system.heap_min = 2500U;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_MEMORY, &ipc, &system, NULL, now, &status));
        CHECK(strcmp(status.lines[0], "HEAP 4500/6000") == 0);
        CHECK(strcmp(status.lines[1], "MIN 2500 FAIL N/A") == 0);
        CHECK(strcmp(status.lines[2], "SUM PEAK 4032 N1") == 0);
        system.heap_min = 4501U;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_MEMORY, &ipc, &system, NULL, now, &status));
        CHECK(strcmp(status.lines[0], "HEAP N/A/N/A") == 0);
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_CPU, &ipc, &system, NULL, now, &status));
        CHECK(strcmp(status.lines[0], "CPU N/A") == 0);
        system.flags |= OLED_UI_SYSTEM_CPU;
        system.cpu_permille = 753U;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_CPU, &ipc, &system, NULL, now, &status));
        CHECK(strcmp(status.lines[0], "CPU 75%") == 0);
        system.cpu_permille = 1001U;
        CHECK(OledUi_SystemBuild(OLED_UI_PAGE_CPU, &ipc, &system, NULL, now, &status));
        CHECK(strcmp(status.lines[0], "CPU N/A") == 0);
    }
    return 0U;
}
