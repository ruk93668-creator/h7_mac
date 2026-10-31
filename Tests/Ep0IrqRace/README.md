# EP0 IRQ竞态回归

从仓库根目录运行`python Tests/Ep0IrqRace/run_race.py`。需要Windows ARMCC5、Unicorn和pyelftools；工具路径在执行器内，Python依赖需可导入。

四项场景：旧XFRC挂起、无旧XFRC、Abort失败保留所有权、STATUS OUT取消。检查新响应真实完成、其他端点保护、共享RX不被Flush及一次Abort。

执行未修改的HAL IRQ函数体和LL；端点操作、W1C寄存器与FIFO副作用为模型。C调用IRQ不验证真实NVIC/PHY或Windows枚举。与迁移审计脚本无依赖；历史合并结果见上级UsbStage4Migration/verification_report.md。
