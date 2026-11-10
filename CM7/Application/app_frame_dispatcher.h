#ifndef APP_FRAME_DISPATCHER_H
#define APP_FRAME_DISPATCHER_H
#include "app_storage.h"
#include "video_config_types.h"

//冷启动固定选择主输出，ETH/Wi-Fi声明存在不等于功能可用
SysResult_t App_FrameDispatcherValidateOutput(VideoOutput_t output);

//Camera每次建立新采集会话时重置本会话帧编号
SysResult_t App_FrameDispatcherBegin(uint32_t epoch);

//Camera取帧前确认编号仍可分配，耗尽后不取得新租约
bool App_FrameDispatcherCanAcquire(void);

//查询已绑定租借服务的需求，不直接暴露存储内部状态
bool App_FrameDispatcherNeedsStorage(void);

//为接收端登记独立租借，接收端归还后才允许Media回收
SysResult_t App_FrameDispatcherPublish(const CameraFrame_t *frame, AppFrameMeta_t *meta);

//预留按需AI接口，尚未实现时明确拒绝
SysResult_t App_FrameDispatcherRequestAi(void);
#endif
