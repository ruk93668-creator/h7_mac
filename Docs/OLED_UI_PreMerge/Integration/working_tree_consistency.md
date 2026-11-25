# 工作目录一致性

- ApplicationV3 主线基线 `62b42d7c0b7df9eadafa00953ee8ce1229da9826`。OLED 开发基线 `7acc3bf8eaa8b41e31a8b6d41f624f97b53d57ec`。
- 构建源码提交 `72140cd55ebc82a8ce4d6e5d5c8a6c5f9a143c6a`。两条历史都是该提交的祖先，合并基为指定 main，基于该 main 引入时无冲突。
- 当前集成工作区的生产文件逐字节与该提交 Git blob 对比。正式构建来自该候选的 Git archive；Raw 副本只切换独立 profile 的 `APP_VIDEO_MJPEG_MODE=0` 定义。
- CM7、Common、Board、CM4 Device、Framework、IPC Service、Platform、I²C BSP 经 `git diff --name-only` 对比，差异为零。
- 原 OLED 工作目录及用户未提交的 Keil 本地设置保留在原分支工作树。候选的构建产物全部在独立目录。
