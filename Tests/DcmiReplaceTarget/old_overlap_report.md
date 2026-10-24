# 完成目标旧缓冲区重叠检查验收

## 修改
- BSP_DCMI_ReplaceTarget读取本次完成目标的旧地址。
- 先确认旧整帧地址范围不会溢出；异常返回PROTOCOL并要求Stop。
- 新缓冲区与旧缓冲区相同、正向重叠或反向重叠，返回INVALID_ARG，恢复PRIMASK，不调用HAL改写地址。
- 两个整帧区间首尾相邻允许，原有与另一DMA目标的重叠检查保留。
- 保留短中文注释和原有DMA硬件操作顺序。

## 验证
```powershell
python Tests/DcmiReplaceTarget/run_tests.py
```
- 当前合计：133项，PASS 133，FAIL 0，断言7496。
- 在原119项回归基础上新增M0/M1各7项：同地址、两方向部分重叠、两方向相邻、旧地址溢出、PRIMASK=1恢复。
- 测试编译链接0 Error、0 Warning。
- CM7、CM4正式Target本轮增量构建均0 Error、0 Warning；见old_overlap_cm7.log与old_overlap_cm4.log。
- 无实板测试，原32 Word门限及DMA切换窗口限制不变。

|用例|结果|
|---|---|
|M0: same old address rejected|PASS|
|M0: old range forward overlap rejected|PASS|
|M0: old range backward overlap rejected|PASS|
|M0: old end adjacency allowed|PASS|
|M0: old start adjacency allowed|PASS|
|M0: old range overflow fault|PASS|
|M0: overlap restores PRIMASK=1|PASS|
|M1: same old address rejected|PASS|
|M1: old range forward overlap rejected|PASS|
|M1: old range backward overlap rejected|PASS|
|M1: old end adjacency allowed|PASS|
|M1: old start adjacency allowed|PASS|
|M1: old range overflow fault|PASS|
|M1: overlap restores PRIMASK=1|PASS|
