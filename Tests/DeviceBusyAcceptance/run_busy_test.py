import argparse,json,struct,sys
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('image',type=Path)
parser.add_argument('--deps',type=Path,required=True)
parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args()
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0,str(args.deps))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
cases=[
 ('正常状态转换','Register→Init→Start→Stop','UNINIT→BUSY→READY→BUSY→RUNNING→BUSY→READY；回调各1次'),
 ('Init回调重入及交叉调用','Init回调内调用Init/Start/Stop','三个嵌套调用均返回BUSY=-5，无额外回调，外层成功后READY'),
 ('Start回调重入及交叉调用','Start回调内调用Init/Start/Stop','三个嵌套调用均返回-5，无额外回调，外层成功后RUNNING'),
 ('Stop回调重入及交叉调用','Stop回调内调用Init/Start/Stop','三个嵌套调用均返回-5，无额外回调，外层成功后READY'),
 ('已有BUSY状态拒绝操作','已注册对象设为BUSY，调用Init/Start/Stop','三个返回-5；保留BUSY，不执行回调'),
 ('缺失Init Ops','UNINIT时使用NULL lifecycle和NULL init','返回NOT_SUPPORTED=-7，保持UNINIT'),
 ('缺失Start Ops','READY时使用NULL lifecycle和NULL start','返回-7，保持READY'),
 ('缺失Stop Ops','RUNNING时使用NULL lifecycle和NULL stop','返回-7，保持RUNNING'),
 ('静态导出自动注册','两个真实DEVICE_EXPORT项；RegisterAll后Find','均进入Registry，查找返回原对象，链表正确，无Init回调'),
 ('auto_init策略','manual=false，automatic=true；Device_InitAll','manual只注册、保持UNINIT；automatic执行1次Init并READY'),
 ('真实Initcall链','调用实际Framework_InitRuntime','BUS注册，DEVICE按策略初始化，后续等级继续'),
 ('Init回调失败','Init回调返回IO=-9','回调期间BUSY，原样返回-9，退出BUSY并进入ERROR'),
 ('Start回调失败','Start回调返回IO=-9','原样返回-9，退出BUSY并进入ERROR'),
 ('Stop回调失败','Stop回调返回IO=-9','原样返回-9，退出BUSY并进入ERROR'),
]
fields=['failure_line','outer_result','nested_init','nested_start','nested_stop','init_calls','start_calls','stop_calls','busy_seen','final_state','auto_state','level_trace','list_count','auto_init_calls']
with args.image.open('rb') as f:
 elf=ELFFile(f);entry=elf.header['e_entry']
 symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 exports=elf.get_section_by_name('ER_DEVICE_EXPORT')
 assert exports['sh_size']==8 and struct.unpack('<2I',exports.data())==tuple(symbols[n] for n in ['manual','automatic'])
 assert 0x08100000<=exports['sh_addr']<0x08200000
states=['UNINIT','READY','RUNNING','BUSY','ERROR']
rows=[]
for case_id,(name,inputs,expected) in enumerate(cases):
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 uc.mem_map(0x08100000,0x100000);uc.mem_map(0x10000000,0x48000);uc.mem_map(0x01000000,0x1000)
 for address,data in sections:uc.mem_write(address,data)
 uc.reg_write(UC_ARM_REG_SP,0x10047FF0);uc.reg_write(UC_ARM_REG_LR,0x01000001);uc.reg_write(UC_ARM_REG_R0,case_id)
 error=None
 try:uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<Iiiii9I',uc.mem_read(symbols['g_busy_result'],56))))
 passed=error is None and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and state['failure_line']==0
 actual=f"返回{state['outer_result']}；嵌套Init/Start/Stop={state['nested_init']}/{state['nested_start']}/{state['nested_stop']}；回调次数={state['init_calls']}/{state['start_calls']}/{state['stop_calls']}；回调见BUSY={state['busy_seen']}；manual/automatic={states[state['final_state']]}/{states[state['auto_state']]}；Registry={state['list_count']}"
 if not passed:actual+=f"；失败行{state['failure_line']}；异常{error}"
 row=dict(name=name,input=inputs,expected=expected,actual=actual,result='Pass' if passed else 'Fail',state=state)
 rows.append(row);print(f"{case_id+1:02d} {row['result']}: {name}: {actual}")
args.report.mkdir(parents=True,exist_ok=True)
(args.report/'busy_results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
lines=['# CM4 Device BUSY 定向验收','',
 '直接使用当前CM4 Device和Framework Initcall源码；静态设备采用真实DEVICE_EXPORT宏；时间和硬件不参与。',
 '每个测试使用全新仿真状态，恢复ELF初始化数据，模拟生命周期回调检查BUSY与设备指针。','',
 '| 测试 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|']
lines += [f"| {r['name']} | {r['input']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',f"结果：{sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass。",'',
 '所有Init/Start/Stop回调内部的3×3嵌套操作均检查返回-5、BUSY不被覆盖及回调计数不增加。',
 '两个静态导出项的Flash表大小8字节，指针与实际设备对象地址一致；注册后检查Registry双向链表与Find对象身份。',
 '全Target构建日志及Warning另见cm4_keil.log；本报告不代表上板并发测试。']
(args.report/'busy_results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f"Total: {sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass")
raise SystemExit(0 if all(r['result']=='Pass' for r in rows) else 1)
