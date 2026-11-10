# Stage 08F：Keil 分组整理

日期：2026-11-10。

## 范围

本轮只调整 Keil 工程的逻辑分组，不移动磁盘源文件，不修改运行代码。

| 文件 | 原分组 | 新分组 |
| --- | --- | --- |
| app_storage.c/.h | Stage08D Storage | CM7\Application |
| app_frame_dispatcher.c/.h | Stage08D Storage | CM7\Application |
| storage_task.c/.h | Stage08D Storage | CM7 Native Runtime（现有任务分组） |
| jpeg_frame.c/.h | Stage08D Storage | CM7\Service |
| mjpeg_avi.c/.h | Stage08D Storage | CM7\Service |

删除空的 Stage08D Storage 分组，同步 uvoptx 的分组和文件编号，保留已有 Watch、断点及调试 Target 设置。

## 检查

- 两个 Target 的文件路径集合、文件类型、文件级编译选项保持一致。
- TargetOption 保持一致。
- CM4 原有文件排除设置保持有效，没有把 CM7 存储代码加入 CM4 构建。
- CM7、CM4 正式 Target 均完整重编，均为 0 Error、0 Warning。
- 构建前后生产源码指纹一致，Keil 未改写工程文件。
- 本轮没有修改生产逻辑，未新增硬件验证结论。

## 既有状态

08D 检查点为 e11086f。录像目标 25FPS 尚未完成；曾实测 AVI 17.58FPS，不代表所有场景均达到该帧率。现有诊断、分区3对照配置和用户视频设置均保留，本轮没有清理性能探针或变更业务参数。

本地整理前，对旧工程和未提交个人设置另行归档；构建产物和本机日志不加入本次提交。
