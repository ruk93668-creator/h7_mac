# Cleanup 1.3 Keil 登记

仓库的 uvprojx 原本只在 CM7 登记代次分配编译单元。实际用户工作区的 CM4 还登记了同一文件。本轮只移除用户工作区的这一条 CM4 登记，共 219 字节，其他内容逐字节一致。仓库中没有可单独暂存的工程改动，用户配置仍只留在本地。

- 清理前 uvprojx SHA256 6a866dc2ea07abb459f760fc471312dc1f5a7b4cbbb63f921730ce441700a689。
- 清理后 uvprojx SHA256 3d4c0323c007cc87bc9db2fd0ba2999828c39e46674aaa6cc49d7b152432e60a。
- uvoptx SHA256 765e9e89ce48a74247281b7c3f9630203a3acc80c25ebf6b75569af4c7c70269，与开工时一致。全局文件选项仍用于 CM7。
- 使用前后实际用户工程配置的隔离副本完成 ARMCC5 V5.06u7 build 960 双核 Rebuild，均为 0 Error、0 Warning。CM4 的 Keil HEX 逐字节相同。
- 原 CM7 Target 关闭 HEX 输出。使用本次实际 AXF 经 fromelf 单独生成 HEX，工程选项保持。CM7 保留 40 B 代次函数，旧服务未进入最终符号表。CM4 不包含该函数。

仓库跟踪的原版 Target 配置也分别对基线和候选完成双核 Rebuild，均为 0 Error、0 Warning。日志为 b_Keil.log 与 ct_Keil.log。
