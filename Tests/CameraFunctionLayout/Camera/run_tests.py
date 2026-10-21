import argparse,json,struct,subprocess,sys,hashlib
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parents[2]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
names=['正常Initcall配置160x120 Y8，彩条关闭', '错误芯片ID禁止写表', 'SCCB读ID地址NACK', '关键寄存器回读不一致-0', '关键寄存器回读不一致-1', '关键寄存器回读不一致-2', '关键寄存器回读不一致-3', '关键寄存器回读不一致-4', '关键寄存器回读不一致-5', '关键寄存器回读不一致-6', '关键寄存器回读不一致-7', '关键寄存器回读不一致-8', '关键寄存器回读不一致-9', '关键寄存器回读不一致-10', '关键寄存器回读不一致-11', '关键寄存器回读不一致-12', '关键寄存器回读不一致-13', '关键寄存器回读不一致-14', '关键寄存器回读不一致-15', '关键寄存器回读不一致-16', '写地址NACK', '寄存器高字节NACK', '寄存器低字节NACK', '数据字节NACK', '真实SCCB写后读', '初始化前禁止写', 'SDA忙拒绝写', 'SCL忙拒绝写', '软件复位等待5ms', '组件多字节和参数边界', '组件写表故障保留IO', '关键寄存器回读NACK', '配置失败后重试', '主循环及重复App_Init不重配不重启', 'Device参数和状态检查', 'PLL错误阻止初始化', 'DWT停止阻止初始化', 'SCCB读地址NACK', 'DWT回绕仍能完整配置']
names+=['应用缺少摄像头设备','应用Ops缺失及恢复','应用配置函数缺失及恢复']
names += ['只读配置来源及取值', 'NULL配置拒绝', '零宽度拒绝', '零高度拒绝',
          '未支持宽度拒绝', '未支持高度拒绝', '未知格式拒绝', '负数格式拒绝',
          '未支持分辨率组合拒绝', '有效配置不被Device修改', 'Application向Device传递同一配置指针',
          '未支持配置先于SCCB访问拒绝']
names += ['未初始化轮询无副作用', '初始化及千次轮询不自动采集', '重复初始化不重配不启动',
          '显式启动后完成帧交给业务释放', '显式启动失败仍由Media恢复', '恢复失败由Media内部重试',
          '采集超时由Media处理', '重复初始化不回收业务持有的帧']
names=names[:54]
for group in ['开机配置、帧长、完成交付及无热切换','格式写入失败及重新配置','错误芯片ID及恢复','采集启动失败向上传递','通信错误优先及Media恢复','超时及Media恢复','DMA长度与缓冲区保护','NULL、奇数YUV宽度及非法配置']:
 for fmt in ['Y8','RGB565','YUV422']: names.append(group+' / '+fmt)
commands=[]
def run(cmd):
 p=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True);text=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':subprocess.list2cmdline([str(x) for x in cmd]),'code':p.returncode,'output':text})
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(commands[-1]['command']);print(text,end='')
 if p.returncode:raise RuntimeError('Build failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for inc in ['Tests/CameraFunctionLayout/Camera/stubs','Drivers/CMSIS/Include','CM7/BSP','CM7/Device/Components/OV5640','CM7/Device/Inc','CM7/Application','CM7/Media/Inc','CM7/Framework','CM7/Platform','Common/Inc','Board/Inc']:args+=['-I',root/inc]
run(args+['-c',here/'camera_test.c','-o',here/'camera_test.o'])
for source in ['ov5640','ov5640_reg']:
 run(args+['-c',root/('CM7/Device/Components/OV5640/'+source+'.c'),'-o',here/(source+'.o')])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'ov5640.o',here/'ov5640_reg.o',here/'camera_test.o','--entry','Camera_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','--keep=*(.device_export)','--no_scanlib','-o',here/'camera_test.axf'])
with (here/'camera_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
fields=['fail_line','checks','tx_count','starts','stops','nacks','reads','reset_high','mco','delays','writes','verify_result_raw','verified_count','failed_reg','heap','last_bus_error','init_result']
rows=[]
for id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.mem_map(0x08000000,0x100000);u.mem_map(0x20000000,0x40000);u.mem_map(0x01000000,0x1000)
 u.mem_map(0xE0000000,0x10000);u.mem_map(0x58024000,0x1000)
 for a,d in sections:u.mem_write(a,d)
 u.mem_write(syms['s_camera_config']+4,struct.pack('<I',(id-54)%3 if id>=54 else 0))
 u.mem_write(0x58024400,struct.pack('<I',1))
 u.mem_write(0xE0001004,struct.pack('<I',0xFFFFFFF0 if id==38 else 0))
 def counter(uc,access,addr,size,value,data):
  if id!=36 and struct.unpack('<I',uc.mem_read(0xE0001000,4))[0]&1:
   n=struct.unpack('<I',uc.mem_read(0xE0001004,4))[0]
   uc.mem_write(0xE0001004,struct.pack('<I',(n+4800)&0xFFFFFFFF))
 u.hook_add(UC_HOOK_MEM_READ,counter,begin=0xE0001004,end=0xE0001007)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=100000000,timeout=30000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<16Ii',u.mem_read(syms['g_result'],68))))
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line']
 verified=state['verify_result_raw'] if state['verify_result_raw']<0x80000000 else state['verify_result_raw']-0x100000000
 actual=f"写入{state['writes']}次，首次Initcall={state['init_result']}，独立验收={verified}，已校验{state['verified_count']}项，失败寄存器0x{state['failed_reg']:04X}，Timer堆={state['heap']}，断言{state['checks']}项"
 if id>=54: expected='按当前格式配置并启动；寄存器、帧长、DMA计数、错误与所有权断言成立'
 elif id>=42: expected='公共配置取值、参数拒绝、不支持语义、无副作用或同一指针传递断言成立'
 elif 3<=id<=19: expected='正式Initcall成功；独立回读返回PROTOCOL，准确定位第'+str(id-3)+'项；不影响正式初始化'
 elif id==31: expected='正式Initcall成功；独立回读返回IO；不影响正式初始化'
 elif id in [0,32,33,38]: expected='真实启动配置成功；生产路径无17项验收读；独立回读17项通过；不创建Timer；主循环不重复配置，重复App_Init不重配不重启'
 elif id in [1,2,30,35,36]: expected='生产初始化保留并返回原错误；失败不标记应用初始化成功'
 elif id in [39,40,41]: expected='缺失设备/接口返回NOT_FOUND或NOT_SUPPORTED；不误标成功；接口恢复可重试'
 else: expected='对应SCCB、接口参数或状态断言全部成立'

 observed={hex(reg):u.mem_read(syms['registers']+reg,1)[0] for reg in [0x3808,0x3809,0x380A,0x380B,0x4300,0x501F,0x503D,0x3008,0x3037]}
 rows.append({'name':name,'input':name,'expected':expected,'result':'PASS' if passed else 'FAIL','actual':actual,'registers':observed,'state':state,'error':error,'sccb_tx_prefix':bytes(u.mem_read(syms['g_wire_tx'],min(128,state['tx_count']))).hex(' ')})
 trace_count=struct.unpack('<I',u.mem_read(syms['g_write_trace_count'],4))[0]
 rows[-1]['write_trace_count']=trace_count
 rows[-1]['write_trace']=list(struct.unpack('<'+'I'*trace_count,u.mem_read(syms['g_write_trace'],trace_count*4)))
 rows[-1]['write_trace_sha256']=hashlib.sha256(u.mem_read(syms['g_write_trace'],trace_count*4)).hexdigest()
 rows[-1]['register_map_sha256']=hashlib.sha256(u.mem_read(syms['registers'],65536)).hexdigest()
 print(f"{id+1:02d} {rows[-1]['result']}: {name}: {actual}; line={state['fail_line']}; error={error}")
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows),'assertions':sum(r['state']['checks'] for r in rows)}
(here/'results.json').write_text(json.dumps({'summary':summary,'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# OV5640三种格式模拟验收','','ARMCC5编译当前真实BSP、Device、ST组件、Media、Application及Framework，在Unicorn执行。GPIO模型逐位解码SCCB；DCMI仅在本套中使用桩，另有真实HAL回归。执行器在程序运行前选择开机只读配置，不模拟运行时热切换。17项回读及统计只存在于本测试。','', '| 测试输入 | 预期 | 实际结果 | 判定 |','|---|---|---|---|']
lines += [f"| {r['name']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',str(summary),'','仅为模拟。未连接或测量真实摄像头供电、XCLK及芯片ID。GPIO模型不模拟模拟电气边沿、外部上拉和时钟延展。']
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(0 if not summary['failed'] else 1)
