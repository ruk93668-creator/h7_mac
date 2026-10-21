import argparse
import json
from pathlib import Path
import struct
import sys

parser=argparse.ArgumentParser()
parser.add_argument('image',type=Path)
parser.add_argument('--deps',type=Path,required=True)
parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args()
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0,str(args.deps))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

cases=[
 ('正常自动启动','导出a/b/c；auto_init=true/false/true；调用Framework_InitRuntime','注册3个；只初始化a、c；后续等级继续','正常'),
 ('全部手动初始化','3个auto_init均false','注册3个，无初始化回调，启动成功','正常'),
 ('空导出指针','测试注入：第二个导出项指针为NULL','返回-1；保留第一个注册项；不初始化、不执行后续等级','失败中止'),
 ('重复导出指针','测试注入：第二个导出项也指向a','返回-3；链表仅一个a，无重复节点','失败中止'),
 ('无效设备名称','第二个设备名称为空字符串','返回-1；注册中止，c未注册','失败中止'),
 ('非法首次注册状态','第二个设备预设READY','返回-4；注册中止，c未注册','失败中止'),
 ('跳过无回调的手动设备','b.auto_init=false且lifecycle=NULL','b不初始化，不阻塞a、c或后续等级','正常'),
 ('自动设备缺失回调','b.auto_init=true且lifecycle=NULL','返回-7；a已READY；b/c未初始化，后续等级停止','失败中止'),
 ('初始化回调失败','c.init返回IO=-9','原样返回-9；a保留READY，c进入ERROR，后续等级停止','失败中止'),
 ('重复RegisterAll','连续调用两次Device_RegisterAll','第二次返回-3；无链表损坏；非幂等','策略边界'),
 ('重复InitAll','初始化成功后再次调用Device_InitAll','第二次返回-4；不会重复调用已READY设备回调','策略边界'),
 ('提前手动初始化','RegisterAll后手动Init(a)，再InitAll','返回-4并中止；c仍未初始化','策略边界'),
 ('未注册先InitAll','空Registry直接调用Device_InitAll','返回0，无初始化；不会检查是否执行过RegisterAll','策略边界'),
 ('Init回调重入自身','a.init第一次调用Device_Init(a)','应拒绝重入，不得重复执行同一设备的初始化回调','安全检查'),
 ('初始化时追加设备','a.init注册auto_init=true的extra','当前实现在同轮访问新增extra；回调顺序a→c→extra','策略边界'),
]
fields=['failure_line','result','secondary_result','callbacks','callback_trace','level_trace','registered',
        'state_a','state_b','state_c','state_extra','list_checks']
with args.image.open('rb') as f:
 elf=ELFFile(f)
 entry=elf.header['e_entry']
 symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 export_section=elf.get_section_by_name('ER_DEVICE_EXPORT')
 export_base=export_section['sh_addr']
 assert export_section['sh_size']==12
 assert struct.unpack('<3I',export_section.data())==tuple(symbols[n] for n in ['a','b','c'])
 assert 0x08100000<=export_base<0x08200000
rows=[]
states=['UNINIT','READY','RUNNING','ERROR']
for case_id,(name,inputs,expected,category) in enumerate(cases):
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 uc.mem_map(0x08100000,0x100000)
 uc.mem_map(0x10000000,0x48000)
 uc.mem_map(0x01000000,0x1000)
 # Restore initialized execution sections, modeling completed C runtime startup.
 for address,data in sections: uc.mem_write(address,data)
 if case_id==2: uc.mem_write(export_base+4,struct.pack('<I',0))
 if case_id==3: uc.mem_write(export_base+4,struct.pack('<I',symbols['a']))
 uc.reg_write(UC_ARM_REG_SP,0x10047FF0)
 uc.reg_write(UC_ARM_REG_LR,0x01000001)
 uc.reg_write(UC_ARM_REG_R0,case_id)
 error=None
 try: uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
 except Exception as exc: error=str(exc)
 observation=dict(zip(fields,struct.unpack('<Iii9I',uc.mem_read(symbols['g_auto_result'],48))))
 completed=error is None and uc.reg_read(UC_ARM_REG_PC)==0x01000000
 passed=completed and observation['failure_line']==0 and uc.reg_read(UC_ARM_REG_R0)==0
 actual=f"返回{observation['result']}；第二次/回调内操作返回{observation['secondary_result']}；注册{observation['registered']}；回调次数{observation['callbacks']}，顺序{observation['callback_trace']}；后续等级轨迹{observation['level_trace']}；a/b/c/extra状态="
 actual+='/'.join(states[observation[n]] for n in ['state_a','state_b','state_c','state_extra'])
 if not passed: actual+=f"；失败断言行{observation['failure_line']}；仿真异常={error}"
 row=dict(case=case_id,name=name,input=inputs,expected=expected,actual=actual,category=category,result='Pass' if passed else 'Fail',observation=observation,execution_completed=completed)
 rows.append(row)
 print(f"{case_id+1:02d} {row['result']}: {name}: {actual}")
args.report.mkdir(parents=True,exist_ok=True)
summary=dict(scope='CM4 actual Device and Framework Initcall sources; isolated ARM simulation; no production edits',
             export_address=f'0x{export_base:08X}',export_table_bytes=12,results=rows)
(args.report/'auto_results.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
lines=['# CM4 Device 自动注册与初始化仿真','',
       '使用实际device.c、framework_init.c、DEVICE_EXPORT宏及真实链接段边界；模拟设备和生命周期回调，无硬件。',
       '每个测试从全新仿真状态开始；NULL/重复导出项只在仿真镜像中注入，不修改生产Flash或源码。','',
       '| 测试 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|']
lines += [f"| {r['name']} | {r['input']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',f"{sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass；其余逐项见缺陷分析。",'',
          '等级轨迹1456表示PLATFORM及后续MEDIA/SERVICE/APPLICATION成功执行；轨迹1表示在BUS或DEVICE失败后中止。',
          '注册和初始化失败采用Fail-fast但不回滚：先前注册设备和已完成初始化的设备保留。',
          '重复调用的-3/-4是当前非幂等策略，不自动归为算法缺陷。',
          '本报告不代表正式Target链接或上板验收。OLED驱动尚未完成，不在模拟设备中补写其硬件实现。']
(args.report/'auto_results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
# Completed diagnosis may contain a deliberately exposed safety failure.
# Unexpected failures or emulator crashes still fail the runner.
unexpected=[r for r in rows if not r['execution_completed'] or (r['result']=='Fail' and r['case']!=13)]
print(f"Completed diagnosis: {sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass; unexpected failures={len(unexpected)}")
raise SystemExit(1 if unexpected else 0)
