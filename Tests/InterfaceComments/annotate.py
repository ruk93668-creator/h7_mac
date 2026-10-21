import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
changes = []
interface_count = 0

# 按当前实现描述接口，只修改注释。
def annotate(relative, descriptions, replacements=()):
    global interface_count
    path = root / relative
    original = path.read_bytes()
    text = original.decode('gbk').replace('\r\n', '\n')
    for old, new in replacements:
        assert text.count(old) == 1, (relative, old)
        text = text.replace(old, new, 1)
    for marker, description in descriptions:
        assert text.count(marker) == 1, (relative, marker)
        indent = re.match(r'[ \t]*', marker).group()
        text = text.replace(marker, indent + '//' + description + '\n' + marker, 1)
        interface_count += 1
    newline = '\r\n' if b'\r\n' in original else '\n'
    updated = text.replace('\n', newline).encode('gbk')
    # 按C词法保留字符串/字符常量，只忽略注释和空白。
    def tokens(data):
        source = data.decode('gbk')
        pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\r\n]*|\s+|[A-Za-z_][A-Za-z_0-9]*|[0-9]+|.'
        return [t for t in re.findall(pattern, source, re.S)
                if not t.isspace() and not t.startswith(('//', '/*'))]
    assert tokens(original) == tokens(updated), relative
    if original != updated:
        path.write_bytes(updated)
        changes.append({'path': relative, 'before_sha256': hashlib.sha256(original).hexdigest(),
                        'after_sha256': hashlib.sha256(updated).hexdigest(), 'code_tokens_unchanged': True})

for core in ['CM7', 'CM4']:
    annotate(f'{core}/Application/app_main.h', [
        ('SysResult_t App_Init(void);', '初始化当前核心的应用模块'),
        ('void App_Run(void);', '轮询当前核心的应用模块，由主循环持续调用'),
    ])
    annotate(f'{core}/Device/Inc/device.h', [
        ('#define DEVICE_EXPORT(obj)', '导出静态设备对象，由设备注册表自动发现'),
        ('    SysResult_t (*init)(Device_t *dev);', '执行具体设备的初始化操作'),
        ('    SysResult_t (*start)(Device_t *dev);', '执行具体设备的启动操作'),
        ('    SysResult_t (*stop)(Device_t *dev);', '执行具体设备的停止操作'),
        ('SysResult_t Device_Register(Device_t *dev);', '注册未初始化设备，拒绝重复对象或名称'),
        ('Device_t *Device_Find(const char *name);', '按名称查找已注册设备，未找到时返回NULL'),
        ('SysResult_t Device_Init(Device_t *dev);', '初始化已注册设备，成功后进入READY状态'),
        ('SysResult_t Device_Start(Device_t *dev);', '启动READY设备，成功后进入RUNNING状态'),
        ('SysResult_t Device_Stop(Device_t *dev);', '停止RUNNING设备，成功后回到READY状态'),
    ], [
        ('/* 自动遍历链接段中的设备声明 */', '//遍历设备导出表，注册所有静态设备'),
        ('/* 遍历 Registry，初始化符合策略的设备 */', '//初始化已注册且auto_init为true的设备，首次失败即返回'),
    ])
    annotate(f'{core}/Framework/framework_init.h', [
        ('typedef SysResult_t (*FrameworkInitFunc_t)(void);', '模块初始化回调，成功返回SYS_OK'),
        ('#define FRAMEWORK_INIT_REGISTER(func, init_level)', '将初始化函数和等级登记到自动初始化表'),
        ('SysResult_t Framework_InitRuntime(void);', '按等级执行模块初始化，首次失败即返回'),
    ])
    annotate(f'{core}/Platform/ipc_platform.h', [], [
        (f'/* {core} IPC platform interface. */', '//执行内存屏障，保证共享数据访问顺序'),
    ])
    annotate(f'{core}/Platform/platform_time.h', [
        ('uint32_t Platform_TimeGetMs(void);', '获取当前核心的毫秒Tick，计数允许回绕'),
    ])

annotate('CM7/BSP/bsp_sdram.h', [
    ('SysResult_t BSP_SDRAM_Init(void);', '初始化FMC、SDRAM时钟和芯片时序'),
    ('bool BSP_SDRAM_IsReady(void);', '查询SDRAM初始化是否已成功完成'),
])
annotate('CM7/BSP/bsp_dcmi.h', [
    ('SysResult_t BSP_DCMI_StartSnapshot(', '启动一次Snapshot采集，将图像写入指定缓冲区'),
])
annotate('CM7/Service/ipc_service.h', [], [
    ('/* CM7 IPC service interface. */', '//唤醒CM4前初始化共享区，最后发布有效标志'),
])
annotate('CM4/Application/app_oled.h', [], [
    ('//获取OLED、绘制横线并启动100ms周期刷新', '//获取OLED、清空帧缓冲并启动100ms周期刷新'),
])
annotate('CM4/BSP/bsp_iic.h', [
    ('SysResult_t BSP_IIC_WriteAsync(', '复制数据并启动I2C2异步发送，结果通过TakeResult读取'),
    ('bool BSP_IIC_TakeResult(', '轮询异步发送结果并检查超时，有结果时填写result并返回true'),
], [
    ('''/* CM4 owns I2C2 on PB10/PB11. Called automatically at INIT_LEVEL_BUS.
 * Success is idempotent; initialization is for the cooperative main context.
 * Requires the 120 MHz I2C2 kernel clock used to calculate the timing value.
 * Returns INVALID_STATE for a clock mismatch, or the mapped HAL failure.
 */''', '//通过I2C2阻塞发送数据，返回本次发送结果'),
])
annotate('Common/Inc/framework_event.h', [
    ('typedef void (*FrameworkEventHandler_t)(', '处理已出队事件，event仅在本次回调期间有效'),
    ('SysResult_t Framework_EventSubscribe(', '按事件ID注册处理函数，拒绝重复订阅'),
    ('SysResult_t Framework_EventPost(', '复制事件到FIFO队尾，等待主循环分发'),
    ('void Framework_EventDispatch(void);', '每轮最多分发4个事件，调用匹配的订阅回调'),
])
annotate('Common/Inc/framework_list.h', [
    ('#define FRAMEWORK_CONTAINER_OF(ptr, type, member)', '根据成员地址取得所属结构体地址'),
    ('static inline void Framework_ListInit(', '初始化节点，使前后指针均指向自身'),
    ('static inline bool Framework_ListIsEmpty(', '检查以head为头节点的链表是否为空'),
    ('static inline void Framework_ListAddTail(', '将未入链节点插入链表尾部'),
    ('static inline void Framework_ListRemove(', '从链表移除节点，并将节点恢复为自环'),
])
annotate('Common/Inc/framework_runtime.h', [
    ('void Framework_Run(void);', '执行一轮事件分发和定时器处理'),
])
annotate('Common/Inc/framework_timer.h', [
    ('typedef void (* FrameworkTimerCallback_t)', '定时器到期回调，context来自创建配置'),
    ('SysResult_t Framework_TimerCreate(', '按配置创建定时器并返回Handle，创建后不自动启动'),
    ('SysResult_t Framework_TimerStart(', '启动定时器，已启动时从当前时间重新计时'),
    ('SysResult_t Framework_TimerStop(', '停止定时器，保留对象和Handle以便再次启动'),
    ('SysResult_t Framework_TimerDelete(', '删除定时器，使旧Handle失效'),
    ('void Framework_TimerProcess(void);', '检查到期定时器，每轮最多处理一个并同步执行回调'),
])
annotate('Common/Inc/ipc_shared.h', [
    ('extern volatile IpcShared_t g_ipc;', '双核共享的IPC协议对象，由Service维护'),
])

out = Path(__file__).resolve().parent / 'audit.json'
out.write_text(json.dumps({'modified_files': len(changes), 'added_interface_comments': interface_count,
                           'all_code_tokens_unchanged': True, 'files': changes}, ensure_ascii=False, indent=2),
               encoding='utf-8')
print('Modified headers:', len(changes))
print('Added interface comments:', interface_count)
print('All code tokens unchanged; encoding and line endings preserved.')
for row in changes:
    print(row['path'])
