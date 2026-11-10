//真实FreeRTOS内核与事件适配，保留旧IPC桩仅用于建立其他接收端
#define StorageTask_Create DummyStorageTask_Create
#include "../IpcRuntime/task_test.c"
#undef StorageTask_Create
#include "../../CM7/Application/Tasks/storage_task.c"

static const AppStorageConfig_t config={10,131072,60000,1048576,524288,16};
volatile AppStorageStatus_t g_storage_debug;
volatile uint32_t prepared_storage,processed_storage;
const AppStorageConfig_t *VideoConfig_GetStorage(void){return &config;}
SysResult_t App_StoragePrepare(const AppStorageConfig_t *c){if(c!=&config||!App_EventsIsOwner(RUNTIME_TARGET_STORAGE))return SYS_ERR_INVALID_STATE;prepared_storage++;g_storage_debug.ready=1;return SYS_OK;}
void App_StorageProcess(void){if(App_EventsIsOwner(RUNTIME_TARGET_STORAGE))processed_storage++;}
SysResult_t App_StorageStartRecording(void){return SYS_OK;}
SysResult_t App_StorageRequestSnapshot(void){return SYS_OK;}
SysResult_t App_StorageStop(void){return SYS_OK;}
SysResult_t App_StorageRecover(void){return SYS_OK;}

int StorageRuntime_TestRun(uint32_t id)
{
    g_case=99;
    CHECK(App_EventsInit()==SYS_OK);
    CHECK(CameraTask_Create()==SYS_OK);CHECK(UsbTask_Create()==SYS_OK);CHECK(ControlTask_Create()==SYS_OK);CHECK(IpcTask_Create()==SYS_OK);
    if(id==2){test_ipsr=16;CHECK(StorageTask_Create()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==3){CHECK(StorageTask_Create()==SYS_ERR_NO_RESOURCE);CHECK(s_tasks[RUNTIME_TARGET_STORAGE]==NULL);return 0;}
    CHECK(StorageTask_Create()==SYS_OK);
    CHECK(uxTaskGetNumberOfTasks()==5U&&s_tasks[RUNTIME_TARGET_STORAGE]!=NULL);
    CHECK(sizeof(s_stack)==8192U&&((uint32_t)s_stack&7U)==0U);
    if(id==0)return 0;
    if(id==1){CHECK(StorageTask_Create()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==4){Running();pxCurrentTCB=s_tasks[RUNTIME_TARGET_STORAGE];StorageTask_Run(NULL);return 1;}
    CHECK(App_EventsRecordInitialization(SYS_OK)==SYS_OK);Running();CHECK(App_EventsReleaseBoot()==SYS_OK);
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_STORAGE];StorageTask_Run(NULL);return 1;
}
