//同一仿真翻译单元使用A1真实Media和Camera私有Dispatcher，生产任务入口不暴露测试端口。
#include "../MediaLease/media_lease_test.c"
#include "../../../CM7/ApplicationV3/Services/Camera/camera_service.c"

static AppMessage_t s_delivered[CAMERA_FRAME_CONSUMER_COUNT];
static bool s_full[CAMERA_FRAME_CONSUMER_COUNT];

//仅USB在产品使用真实RTOS队列，四方容量试验通过此值传递端捕获消息。
static SysResult_t Test_Send(CameraFrameConsumer_t consumer, const AppMessage_t *message)
{
    if (s_full[consumer])
        return SYS_ERR_NO_RESOURCE;
    s_delivered[consumer] = *message;
    return SYS_OK;
}

static void Test_Receive(CameraFrameConsumer_t consumer, AppMessageType_t type,
    uint32_t command_id, uint32_t generation, uint32_t lease_id, bool admission)
{
    AppMessage_t message = {.type = type, .command_id = command_id,
        .stream_epoch = generation, .lease_id = lease_id};
    CameraFrame_Receive(consumer, &message, admission);
}

static uint32_t Test_Release(CameraFrameConsumer_t consumer, bool admission)
{
    const AppMessage_t offer = s_delivered[consumer];

    Test_Receive(consumer, APP_MESSAGE_FRAME_RETURN, offer.command_id,
        offer.stream_epoch, offer.lease_id, admission);
    CHECK(CameraFrame_Advance(admission, Test_Send) == SYS_OK);
    CHECK(CameraFrame_Advance(admission, Test_Send) == SYS_OK);
    CHECK(s_delivered[consumer].type == APP_MESSAGE_RETURN_ACK);
    CHECK(s_delivered[consumer].frame.data == NULL);
    Test_Receive(consumer, APP_MESSAGE_RETURN_ACK_CONSUMED, offer.command_id,
        offer.stream_epoch, offer.lease_id, admission);
    CHECK(s_transfers[consumer].state == CAMERA_TRANSFER_EMPTY);
    return 0U;
}

static uint32_t Test_FourConsumers(void)
{
    CameraFrameLease_t held[CAMERA_FRAME_CONSUMER_COUNT];
    CameraFrame_t frames[CAMERA_FRAME_CONSUMER_COUNT];
    uint32_t index, prior, iteration;

    //让较低优先级先入账，第一块图像仍由USB领取，之后优先级逐轮轮转。
    for (index = CAMERA_FRAME_CONSUMER_COUNT; index != 0U; --index)
        Test_Receive((CameraFrameConsumer_t)(index - 1U), APP_MESSAGE_FRAME_REQUEST, 1U, 0U, 0U, true);

    for (index = 0U; index < CAMERA_FRAME_CONSUMER_COUNT; ++index)
    {
        CHECK(Complete(index & 1U, index + 1U) == SYS_OK);
        CHECK(CameraFrame_Advance(true, Test_Send) == SYS_OK);
        CHECK(s_transfers[index].state == CAMERA_TRANSFER_OFFER_PENDING);
        CHECK(s_transfers[index].reply.frame_id == index + 1U);
        if (index + 1U < CAMERA_FRAME_CONSUMER_COUNT)
            CHECK(s_transfers[index + 1U].state == CAMERA_TRANSFER_REQUESTED);
        held[index] = s_transfers[index].lease.raw;
        frames[index] = s_transfers[index].reply.frame;
        for (prior = 0U; prior < index; ++prior)
            CHECK(held[prior].buffer_id != held[index].buffer_id);
        Test_Receive((CameraFrameConsumer_t)index, APP_MESSAGE_FRAME_REQUEST, 2U, 0U, 0U, true);
        CHECK(s_transfers[index].last_request == 1U);
    }
    CHECK(CameraFrame_Advance(true, Test_Send) == SYS_OK);
    CHECK(VerifyHeld(held, frames, 15U) == 0U);

    //USB退出只交还本路，其他三路继续持有原帧，采集持续轮转。
    CHECK(Test_Release(CAMERA_FRAME_CONSUMER_USB, true) == 0U);
    for (iteration = 0U; iteration < 100U; ++iteration)
        CHECK(Complete(iteration & 1U, 10U) == SYS_OK);
    CHECK(VerifyHeld(held, frames, 14U) == 0U);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY && CameraFrame_Draining());
    for (index = 1U; index < CAMERA_FRAME_CONSUMER_COUNT; ++index)
        CHECK(Test_Release((CameraFrameConsumer_t)index, false) == 0U);
    CHECK(!CameraFrame_Draining());
    CHECK(CameraCapture_Stop() == SYS_OK && CameraCapture_IsStopped());
    return 0U;
}

static uint32_t Test_ConsumptionAndOldReturn(void)
{
    const CameraFrameConsumer_t consumer = CAMERA_FRAME_CONSUMER_USB;
    AppMessage_t offer;
    uint32_t physical_buffer;

    CHECK(Complete(0U, 1U) == SYS_OK);
    Test_Receive(consumer, APP_MESSAGE_FRAME_REQUEST, 1U, 0U, 0U, true);
    CHECK(CameraFrame_Advance(true, Test_Send) == SYS_OK);
    CHECK(CameraFrame_Advance(true, Test_Send) == SYS_OK);
    offer = s_delivered[consumer];
    physical_buffer = s_transfers[consumer].lease.raw.buffer_id;

    //先注入过期代次，再注入重复Return，核对实际槽和终结记录互不冒充。
    Test_Receive(consumer, APP_MESSAGE_FRAME_RETURN, 1U, offer.stream_epoch + 1U, offer.lease_id, true);
    CHECK(CameraFrame_Advance(true, Test_Send) == SYS_OK);
    CHECK(s_capture.stream.buffers[physical_buffer].state == CAMERA_BUFFER_IN_USE);
    Test_Receive(consumer, APP_MESSAGE_FRAME_RETURN, 1U, offer.stream_epoch, offer.lease_id, true);
    CHECK(CameraFrame_Advance(true, Test_Send) == SYS_OK);
    CHECK(s_capture.stream.buffers[physical_buffer].state == CAMERA_BUFFER_FREE);
    s_full[consumer] = true;
    CHECK(CameraFrame_Advance(false, Test_Send) == SYS_OK);
    CHECK(s_transfers[consumer].state == CAMERA_TRANSFER_TERMINAL_PENDING);
    Test_Receive(consumer, APP_MESSAGE_RETURN_ACK_CONSUMED, 1U, offer.stream_epoch, offer.lease_id, false);
    CHECK(CameraFrame_Draining());
    s_full[consumer] = false;
    CHECK(CameraFrame_Advance(false, Test_Send) == SYS_OK);
    Test_Receive(consumer, APP_MESSAGE_FRAME_RETURN, 1U, offer.stream_epoch, offer.lease_id, false);
    Test_Receive(consumer, APP_MESSAGE_FRAME_REQUEST, 2U, 0U, 0U, false);
    CHECK(s_transfers[consumer].state == CAMERA_TRANSFER_WAIT_CONSUMED);
    CHECK(CameraFrame_Draining());
    Test_Receive(consumer, APP_MESSAGE_RETURN_ACK_CONSUMED, 1U, offer.stream_epoch, offer.lease_id, false);
    CHECK(!CameraFrame_Draining());
    Test_Receive(consumer, APP_MESSAGE_FRAME_RETURN, 1U, offer.stream_epoch, offer.lease_id, true);
    CHECK(s_transfers[consumer].state == CAMERA_TRANSFER_EMPTY);
    return 0U;
}

uint32_t A2_TestRun(uint32_t scenario)
{
    g_stream_size = sizeof(CameraStream_t);
    g_capture_size = sizeof(CameraCapture_t);
    g_lease_size = sizeof(CameraFrameLease_t);
    CHECK(StartRaw() == 0U);
    return scenario == 0U ? Test_FourConsumers() : Test_ConsumptionAndOldReturn();
}

//真实Service/FreeRTOS由startup_test.c覆盖。联合试验若误触下列非数据面入口，立即失败。
static void Test_UnexpectedServiceCall(void)
{
    g_failure = __LINE__;
    __breakpoint(0);
}

SysResult_t AppV3Mailbox_Init(AppV3ServiceMailbox_t *mailbox, RuntimeEventTarget_t owner,
    AppV3QueueReadable_t readable, AppV3Queue_t *endpoint)
{
    (void)mailbox; (void)owner; (void)readable; (void)endpoint;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

void AppV3Mailbox_Discard(AppV3ServiceMailbox_t *mailbox, AppV3Queue_t *endpoint)
{
    (void)mailbox; (void)endpoint;
    Test_UnexpectedServiceCall();
}

SysResult_t AppV3Mailbox_Request(RuntimeEventTarget_t owner, const AppV3Queue_t *endpoint,
    uint32_t command_id, uint32_t operation, uint32_t argument)
{
    (void)owner; (void)endpoint; (void)command_id; (void)operation; (void)argument;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

SysResult_t AppV3Mailbox_Receive(AppV3ServiceMailbox_t *mailbox, AppV3ServiceCommand_t *command)
{
    (void)mailbox; (void)command;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

SysResult_t AppV3Mailbox_Complete(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, bool running, uint32_t parameter)
{
    (void)mailbox; (void)command; (void)result; (void)running; (void)parameter;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

SysResult_t AppV3Mailbox_CompleteHardware(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, const AppV3HardwareReport_t *report)
{
    (void)mailbox; (void)command; (void)result; (void)report;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

void AppV3_ServiceRun(void *argument, AppV3ServiceMailbox_t *mailbox, uint32_t (*advance)(void))
{
    (void)argument; (void)mailbox; (void)advance;
    Test_UnexpectedServiceCall();
}

bool App_MessagesIsOwner(RuntimeEventTarget_t target)
{
    (void)target;
    Test_UnexpectedServiceCall();
    return false;
}

bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message)
{
    (void)channel; (void)message;
    Test_UnexpectedServiceCall();
    return false;
}

SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message)
{
    (void)channel; (void)message;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

Device_t *Device_Find(const char *name)
{
    (void)name;
    Test_UnexpectedServiceCall();
    return NULL;
}

SysResult_t Device_Start(Device_t *device)
{
    (void)device;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}

SysResult_t Device_Stop(Device_t *device)
{
    (void)device;
    Test_UnexpectedServiceCall();
    return SYS_ERR_INVALID_STATE;
}
