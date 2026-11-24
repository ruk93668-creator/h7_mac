#if APP_V3_RECORD_BOARD_TEST
volatile AppV3RecordBoardCommand_t g_app_v3_record_command;
volatile AppV3RecordBoardStatus_t g_app_v3_record_board;
static uint32_t s_record_board_started;
#if APP_V3_JOINT_BOARD_TEST
volatile uint32_t g_app_v3_joint_time_ms;
#endif

static SysResult_t RecordBoard_Dispatch(uint32_t command, uint32_t partition, uint32_t file, uint32_t active)
{
    switch (command)
    {
        case 1U: return AppV3Supervisor_Set(APP_V3_PIPE_CAMERA, 0U);
        case 2U:
            //只接受用户已核对的显式分区。文件号必须由本次操作提供。
            if (partition != APP_STORAGE_PARTITION || file == 0U)
                return SYS_ERR_INVALID_ARG;
            if ((active & APP_V3_PIPE_RECORD) != 0U)
                return SYS_ERR_ALREADY_EXISTS;
            return AppV3Supervisor_Set(APP_V3_PIPE_RECORD, file);
        case 3U: return AppV3Supervisor_Set(APP_V3_PIPE_CAMERA, 0U);
        case 4U: return AppV3Supervisor_Set(0U, 0U);
        case 5U: return AppV3Supervisor_Query(APP_V3_PIPE_CAMERA);
        case 6U: return AppV3Supervisor_Query(APP_V3_PIPE_RECORD);
        case 7U: return AppV3Supervisor_Recover(APP_V3_PIPE_RECORD);
#if APP_V3_JOINT_BOARD_TEST
        case 9U: return AppV3Supervisor_Set(APP_V3_PIPE_USB, 0U);
        case 10U:
            if (partition != APP_STORAGE_PARTITION || file == 0U)
                return SYS_ERR_INVALID_ARG;
            if ((active & APP_V3_PIPE_RECORD) != 0U)
                return SYS_ERR_ALREADY_EXISTS;
            return AppV3Supervisor_Set(APP_V3_PIPE_USB | APP_V3_PIPE_RECORD, file);
        case 11U:
            //独立停USB必须保留当前录像，不能意外创建新录像。
            if ((active & APP_V3_PIPE_RECORD) == 0U)
                return SYS_ERR_INVALID_STATE;
            return AppV3Supervisor_Set(APP_V3_PIPE_RECORD, 0U);
        case 13U: return AppV3Supervisor_Query(APP_V3_PIPE_USB);
        case 14U:
            if ((active & APP_V3_PIPE_RECORD) == 0U)
                return SYS_ERR_INVALID_STATE;
            return AppV3Supervisor_Set(APP_V3_PIPE_USB | APP_V3_PIPE_RECORD, 0U);
#endif
        case 8U:
        {
            UBaseType_t words[RUNTIME_TARGET_COUNT];
            uint32_t index;

            //按原任务句柄读取历史栈低水位，单位是StackType_t。
            g_app_v3_record_board.stack_valid = App_EventsReadStackWatermarks(words);
            if (!g_app_v3_record_board.stack_valid)
                return SYS_ERR_INVALID_STATE;
            for (index = 0U; index < RUNTIME_TARGET_COUNT; ++index)
                g_app_v3_record_board.stack_remaining_words[index] = words[index];
            return SYS_OK;
        }
        default: return SYS_ERR_NOT_SUPPORTED;
    }
}

void AppV3_RecordBoardPoll(void)
{
    AppV3SupervisorStatus_t snapshot;
    uint32_t command;

    //只在Control任务读取并清除邮箱。命令完成仍由原Business结果决定。
    if (AppV3Supervisor_Read(&snapshot) != SYS_OK)
        return;
    g_app_v3_record_board.configured_partition = APP_STORAGE_PARTITION;
#if APP_V3_JOINT_BOARD_TEST
    g_app_v3_joint_time_ms = Platform_TimeGetMs();
#endif
    if (g_app_v3_record_board.command_pending && !snapshot.planning && snapshot.pending_id == 0U)
    {
        g_app_v3_record_board.elapsed_ms = Platform_TimeGetMs() - s_record_board_started;
        g_app_v3_record_board.command_pending = false;
    }
    command = g_app_v3_record_command.command;
    if (command != 0U && !g_app_v3_record_board.command_pending)
    {
        uint32_t partition = g_app_v3_record_command.approved_partition;
        uint32_t file = g_app_v3_record_command.file_number;

        g_app_v3_record_command.command = 0U;
        //每次录像都要求写入本次已批准的分区和未使用文件号。
        g_app_v3_record_command.approved_partition = 0U;
        g_app_v3_record_command.file_number = 0U;
        g_app_v3_record_board.last_command = command;
        ++g_app_v3_record_board.issued;
        g_app_v3_record_board.elapsed_ms = 0U;
        s_record_board_started = Platform_TimeGetMs();
        g_app_v3_record_board.dispatch_result = RecordBoard_Dispatch(command, partition, file, snapshot.active);
        g_app_v3_record_board.command_pending = g_app_v3_record_board.dispatch_result == SYS_OK && command != 8U;
        (void)AppV3Supervisor_Read(&snapshot);
    }
    g_app_v3_record_board.supervisor = snapshot;
}
#endif
