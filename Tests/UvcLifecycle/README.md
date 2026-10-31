# UVC上下文生命周期测试

从仓库根目录运行：`python Tests/UvcLifecycle/run_tests.py`。

要求Windows ARMCC5、Unicorn、pyelftools；工具路径与既有USB回归相同，Python依赖需可导入。测试复用既有HAL桩，直接包含真实usb_uvc_class.c及Platform固定内存池实现，不使用标准malloc。

12项覆盖：Init空句柄、配置0/2、重复Init、内存池耗尽、清零后configured为true/两个接口Alt0、DeInit空句柄、空上下文、正常及重复DeInit、Core延迟归还、释放后重新Init，以及Class操作表14个字段及FS描述符返回值。

本轮结果：12/12 PASS。本轮修改Class操作表，未修改Core/BSP；前轮原USB 204/204及EP0 IRQ 4/4通过记录仍为历史结果。

CM7增量编译：0 Error、1 Warning（s_uvc_class尚未接入注册流程，ARMCC5 #177未引用）。当前已建立回调实现及Class操作表，还没有可枚举的UVC Class；未进行实板UVC验证。CM4源码未修改，本轮未重编。

USBD_free没有返回值，UVC_DeInit的USBD_OK表示已申请归还。Core处于DEINITIALIZING时仍保留内存池占用，直到Core确认DeInit成功后回收；测试专门验证此语义。

编译命令、结果JSON和构建日志为本地生成文件，不提交；.gitignore已排除。
