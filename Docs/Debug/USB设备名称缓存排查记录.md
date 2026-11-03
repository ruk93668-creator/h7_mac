# 固件写Roxy，Windows却显示UVC：用设备属性区分上报名称与友好名

记录日期：2026-11-03。**状态：名称不一致已由当前Windows属性解释；未执行卸载设备及重装验证。**

## 当时如何产生怀疑

已经将UVC功能字符串改成Roxy Camera并修复24B长度，Windows却再次显示UVC Camera。起初合理怀疑有：源码被覆盖、编译/烧录了旧目标、Windows保留了旧名。不能直接认定是缓存。

## 实际检查顺序

| 步骤 | 结果 | 能够说明什么 |
|---|---|---|
| 查看分支与实际源码 | codex/cm7-freertos-p0；字符串仍为Roxy Camera；有效24B | 没发现源码改回旧名，但仅看源码不能证明板上固件 |
| 读取当前1209:0001摄像头实例 | 状态OK，Camera子设备显示UVC Camera | 确认不是另一台同名摄像头 |
| 比较两个设备属性 | BusReportedDeviceDesc=Roxy Camera；FriendlyName=UVC Camera | Windows保存的友好名和总线上报描述不同，显示旧名不等于源码回退 |

属性读取针对`VID_1209&PID_0001&MI_00`摄像头子实例，避免把父级USB Composite Device的通用名称误当作摄像头功能名。

## 结论和没有做的事

本次有直接的本机PnP属性证据支持“显示友好名未更新”。提出卸载对应设备实例再拔插以刷新；未实际操作设备卸载，也没有改VID/PID、注册表或驱动。这个显示名称问题与NO_BANDWIDTH不是同一故障。

用于复核的命令：

```powershell
Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -like 'USB\VID_1209&PID_0001&MI_00*' }
```

再对实际InstanceId读取`DEVPKEY_Device_FriendlyName`及`DEVPKEY_Device_BusReportedDeviceDesc`。ffplay应使用DirectShow设备列表实际公布的名称。

## 面试30秒复述

> 我改了USB功能名称后，Windows仍显示旧名。起初不能排除旧固件，所以先核对分支、字符串和长度，再定位到相同VID/PID的摄像头实例。最终发现总线上报描述已经是Roxy Camera，但Windows FriendlyName仍是UVC Camera，说明这是设备属性显示层的问题。我因此没有再次修改USB字符串或随意更换VID/PID，也把它与真正导致取流失败的带宽错误分开处理。
