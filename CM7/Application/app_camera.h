#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "sys_result.h"
#include "camera_config.h"
#include "app_messages.h"
#include <stdbool.h>

//冷启动显式传入相机配置，不允许运行中覆盖
SysResult_t App_CameraConfigure(const CameraConfig_t *config);

//获取当前只读配置，供Device与Media共用
const CameraConfig_t *App_CameraGetConfig(void);

//主循环初始化并启动单帧采集，重复或交叉启动由Media拒绝
SysResult_t App_CameraInit(void);

//主循环初始化并启动连续采集，停止并释放占用后才允许切换模式
SysResult_t App_CameraInitStream(void);

//先停止Media并等待帧归还，再通过Device_Stop停止传感器
SysResult_t App_CameraStop(void);

//推进Media采集流程，由App_Run持续调用
void App_CameraRun(void);

//保存业务状态的只读副本，不携带任务句柄或Media帧引用
typedef struct
{
    uint32_t state;//按原停止优先规则生成0、1、2状态
    uint32_t command_id;//最近接收的控制命令编号
    SysResult_t last_result;//最近业务操作结果，不含任务等待错误
    uint32_t captured_frames;//最近读取的Media完成帧数
    uint32_t dropped_frames;//最近读取的Media丢帧数
    uint32_t measured_fps_milli;//按原窗口计算的采集FPS
    CameraConfig_t applied;//最近实际运行时采样的配置
    bool applied_valid;//配置是否已经实际采样
} AppCameraRuntimeStatus_t;

//标明本次更新哪组诊断字段，不参与业务决策
typedef enum
{
    APP_CAMERA_DIAG_COMMAND = 0,
    APP_CAMERA_DIAG_RESULT,
    APP_CAMERA_DIAG_STATE,
    APP_CAMERA_DIAG_COUNTERS,
    APP_CAMERA_DIAG_FPS
} AppCameraDiagnosticField_t;

//观察者只在Camera调用栈内读取副本，禁止保存指针或回调业务接口
typedef void (*AppCameraDiagnosticObserver_t)(AppCameraDiagnosticField_t field,
    const AppCameraRuntimeStatus_t *status);

//以下运行期接口仅由CameraTask串行调用，确认接口只在成功入队后调用
//登记诊断观察者，观察者不得收发消息或操作硬件
void App_CameraSetDiagnosticObserver(AppCameraDiagnosticObserver_t observer);

//判断原ACK及STOP门控是否允许接收下一条命令
bool App_CameraCanAcceptCommand(void);

//处理一条已经接收的命令，不直接操作RTOS消息队列
void App_CameraHandleCommand(const AppMessage_t *command);

//推进Media及故障识别，必须先于Bridge本轮推进
void App_CameraProcess(void);

//判断Bridge本轮能否申请新帧
bool App_CameraCanOfferFrame(void);

//在Bridge推进后处理停止完成及采集统计
void App_CameraFinishRound(void);

//读取待发ACK，失败时保留原记录
bool App_CameraGetPendingAck(AppMessage_t *message);

//确认ACK已经成功入队，不代表Control已经处理
void App_CameraConfirmAckEnqueued(void);

//独立读取待发FAULT，不消耗故障记录
bool App_CameraGetPendingFault(AppMessage_t *message);

//确认FAULT已经成功入队
void App_CameraConfirmFaultEnqueued(void);

//只在原状态发布位置采样实际配置
void App_CameraSampleStatus(void);

//读取业务副本，不采样配置或推进业务
void App_CameraGetStatus(AppCameraRuntimeStatus_t *status);

//读取最近业务结果，不混入任务等待错误
SysResult_t App_CameraGetLastResult(void);

#endif
