import hashlib
import json
from pathlib import Path
import re
import shutil

here = Path(__file__).resolve().parent
root = here.parent.parent
paths = {
    'camera': 'Tests/CameraCleanup/Camera/results.json',
    'dcmi': 'Tests/DcmiCapture/results.json',
    'dcmi_init': 'Tests/DcmiCapture/Init/results.json',
    'sdram': 'Tests/DcmiInit/Sdram/production_results.json',
    'oled': 'Tests/Ov5640Migration/cm4_results.json',
    'archived_single_frame': 'Tests/CameraSingleFrame/Camera/results.json',
}
groups = {}
for name, relative in paths.items():
    dest = here / (name + '_results.json')
    shutil.copyfile(root / relative, dest)
    groups[name] = json.loads(dest.read_text(encoding='utf-8'))['summary']
assert all(s['failed'] == 0 for s in groups.values())
active = [groups[k] for k in ['camera', 'dcmi', 'dcmi_init', 'sdram', 'oled']]
summary = {'production_related_total': sum(s['total'] for s in active), 'failed': 0,
           'production_related_assertions': sum(s['assertions'] for s in active), 'groups': groups}
(here / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')

base = json.loads((here / 'baseline.json').read_text(encoding='utf-8'))
changed = [name for name, value in base.items() if (root / name).is_file()
           and hashlib.sha256((root / name).read_bytes()).hexdigest() != value]
deleted = [name for name in base if not (root / name).exists()]
protected = json.loads((here / 'protected_data.json').read_text(encoding='utf-8'))
missing = [name for name in protected if not (root / name).is_file()]
updated = [name for name, value in protected.items() if (root / name).is_file()
           and hashlib.sha256((root / name).read_bytes()).hexdigest() != value]
assert not missing
assert all(name.endswith('CameraSingleFrame\\Camera\\results.md') for name in updated), updated
concurrent = ['CM7\\Device\\Src\\ov5640_device.c']
owned_changed = [name for name in changed if name not in concurrent]
assert len(owned_changed) == 6 and len(deleted) == 2, (changed, deleted)
for name in concurrent:
    # Current user edit only disables colorbars; do not overwrite it.
    restored = (root / name).read_bytes().replace(b'COLORBAR_MODE_DISABLE', b'COLORBAR_MODE_ENABLE')
    assert hashlib.sha256(restored).hexdigest() == base[name]
# 旧接口不得出现在任何C、头文件或Python源码中，仅历史文档允许解释。
retired = 'CameraCapture_' + 'GetLastStopResult'
hits = []
for folder in ['CM7', 'CM4', 'Common', 'Board', 'Tests']:
    for path in (root / folder).rglob('*'):
        if path.is_file() and path.suffix in ['.c', '.h', '.py']:
            if retired in path.read_bytes().decode('utf-8', errors='ignore'):
                hits.append(str(path.relative_to(root)))
assert not hits, hits
map_text = (root / 'MDK-ARM/h7_mac_CM7/h7_mac_CM7.map').read_text(errors='replace')
assert not any(name in map_text for name in ['App_CameraCheck', 'g_camera_check', retired])
audit = {'checked_production_files': len(base), 'changed': changed, 'deleted': deleted,
         'cleanup_changed': owned_changed, 'concurrent_changes_preserved': concurrent,
         'preserved_files': len(protected), 'missing_preserved_files': missing,
         'updated_live_test_reports': updated, 'retired_interface_code_hits': hits,
         'diagnostics_absent_from_final_map': True}
(here / 'source_audit.json').write_text(json.dumps(audit, ensure_ascii=False, indent=2), encoding='utf-8')

report = '''# 正式固件测试代码清理

日期：2026-10-20。按用户确认，只清理正式固件；独立Tests、实板数据和验收报告保留。按追加要求，旧停止恢复查询接口在历史源码和测试调用中也删除，仅历史说明保留名称和设计记录。

## 修改清单

| 文件 | 修改 |
|---|---|
| CM7/Application/app_camera_check.c/.h | 从正式工程删除，临时统计模块仅归档在Tests。 |
| CM7/Application/app_camera.c | 移除Check调用、Watch记录、CRC/像素统计、开机自动单帧采集和自动取帧/释放。保留唯一160×120 Y8配置、Device配置、Media初始化及Process。 |
| CM7/Application/app_camera.h | 更新正式接口注释，不再描述单帧验收。 |
| CM7/Device/Inc/ov5640_device.h | 接口注释去掉“彩条”，与当前输出配置一致，不改变接口。 |
| CM7/Media/Src/camera_capture.c、Inc/camera_capture.h | 删除旧停止恢复查询接口及仅用于该接口的stop_result字段；已有停止恢复状态转换不变。 |
| MDK-ARM/h7_mac.uvprojx | 移除两个正式诊断文件的Keil登记。 |

本轮清理没有修改BSP、HAL、Device配置、Framework、Scatter、双核启动或CM4源码。核对期间发现外部将正式Device的彩条开关改为COLORBAR_MODE_DISABLE；该改动原样保留，并按最新关闭彩条的160×120 Y8配置重跑当前测试。历史彩条Device仅作为Tests快照，不改回正式配置。

## 清理后的流程

```text
原App_Init → App_CameraInit
    → 获取Device、配置OV5640
    → 初始化Media
    → 不自动Start

原App_Run → App_CameraRun → CameraCapture_Process
```

Application不读取底层恢复细节。Media内部仍负责采集完成、超时和错误恢复；业务可通过现有Start/GetFrame/ReleaseFrame/Stop等Media接口显式发起和消费图像。Application不会自动读取或丢弃READY帧。

## 旧接口与历史测试

历史设计曾有 `@RETIRED_STOP_QUERY@()`，现已从所有C/头文件/Python源码中删除。当前正式接口和历史源码均无该函数的声明、实现或调用；只在历史文档中解释其存在过。

Tests/CameraSingleFrame保留独立历史统计测试。其诊断入口和Media快照不编入正式Keil Target。停止结果仅由测试桩观察并记录，不通过Application查询。已停用会重建旧设计的一次性迁移/报告生成工具，历史测试执行入口保留。

## 验收

| 项目 | PASS / 总数 | 断言 |
|---|---:|---:|
| 当前摄像头配置54项 + 清理流程8项 | 62/62 | 1840 |
| DCMI采集BSP回归 | 46/46 | 6425 |
| DCMI初始化回归 | 20/20 | 1183 |
| SDRAM初始化回归 | 20/20 | 531 |
| CM4 OLED回归 | 12/12 | 3493 |
| 当前正式代码相关合计 | 160/160 | 13472 |

另：历史单帧套件82/82通过，图像转换工具11项检查通过；两者不计入当前正式固件的160项。

新增清理用例验证：未初始化轮询无副作用、初始化及千次轮询不自动采集、重复初始化不重配、显式启动后业务自行取帧释放、启动失败/超时/恢复失败均由Media处理、READY帧不会被App_Run或重复初始化回收。未用历史Application快照替代当前Application验证。

CM7最终全量重编译：0 Error、0 Warning，日志cm7_final_keil.log；最后接口注释更新后增量编译同样0 Error、0 Warning，日志cm7_latest_keil.log。CM4全量重编译：0 Error、0 Warning，日志cm4_keil.log。期间一次Keil重编译因临时via参数文件缺失出现C3904U，失败日志cm7_keil.log保留，随后单独重编译通过，未据此修改业务源码。

最终CM7 Map中无App_CameraCheck、g_camera_check或旧查询接口；源码全局检查同样无旧查询声明/实现/调用。

## 保留结果

原实板HEX、19200字节BIN、160×120 PNG、4倍预览、图像统计和主要验收报告均未修改。Tests中既有文件无丢失；历史单帧套件的实时结果表随本轮测试刷新，原主验收报告保留。完整清单和哈希核对见source_audit.json。

未下载、复位或重新操作实板；此次是正式源码清理、编译和模拟回归。

## 执行命令

```powershell
python Tests/CameraCleanup/Camera/run_tests.py
python Tests/DcmiCapture/run_tests.py
python Tests/DcmiCapture/Init/run_tests.py
python Tests/DcmiInit/Sdram/run_production.py
python Tests/Ov5640Migration/run_cm4_regression.py
python Tests/CameraSingleFrame/Camera/run_tests.py
python Tests/CameraVisualization/test_view_frame.py
```

独立ARMCC编译/链接命令见Camera/commands.json，逐项断言结果见Camera/results.json。历史接口说明见Tests/CameraSingleFrame/history.md。
'''
(here / '清理报告.md').write_text(report.replace('@RETIRED_STOP_QUERY@', retired), encoding='utf-8')
print('Production-related regressions:', summary['production_related_total'], 'PASS; code hits:', hits)
print('Cleanup changed:', len(owned_changed), 'deleted:', len(deleted), '; concurrent Device setting preserved.')
