// 声明 V1 板的模块和静态配置 ID，目录及装配均不操作硬件。

#ifndef V2_BOARD_V1_H
#define V2_BOARD_V1_H

#include "../Core/Runtime/control_service.h"

enum
{
    V2_BOARD_CAMERA = 1,
    V2_BOARD_USB,
    V2_BOARD_STORAGE
};

enum
{
    V2_BOARD_RAW_USB = 1,
    V2_BOARD_JPEG_USB,
    V2_BOARD_JPEG_STORAGE,
    V2_BOARD_JPEG_USB_STORAGE
};

// 借用程序全生命周期有效的只读目录。
const CSCatalog *V2BoardV1_Catalog(void);

// 返回采集配置目标。USB 交付速率和 Storage 采样速率由各自配置表达。
SysResult_t V2BoardV1_CaptureTarget(uint32_t backend,
                                    CoreFrameRate *target);

// 只计算候选。工作区由调用方独占，装配失败保留输出，不触及运行配置。
SysResult_t V2BoardV1_Compose(uint32_t profile_id,
                              V2CompositionScratch *scratch,
                              V2CompositionCandidate *candidate,
                              V2Issue *issue,
                              GraphIssue *graph_issue);
#endif
