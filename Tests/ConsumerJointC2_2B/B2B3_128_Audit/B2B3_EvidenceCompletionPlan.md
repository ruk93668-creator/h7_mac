# C2.2B Evidence Completion Plan

基线112417798305e5c9dc61c5b3698cc8497efd4869，当前分支codex/stage-C2-2B-B2B6-v1-compat。开始时仅原未跟踪output/。本次只新增四份计划文件，不运行测试、不修改源码/测试/判定工具，不改变任何既有结论。M4 //等OLED稳定 和 HAL_Delay(500); 保留；所有生产文件零写入。

## 覆盖与顺序

全部33项：3项PRESERVE、2项CONTRACT、7项RTOS、21项RUNTIME。30项未关闭目标均有输入、步骤、成功/异常判据、原断言、采集位置/时点、工具与专项需求。CSV是逐项执行矩阵，以下逐项章节补充阅读。所有新增计划均DESIGN_ONLY/NOT_RUN；不将“有方案”计作闭环。

建议实施顺序：先冻结原证据及源码哈希；人工决定D23/D31；建立只读证据采集和来源校验；落实7项RTOS独立期望；预审真实FIFO夹具可达性并实现21项独立场景；执行各自O0/O2正反对照后再提出重新裁决。复验及构建需下一阶段授权，本轮不执行。两项契约未批准时可以独立补RTOS/账本证据，但不得把新BUSY契约设为已认可。

## 变更边界与最小文件范围建议

未来优先在本目录下新增独立补证运行器/判定器及专项目录，不修改原UsbModuleB2/StorageModuleB3历史断言，不覆盖历史日志。可复用现有原始证据；记录缺少的才新增采集。真实FIFO夹具须单独编译生产Media源，原Joint和B1 Media测试均存在FIFO桩，不能直接改名当作真实实现。原流程/配置/协议/ABI不变，生产released/idle保持原状，不以此实现C2.2C。

任何不可合法构造的场景（尤其B3/25无效描述）要输出可达性证据并暂停该项；不写私有账本/伪造frame、session、grant绕过限制。错误注入限外部介质/硬件边界，真实账本观察只读。文件异常、业务完成、帧归还、资格释放分别表达；即使运行证据补齐也不自动将历史BLOCKED/CONTRACT_CHANGED改成PASS。

## 退出验收条件（未来）

33项ID集合完全一致、原证据哈希不变；每条新结论具备原始记录和判定过程；负对照能识别错误而不是仅返回0；缺证保留BLOCKED。软件ledger证明与实板时序证明分开。仅在人工审核本设计后提出编码，本文不授权进入C2.2C。


## B3/13 — Snapshot complete

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(13)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 140, "pass": true, "expression": "g_storage_status.saved_frames==1&&g_storage_status.saved_files==1", "trace_index": 141}, {"line": 140, "pass": true, "expression": "sd_owned&&s_state==STORAGE_IDLE", "trace_index": 141}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 151}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 140, "pass": true, "expression": "g_storage_status.saved_frames==1&&g_storage_status.saved_files==1", "trace_index": 141}, {"line": 140, "pass": true, "expression": "sd_owned&&s_state==STORAGE_IDLE", "trace_index": 141}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 151}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：单段合法JPEG，正常介质

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：saved_frames/files正常；SD归还后文件有效；USB持有时块保留

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/14 — Two-span snapshot

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(14)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 140, "pass": true, "expression": "g_storage_status.saved_frames==1&&g_storage_status.saved_files==1", "trace_index": 142}, {"line": 140, "pass": true, "expression": "sd_owned&&s_state==STORAGE_IDLE", "trace_index": 142}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 152}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 140, "pass": true, "expression": "g_storage_status.saved_frames==1&&g_storage_status.saved_files==1", "trace_index": 142}, {"line": 140, "pass": true, "expression": "sd_owned&&s_state==STORAGE_IDLE", "trace_index": 142}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 152}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：由生产FIFO产生跨块/环回合法JPEG，不能伪造data2

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：两段校验写入后释放同一真实租约；字节哈希与原场景一致

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/15 — Readback corruption

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(15)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 144}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 154}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 144}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 154}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：只让介质Read返回内容翻转一字节

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：FAULT保留；失败文件不计正常完成；租约仍由Camera归还

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/16 — Read failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(16)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 144}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 154}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 144}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 154}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：介质Read返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：记录原IO与清理结果；成功归还不清除文件错误

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/17 — Snapshot close failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(17)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 141}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 151}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 141}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 151}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：首次文件Close返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：独立保留文件故障及卷状态，不能因帧已归还声明文件成功

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/18 — Two-span JPEG CRC

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(18)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 140, "pass": true, "expression": "g_storage_status.saved_frames==1&&g_storage_status.saved_files==1", "trace_index": 142}, {"line": 140, "pass": true, "expression": "sd_owned&&s_state==STORAGE_IDLE", "trace_index": 142}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 152}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 140, "pass": true, "expression": "g_storage_status.saved_frames==1&&g_storage_status.saved_files==1", "trace_index": 142}, {"line": 140, "pass": true, "expression": "sd_owned&&s_state==STORAGE_IDLE", "trace_index": 142}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 152}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：同ID14的生产跨段JPEG及正常介质；保留原ID18输入

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：原分支与ID14相同但证据独立保存，不合并ID

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/24 — Occupied slot

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(24)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 126, "pass": true, "expression": "Offer(false,false)==SYS_OK", "trace_index": 97}, {"line": 126, "pass": true, "expression": "Offer(false,false)==SYS_ERR_BUSY", "trace_index": 102}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 126, "pass": true, "expression": "Offer(false,false)==SYS_OK", "trace_index": 97}, {"line": 126, "pass": true, "expression": "Offer(false,false)==SYS_ERR_BUSY", "trace_index": 102}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：Offer首帧成功，再Offer第二帧返回BUSY

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：第二次拒绝无多余Retain；首帧归还一次；第二帧原持有方责任不丢失

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/25 — JPEG invalid

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(25)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 136}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 146}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 136}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 146}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：原Offer把size_bytes设为4；需先核对真实Retain能否合法接受此描述

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：不得伪造loan；如真实解析/Retain提前拒绝，保存边界差异并申请契约审核，不冒充端到端通过

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/26 — Open failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(26)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 137}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 147}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 137}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 147}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：StorageFS_Open返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：挂载/打开错误保留；已接受帧最终SD归还，未成功文件不计完成

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/27 — AVI partial failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(27)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 143}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 153}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 143}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 153}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：第二次StorageFS_Write返回IO，保留原fail_nth=2

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：记录已写字节、失败及收尾顺序；不得提前释放正在读取的帧

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/28 — Short write

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(28)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 144}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 154}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 144}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 154}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：原short_write=true，在原条件n>224时少写1字节

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：按原实现产生错误；写入未完成与归还完成分开记录

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/29 — Periodic sync failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(29)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 148}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 158}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 141, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 148}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 158}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：tick=1200，StorageFS_Sync返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：原Sync故障不被归还成功覆盖；帧操作完成后才进入归还

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/32 — Queued frame then STOP

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(32)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 137, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 106}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 165}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 137, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 106}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 165}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：Offer成功，在Process前提交STOP

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：不丢已接收完整帧；保持原Process顺序并正常Finish，归还后才FREE

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/34 — Wrong owner return

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(34)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 143, "pass": true, "expression": "sd_owned", "trace_index": 135}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 154}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 189}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 143, "pass": true, "expression": "sd_owned", "trace_index": 135}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 154}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 189}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：先由Storage身份调用ProcessReturns，再切Camera身份调用

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：错误owner时无ReleaseLease入口且loan仍SD owned；Camera调用后才真实归还

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/36 — Cached status timing

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(36)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 147, "pass": true, "expression": "App_StorageGetStatus(&view)==SYS_OK", "trace_index": 148}, {"line": 147, "pass": true, "expression": "view.pending_returns==1", "trace_index": 148}, {"line": 147, "pass": true, "expression": "App_StorageGetStatus(&view)==SYS_OK", "trace_index": 175}, {"line": 147, "pass": true, "expression": "view.pending_returns==0", "trace_index": 175}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 184}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 219}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 147, "pass": true, "expression": "App_StorageGetStatus(&view)==SYS_OK", "trace_index": 148}, {"line": 147, "pass": true, "expression": "view.pending_returns==1", "trace_index": 148}, {"line": 147, "pass": true, "expression": "App_StorageGetStatus(&view)==SYS_OK", "trace_index": 175}, {"line": 147, "pass": true, "expression": "view.pending_returns==0", "trace_index": 175}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 184}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 219}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：正常处理后Camera归还，再读view并推进Storage Process

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：原view.pending_returns的1→0发布时点保留，账本实际归还不能由缓存值代替

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/37 — Tick wrap

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(37)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 148, "pass": true, "expression": "s_state!=STORAGE_FAULT", "trace_index": 167}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 176}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 211}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 148, "pass": true, "expression": "s_state!=STORAGE_FAULT", "trace_index": 167}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 176}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 211}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：按原UINT32_MAX-20→30序列，并保留原输入/FPS tick设定

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：时间回绕不FAULT；无提前回收，凭据一致且只归还一次

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/38 — AVI Finish write failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(38)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 183}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 183}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 183}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 183}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：首帧及归还正常后，Finish阶段Write返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：Finish故障保留；区分先前已归还的帧与文件尾失败

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/39 — AVI Close failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(39)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 188}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 188}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 188}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 188}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：首帧归还后，在录像封尾Close返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：真实loan释放完成不意味着Close成功；FAULT不改写

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/40 — Unmount failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(40)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 188}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 188}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 188}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 188}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：首帧归还后，Unmount返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：已封尾关闭的文件、卷仍挂载及FAULT分列；不能把全部归还说成卷已释放

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/41 — AVI Header seek failure

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(41)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 185}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 185}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 154, "pass": true, "expression": "s_state==STORAGE_FAULT", "trace_index": 185}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 185}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：首帧归还后，Finish阶段Seek返回IO

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：错误保留；不重复归还，不标正常完成

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/42 — Repeated STOP

原复核结论（保持）：RUNTIME_EVIDENCE_MISSING。

源码：CM7/Application/app_storage.c:350; CM7/Application/app_storage.c:395; CM7/Media/Src/camera_mjpeg_fifo.c:424

测试：Tests/StorageModuleB3/behavior_test.c:101; entry=B3_TestRun(42)

原始断言（保留）：{"executed_C_O0": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 155, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 197}, {"line": 155, "pass": true, "expression": "s_state==STORAGE_IDLE", "trace_index": 216}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 216}], "executed_C_O2": [{"line": 107, "pass": true, "expression": "App_StoragePrepare(&storage_config)==SYS_OK", "trace_index": 5}, {"line": 119, "pass": true, "expression": "r==SYS_OK", "trace_index": 12}, {"line": 127, "pass": true, "expression": "Offer(id==14||id==18,id==25)==SYS_OK", "trace_index": 97}, {"line": 153, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 153}, {"line": 155, "pass": true, "expression": "App_StorageStop()==SYS_OK", "trace_index": 197}, {"line": 155, "pass": true, "expression": "s_state==STORAGE_IDLE", "trace_index": 216}, {"line": 156, "pass": true, "expression": "bad_owner==0", "trace_index": 216}], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：正常STOP/Process后，再STOP/Process

步骤：1. 原业务场景逐字保留并记录输入；2. 编译真实app_storage、Camera owner调用及camera_mjpeg_fifo.c；3. 通过BSP字节回调/公开FIFO接口产生合法frame并真实Retain；4. Storage处理至RETURN_PENDING；5. Camera执行ProcessReturns，捕获真实ReleaseLease返回及账本；6. 分别安排USB先/后归还，记录最后引用；7. 核对文件错误与身份/重复/迟到交错；8. 各ID独立O0/O2证据，不用通用用例代替

成功判据：重复业务STOP不重复实际ReleaseLease；FREE槽位不重用旧凭据

异常/负对照：删除ReleaseLease返回、把请求当成功、owner错误仍进入Release、旧lease重复扣引用、仅把槽FREE或mock sd_owned清0均必须拒绝；两路并持时不得要求全块立即FREE

采集位置：App_StorageOfferFrame/Process/ProcessReturns；CameraMjpegFifo_RetainFrame/ReleaseLease；Mjpeg_ReleaseLoan/Mjpeg_ReleaseBlocks；原CameraTask实际归还位置；FS调用及错误

采集时点：T0取得frame/T1真实Retain/T2业务读取结束/T3 RETURN_PENDING/T4 Camera入口/T5真实Release返回/T6账本与槽位变更/T7 Storage状态缓存发布/T8最后消费者归还

状态及所有权：消费者session/request/generation与Media lease.session/id、stream_epoch/lease_id分别记录不可混用；block mask/refcounts、usb_owned/sd_owned、slot generation/state、ISR/task身份、PRIMASK、返回值和文件状态

建议工具：YES: actual FIFO symbols/ELF provenance, return snapshots and per-ID correlation；建议专项：YES: real-FIFO scenario fixture preserving original assertions。硬件范围：NO for deterministic real-source software ledger proof; YES separately for actual DMA/SD timing and multicore/preemption safety。依赖：Real-FIFO fixture feasibility audit; no private state writes; no production released/idle callbacks。所有拟执行项目NOT_RUN。



## B3/45 — BOOT wait and stack cadence

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/storage_task.c:31; CM7/Application/Tasks/storage_task.c:41

测试：Tests/StorageModuleB3/behavior_test.c:183; entry=B3_TaskRun(2)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：按原waits1/2/3/4事件：首轮无BOOT、次轮BOOT、第三轮TIMEOUT、第四轮IO；保持原tick+=501

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：前两次FOREVER，BOOT后100ms；准备后Process；IO分支continue；采样>=1000ms且按实际now。原case2不创建任务，stack2048/priority1/Attach用已有ID43创建伴随证据；不声称ID45自身覆盖Create

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。



## B2/4 — START IO failure

原复核结论（保持）：EVIDENCE_SUFFICIENT。

源码：CM7/Application/app_usb.c:52; CM7/Application/app_usb.c:132; CM7/Application/app_usb.c:179; CM7/Application/app_usb.c:240; CM7/Application/app_usb.c:255; CM7/Application/Tasks/usb_task.c:185

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(4)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：保留原O0/O2消息、断言、源码与哈希

步骤：索引原始证据，不重新实现、不重新运行或替换

成功判据：只保留B2 4的ACK/FAULT映射充分结论，历史PASS/BLOCKED不变

异常/负对照：保留现有负对照，不把未执行的新控制称为PASS

采集位置：沿用上一轮CSV Current runtime evidence及Existing causal assertions

采集时点：原START失败及ACK/FAULT运输记录

状态及所有权：App_USB权威状态，transport返回与业务完成分离

建议工具：NO；建议专项：NO。硬件范围：NO for this software claim。依赖：Baseline freeze and exact source/assertion preservation。所有拟执行项目NOT_RUN。



## B2/5 — Not initialized

原复核结论（保持）：EVIDENCE_SUFFICIENT。

源码：CM7/Application/app_usb.c:52; CM7/Application/app_usb.c:132; CM7/Application/app_usb.c:179; CM7/Application/app_usb.c:240; CM7/Application/app_usb.c:255; CM7/Application/Tasks/usb_task.c:185

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(5)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：保留原O0/O2消息、断言、源码与哈希

步骤：索引原始证据，不重新实现、不重新运行或替换

成功判据：只保留B2 5的ACK/FAULT映射充分结论，历史PASS/BLOCKED不变

异常/负对照：保留现有负对照，不把未执行的新控制称为PASS

采集位置：沿用上一轮CSV Current runtime evidence及Existing causal assertions

采集时点：原START失败及ACK/FAULT运输记录

状态及所有权：App_USB权威状态，transport返回与业务完成分离

建议工具：NO；建议专项：NO。硬件范围：NO for this software claim。依赖：Baseline freeze and exact source/assertion preservation。所有拟执行项目NOT_RUN。



## B2/6 — ACK failure with FAULT

原复核结论（保持）：EVIDENCE_SUFFICIENT。

源码：CM7/Application/app_usb.c:52; CM7/Application/app_usb.c:132; CM7/Application/app_usb.c:179; CM7/Application/app_usb.c:240; CM7/Application/app_usb.c:255; CM7/Application/Tasks/usb_task.c:185

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(6)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：保留原O0/O2消息、断言、源码与哈希

步骤：索引原始证据，不重新实现、不重新运行或替换

成功判据：只保留B2 6的ACK/FAULT映射充分结论，历史PASS/BLOCKED不变

异常/负对照：保留现有负对照，不把未执行的新控制称为PASS

采集位置：沿用上一轮CSV Current runtime evidence及Existing causal assertions

采集时点：原START失败及ACK/FAULT运输记录

状态及所有权：App_USB权威状态，transport返回与业务完成分离

建议工具：NO；建议专项：NO。硬件范围：NO for this software claim。依赖：Baseline freeze and exact source/assertion preservation。所有拟执行项目NOT_RUN。



## B2/23 — STOP START FIFO

原复核结论（保持）：CONTRACT_REVIEW_REQUIRED。

源码：CM7/Application/app_usb.c:52; CM7/Application/app_usb.c:132; CM7/Application/app_usb.c:179; CM7/Application/app_usb.c:240; CM7/Application/app_usb.c:255; CM7/Application/Tasks/usb_task.c:185

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(23)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：原scenario23 STOP11→START12

步骤：静态冻结旧同步与当前异步差异；列出每条ACK/FAULT、Consumer phase/grant；先人工选择，再编写独立新契约测试，旧断言保留

成功判据：建议接受RELEASE未完成时新START BUSY，不能接受伪造释放或绕过ConsumerBegin；当前建议不等于批准

异常/负对照：把新START ACK改成OK但grant仍RELEASE、或清grant冒充可重启，判定必须拒绝；改FAULT/command_id也必须拒绝

采集位置：App_USBHandleCommand/Process/FinishRound、UsbTask_Reply与ConsumerBegin/End完整身份

采集时点：每条命令入队/取出、回复入队/失败重试、RELEASE Stage/Flush、调用ModeStart前后

状态及所有权：原业务ACK与资格RELEASE独立；保持session/request/consumer/generation，不强制释放

建议工具：YES after decision: complete expected message/state table；建议专项：YES after decision: per-case immutable ACK/FAULT trajectory。硬件范围：NO for this software claim。依赖：User decision before expected behavior is approved。所有拟执行项目NOT_RUN。



## B2/31 — Fault report reset on START

原复核结论（保持）：CONTRACT_REVIEW_REQUIRED。

源码：CM7/Application/app_usb.c:52; CM7/Application/app_usb.c:132; CM7/Application/app_usb.c:179; CM7/Application/app_usb.c:240; CM7/Application/app_usb.c:255; CM7/Application/Tasks/usb_task.c:185

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(31)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：原scenario31 START11→STOP12→START13

步骤：静态冻结旧同步与当前异步差异；列出每条ACK/FAULT、Consumer phase/grant；先人工选择，再编写独立新契约测试，旧断言保留

成功判据：建议接受RELEASE未完成时新START BUSY，不能接受伪造释放或绕过ConsumerBegin；当前建议不等于批准

异常/负对照：把新START ACK改成OK但grant仍RELEASE、或清grant冒充可重启，判定必须拒绝；改FAULT/command_id也必须拒绝

采集位置：App_USBHandleCommand/Process/FinishRound、UsbTask_Reply与ConsumerBegin/End完整身份

采集时点：每条命令入队/取出、回复入队/失败重试、RELEASE Stage/Flush、调用ModeStart前后

状态及所有权：原业务ACK与资格RELEASE独立；保持session/request/consumer/generation，不强制释放

建议工具：YES after decision: complete expected message/state table；建议专项：YES after decision: per-case immutable ACK/FAULT trajectory。硬件范围：NO for this software claim。依赖：User decision before expected behavior is approved。所有拟执行项目NOT_RUN。



## B2/27 — Wait and stack cadence

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/usb_task.c:60; CM7/Application/Tasks/usb_task.c:84

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(27)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：已有原START和BOOT后进入UsbTask_Run，按原时钟/等待注入运行7次

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：20ms等待；按真实tick每>=1000ms采样；TIMEOUT计数；IO保留/Assert后继续；新增独立冷BOOT子场景验证FOREVER

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。



## B2/28 — Task stack and priority

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/usb_task.c:60; CM7/Application/Tasks/usb_task.c:84

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(28)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：原UVC初始化后调用UsbTask_Create；另用干净未调度状态验证创建

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：传入静态stack/TCB及2048words/priority4，随后Attach(USB,有效handle)一次

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。



## B2/59 — Wait and stack cadence

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/usb_task.c:60; CM7/Application/Tasks/usb_task.c:84

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(59)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：与27相同但mode=TEST，不申请Camera

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：与27同等待/采样规则；无UVC/Camera资格操作

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。



## B2/60 — Task stack and priority

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/usb_task.c:60; CM7/Application/Tasks/usb_task.c:84

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(60)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：mode=TEST，其余保持原scenario28

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：2048words/priority4；创建后Attach，TEST不申请Camera

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。



## B2/91 — Wait and stack cadence

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/usb_task.c:60; CM7/Application/Tasks/usb_task.c:84

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(91)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：与27相同但mode=NONE

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：与27同等待/采样规则；不得新增Camera申请

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。



## B2/92 — Task stack and priority

原复核结论（保持）：TEST_EVIDENCE_MISSING。

源码：CM7/Application/Tasks/usb_task.c:60; CM7/Application/Tasks/usb_task.c:84

测试：Tests/UsbModuleB2/behavior_test.c:165; entry=B2_TestRun(92)

原始断言（保留）：{"executed_C_O0": [], "executed_C_O2": [], "original_Python": "baseline.ok && candidate.ok && full trace equality"}

输入：mode=NONE，其余保持原scenario28

步骤：1. 原入口及模式独立镜像；2. 捕获真实Task调用参数及返回、BOOT/事件和tick；3. 用独立常量期望断言而非从被测输出反推；4. 对未执行的Create/Attach仅引用伴随专项，不伪记本ID覆盖；5. 保存O0/O2逐事件与负对照

成功判据：2048words/priority4；创建后Attach，无伪造UVC活动

异常/负对照：篡改wait为错误值、移除BOOT门、stack少1word、priority改值、Attach目标/handle错误或提前Attach均应失败；Run场景不强造Create。另做Attach失败：返回原错误，不能宣称成功或擅自回滚s_task

采集位置：xTaskCreateStatic入口/返回、App_EventsAttach入口/返回、App_EventsWait入口/返回、uxTaskGetStackHighWaterMark及Assert

采集时点：调用前后和BOOT转换/timeout/IO/1000ms采样边界

状态及所有权：Task所有者、handle/TCB/stack地址、words/priority、目标、scheduler/ISR、事件位、实际tick/now/采样值；Map仅静态预算

建议工具：YES: source-resolved expected RTOS contract and event provenance；建议专项：YES: parameter-negative and Attach success/failure companion cases。硬件范围：NO for this software claim。依赖：Unchanged task configuration; timing deterministic clock。所有拟执行项目NOT_RUN。
