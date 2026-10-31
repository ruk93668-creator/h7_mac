# UVC生命周期与接口请求测试

在仓库根目录执行`python Tests/UvcLifecycle/run_tests.py`。需要Windows ARMCC5、Unicorn与pyelftools；工具路径在执行器中，Python依赖需可导入。

## 覆盖内容

- 原12项：空参数、配置编号、重复Init、固定池耗尽、上下文清零/Alt0/configured、重复DeInit、延迟归还、重新Init、Class操作表及FS描述符。
- 新28项：上下文/设备状态、标准接口类型、请求方向/长度/value、接口编号及保留高位、只允许Alt0、GET回复的持久内存地址、非法请求不改变状态。
- 真实ST Core分发GET/SET到UVC_Setup，验证两个接口、Core状态阶段、拒绝Alt1及不打开EP1。

40/40 PASS；原USB 204/204、EP0 IRQ 4/4再次通过。CM7增量编译0 Error、0 Warning（本地cm7_build.log）。本轮未改CM4源码，未重编CM4。

SET_INTERFACE直接调用测试要求回调不产生传输、不修改EP0阶段；Core集成测试要求状态包提交成功、无重复提交导致的BUSY，并能正常完成。GET_INTERFACE验证发送源地址属于上下文，不能指向栈上临时变量。

集成测试仅借用既有TEST字符串回调补齐测试描述符、提供模拟就绪时钟，实际执行UVC操作表、Platform、ST Core及BSP。没有修改正式USB_UVC_Register的描述符表；其字符串回调仍未完成，本次不代表UVC注册/枚举或视频功能通过。HAL端点操作由桩模拟，未进行实板验证。

USBD_free无返回值；DeInit返回USBD_OK表示申请归还，实际回收由Core生命周期协调。

编译命令、结果JSON和日志为本地生成文件，不提交。测试依赖及历史迁移脚本不加入工程。
