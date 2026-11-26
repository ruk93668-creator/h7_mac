# N1–N5 最终 Diff 审核

源码冻结：78580073610ff4cfe06a4b004b1b0d75bffef499。
main：851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71。

原始工作区及 RTE 未修改，审计哈希和目标源清单见 Evidence/Final/static-audit.json。
生产实现、测试与源版本全部在冻结提交；本次交付提交仅增加报告及原始证据，不增加未经重验的源码。

- Platform：唯一执行上下文、生命周期退休、全局 timer、恢复和公共 JPEG copy；不改环 OWN/tail 算法。
- Service：HTTP 活动查询、断开需求撤销，与 TCP ACK/私有缓冲归还区分。
- ApplicationV3：Network Registry/Task/Service、原 Supervisor、Camera 既有消息协议扩展。
- Common：追加任务/消费者/消息槽，不改变旧 USB/Storage/IPC 身份契约。
- Keil：逐项源组/include/宏/Scatter 接入，保留 CM4 和原启动配置。
- Tests：真实生产联合场景及显式实板验收生产者；全部历史失败保留。
- CM4、Media、BSP、USB/Storage 服务及上游 lwIP 相对本轮基线无生产差异。

完整二进制 Diff 与文件状态：Evidence/Final/implementation.patch.gz、implementation.files.txt。
相对 main 的累计开发 Diff：ZIP 内 Diff/full-mainline.patch.gz、Evidence/Final/full-mainline.files.txt；包含之前已审核历史，不等于本轮新增范围。
未压缩 Diff、正式/独立固件及完整 MAP 在本地验收 ZIP；构建产物不进入 Git。

```text
 CM7/ApplicationV3/Core/app_messages.c              |   33 +-
 CM7/ApplicationV3/Core/app_messages.h              |    5 +-
 CM7/ApplicationV3/Core/app_services.c              |   16 +-
 CM7/ApplicationV3/Runtime/queue_pool.c             |    6 +-
 CM7/ApplicationV3/Services/Camera/camera_service.c |    5 +
 .../Services/Network/network_frames.c              |  226 +
 .../Services/Network/network_frames.h              |   22 +
 .../Services/Network/network_service.c             |  372 ++
 .../Services/Network/network_service.h             |   73 +
 CM7/ApplicationV3/Tasks/service_tasks.c            |   34 +-
 CM7/ApplicationV3/Tasks/supervisor.h               |    4 +
 CM7/Core/Src/main.c                                |    8 +
 CM7/Service/Ethernet/eth_http.c                    |   47 +
 CM7/Service/Ethernet/eth_http.h                    |    9 +
 CM7/Service/Ethernet/eth_jpeg_copy.c               |   67 +
 CM7/Service/Ethernet/eth_jpeg_copy.h               |   28 +
 Common/Inc/runtime_event.h                         |    1 +
 Docs/Ethernet/Integration/Evidence/N1/build.json   |   38 +
 .../Ethernet/Integration/Evidence/N1/cm4-build.log |   57 +
 .../Integration/Evidence/N1/cm7_jpeg-build.log     |  137 +
 .../Integration/Evidence/N1/cm7_raw-build.log      |  137 +
 Docs/Ethernet/Integration/Evidence/N1/e4.log       |   26 +
 Docs/Ethernet/Integration/Evidence/N1/e42.log      |   33 +
 Docs/Ethernet/Integration/Evidence/N1/e5.log       |   41 +
 Docs/Ethernet/Integration/Evidence/N1/e6.log       |   43 +
 Docs/Ethernet/Integration/Evidence/N1/e7-qvga.log  |   61 +
 Docs/Ethernet/Integration/Evidence/N1/e7.log       |   61 +
 .../Integration/Evidence/N1/source-manifest.json   |   21 +
 .../Integration/Evidence/N1/v3-prior-failure.log   |  124 +
 .../Integration/Evidence/N1/v3-verified.log        |  124 +
 .../Ethernet/Integration/Evidence/N2/cm4-build.log |   57 +
 .../Integration/Evidence/N2/cm7_jpeg-build.log     |  137 +
 .../Integration/Evidence/N2/cm7_raw-build.log      |  137 +
 .../Integration/Evidence/N2/draft-build.json       |   38 +
 .../Integration/Evidence/N2/lifecycle-results.json | 1160 ++++
 .../Ethernet/Integration/Evidence/N2/lifecycle.log |  132 +
 .../Integration/Evidence/N2/prior-build.json       |   38 +
 .../Integration/Evidence/N2/source-manifest.json   |   18 +
 .../Integration/Evidence/N2/verified-build.json    |   38 +
 .../Evidence/N3_N4_Preflight/browser-reset.log     |  154 +
 .../N3_N4_Preflight/ethernet-host-results.json     |    8 +
 .../ipc-status-baseline-failure.log                |   12 +
 .../N3_N4_Preflight/prior-compile-failure.log      |   36 +
 .../N3_N4_Preflight/prior-production-manifest.json |  408 ++
 .../N3_N4_Preflight/prior-renewal-failure.log      |  152 +
 .../N3_N4_Preflight/prior-supervisor-results.json  | 1286 ++++
 .../production-service-results.json                | 1398 ++++
 .../N3_N4_Preflight/production-service.log         |  156 +
 .../N3_N4_Preflight/related-camera_i2c_O0.log      |   25 +
 .../N3_N4_Preflight/related-camera_i2c_O2.log      |   25 +
 .../N3_N4_Preflight/related-frame_dispatcher.log   |    4 +
 .../N3_N4_Preflight/related-ipc_status_legacy.log  |   12 +
 .../Evidence/N3_N4_Preflight/related-ipc_v2.log    |    5 +
 .../N3_N4_Preflight/related-media_lease.log        |   70 +
 .../Evidence/N3_N4_Preflight/related-oled.log      |   59 +
 .../Evidence/N3_N4_Preflight/related-results.json  |   38 +
 .../N3_N4_Preflight/related-storage_fs.log         |    4 +
 .../N3_N4_Preflight/related-uvc_reader.log         |   24 +
 .../Evidence/N3_N4_Preflight/renewal-fixed.log     |  152 +
 .../Evidence/N4_DisconnectFix/after.json           | 1524 +++++
 .../Evidence/N4_DisconnectFix/after.log            |  170 +
 .../Evidence/N4_DisconnectFix/analysis.md          |    8 +
 .../Evidence/N4_DisconnectFix/before.json          | 1524 +++++
 .../Evidence/N4_DisconnectFix/before.log           |  170 +
 .../Integration/Evidence/N4_FinalFixes/analysis.md |    8 +
 .../N4_FinalFixes/main-ipc-status-failure.log      |   12 +
 .../prior-acceptance-build-failure.log             |  142 +
 .../Evidence/N4_FinalFixes/production-config.json  | 1506 +++++
 .../Evidence/N4_FinalFixes/production-config.log   |  168 +
 .../N4_FinalFixes/stopped-timer-after.json         | 1506 +++++
 .../Evidence/N4_FinalFixes/stopped-timer-after.log |  168 +
 .../N4_FinalFixes/stopped-timer-before.json        | 1506 +++++
 .../N4_FinalFixes/stopped-timer-before.log         |  168 +
 .../Integration/Evidence/N5_Producer/results.json  | 1452 ++++
 .../Integration/Evidence/N5_Producer/run.log       |  162 +
 .../Integration/Evidence/N5_Producer/scope.md      |    4 +
 .../Integration/Evidence/N5_ReadyGate/results.json | 1524 +++++
 .../Integration/Evidence/N5_ReadyGate/run.log      |  170 +
 .../Integration/Evidence/N5_ReadyGate/scope.md     |   10 +
 Docs/Ethernet/Integration/N2_ServiceLifecycle.md   |   23 +-
 Docs/Ethernet/Integration/N3_MediaIntegration.md   |   23 +-
 MDK-ARM/h7_mac.uvprojx                             |   22 +-
 MDK-ARM/h7_mac_CM7_custom.sct                      |   73 +-
 Platform/Ethernet/eth_execution.c                  |   33 +
 Platform/Ethernet/eth_execution.h                  |   16 +
 Platform/Ethernet/eth_mac.c                        |   22 +
 Platform/Ethernet/eth_mac.h                        |    4 +
 Platform/Ethernet/eth_memory.c                     |   10 +
 Platform/Ethernet/eth_netif.c                      |    9 +
 Platform/Ethernet/eth_network.c                    |   98 +-
 Platform/Ethernet/eth_network.h                    |    8 +
 Platform/Ethernet/eth_tcp.c                        |   74 +
 Platform/Ethernet/eth_tcp.h                        |    6 +
 Platform/Ethernet/eth_transport.c                  |   50 +-
 Platform/Ethernet/eth_transport.h                  |    6 +
 Platform/Ethernet/eth_udp.c                        |   13 +
 Platform/Ethernet/lwipopts.h                       |   16 +-
 Tests/ApplicationV3/hardware_fixture.c             |   42 +
 Tests/ApplicationV3/network_fixture.c              |  353 +
 Tests/ApplicationV3/run_tests.py                   |  267 +-
 Tests/ApplicationV3/startup_test.c                 | 7068 +++++++++++---------
 Tests/CameraLifecycleC1/run_tests.py               |    3 +-
 Tests/Ethernet/build_firmware.py                   |   24 +-
 Tests/Ethernet/build_production.py                 |  147 +
 Tests/Ethernet/e4_sources.py                       |    4 +-
 Tests/Ethernet/e7_camera_provider.c                |   78 +-
 Tests/Ethernet/network_board_acceptance.c          |  140 +
 Tests/Ethernet/network_board_acceptance.h          |   39 +
 Tests/Ethernet/run_e6_host.py                      |    2 +-
 Tests/Ethernet/run_production_matrix.py            |  167 +
 Tests/Ethernet/test_e4_network.c                   |   34 +
 Tests/Ethernet/test_mac.c                          |   12 +
 112 files changed, 24916 insertions(+), 3514 deletions(-)
```

累计 main Diff 压缩件约 43 MB，保留在验收 ZIP，不在 Git 重复保存既有历史二进制。
其 SHA-256 与大小见 Evidence/Final/full-mainline-diff.json；本轮实现 Diff 压缩件已随文档入库。
