import json,struct,subprocess,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parent.parent
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(cmd):
 p=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True)
 output=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append(dict(command=subprocess.list2cmdline([str(x) for x in cmd]),code=p.returncode,output=output))
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(commands[-1]['command']);print(output,end='')
 if p.returncode:raise RuntimeError('build failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',root/'CM7/BSP/bsp_dcmi_hal_config.h']
for inc in ['CM7/Media/Inc','CM7/Core/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/STM32H7xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Drivers/CMSIS/Include','CM7/BSP','CM7/Platform','CM7/Framework','Common/Inc','Board/Inc']:args+=['-I',root/inc]
run(args+['-c',here/'joint_test.c','-o',here/'dcmi_test.o'])
objects=[here/'dcmi_test.o']
for source in ['stm32h7xx_hal_dcmi','stm32h7xx_hal_dma','stm32h7xx_hal_dma_ex','stm32h7xx_hal_gpio']:
 dest=here/(source+'.o');objects.append(dest)
 run(args+['-c',root/('Drivers/STM32H7xx_HAL_Driver/Src/'+source+'.c'),'-o',dest])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','Dcmi_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','-o',here/'dcmi_test.axf'])
with (here/'dcmi_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['注册及原子启动 / Y8', '注册及原子启动 / RGB565', '注册及原子启动 / YUV422', 'M0 TC→FRAME / Y8', 'M0 TC→FRAME / RGB565', 'M0 TC→FRAME / YUV422', 'M0 FRAME→TC / Y8', 'M0 FRAME→TC / RGB565', 'M0 FRAME→TC / YUV422', 'M1 TC→FRAME / Y8', 'M1 TC→FRAME / RGB565', 'M1 TC→FRAME / YUV422', 'M1 FRAME→TC / Y8', 'M1 FRAME→TC / RGB565', 'M1 FRAME→TC / YUV422', '多帧Get/Release / Y8', '多帧Get/Release / RGB565', '多帧Get/Release / YUV422', 'READY回收30帧 / Y8', 'READY回收30帧 / RGB565', 'READY回收30帧 / YUV422', 'IN_USE持有100帧 / Y8', 'IN_USE持有100帧 / RGB565', 'IN_USE持有100帧 / YUV422', '正常Stop等待IN_USE / Y8', '正常Stop等待IN_USE / RGB565', '正常Stop等待IN_USE / YUV422', '替换HAL失败 / Y8', '替换HAL失败 / RGB565', '替换HAL失败 / YUV422', '替换期间CT变化 / Y8', '替换期间CT变化 / RGB565', '替换期间CT变化 / YUV422', '故障Stop及DRAINING / Y8', '故障Stop及DRAINING / RGB565', '故障Stop及DRAINING / YUV422', 'Stop失败重试与DRAINING / Y8', 'Stop失败重试与DRAINING / RGB565', 'Stop失败重试与DRAINING / YUV422', 'Stop保留ERROR且重启不误消费 / Y8', 'Stop保留ERROR且重启不误消费 / RGB565', 'Stop保留ERROR且重启不误消费 / YUV422', '非法Start保留旧事件 / Y8', '非法Start保留旧事件 / RGB565', '非法Start保留旧事件 / YUV422', 'BUSY Start保留旧事件 / Y8', 'BUSY Start保留旧事件 / RGB565', 'BUSY Start保留旧事件 / YUV422', '新Start清事件HAL错误与挂起 / Y8', '新Start清事件HAL错误与挂起 / RGB565', '新Start清事件HAL错误与挂起 / YUV422', '启动首帧IRQ延后到RUNNING / Y8', '启动首帧IRQ延后到RUNNING / RGB565', '启动首帧IRQ延后到RUNNING / YUV422', 'DBM启动失败交付新错误 / Y8', 'DBM启动失败交付新错误 / RGB565', 'DBM启动失败交付新错误 / YUV422', 'Snapshot释放后Stream启动 / Y8', 'Snapshot释放后Stream启动 / RGB565', 'Snapshot释放后Stream启动 / YUV422', 'Snapshot超时恢复 / Y8', 'Snapshot超时恢复 / RGB565', 'Snapshot超时恢复 / YUV422', '空闲Stop保留旧错误 / Y8', '空闲Stop保留旧错误 / RGB565', '空闲Stop保留旧错误 / YUV422', '多轮停止重启 / Y8', '多轮停止重启 / RGB565', '多轮停止重启 / YUV422', 'PRIMASK延后真实IRQ / Y8', 'PRIMASK延后真实IRQ / RGB565', 'PRIMASK延后真实IRQ / YUV422', 'NDTR门限错过 / Y8', 'NDTR门限错过 / RGB565', 'NDTR门限错过 / YUV422', '重复TC / Y8', '重复TC / RGB565', '重复TC / YUV422', '重复FRAME / Y8', '重复FRAME / RGB565', '重复FRAME / YUV422', '可见跨目标延迟拒绝 / Y8', '可见跨目标延迟拒绝 / RGB565', '可见跨目标延迟拒绝 / YUV422', '风险反例FRAME先服务跨帧合并 / Y8', '风险反例FRAME先服务跨帧合并 / RGB565', '风险反例FRAME先服务跨帧合并 / YUV422', '风险反例TC先服务跨帧合并 / Y8', '风险反例TC先服务跨帧合并 / RGB565', '风险反例TC先服务跨帧合并 / YUV422', 'DRAINING接口门控 / Y8', 'DRAINING接口门控 / RGB565', 'DRAINING接口门控 / YUV422', 'Media候选旧目标重叠拒绝 / Y8', 'Media候选旧目标重叠拒绝 / RGB565', 'Media候选旧目标重叠拒绝 / YUV422']
names += [f'{n} / {fmt}' for n in ['风险反例FREE下跨帧FRAME先服务','风险反例FREE下跨帧TC先服务'] for fmt in ['Y8','RGB565','YUV422']]
names += [f'{n} / {fmt}' for n in ['IN_USE下TC挂起且CT绕回仍拒绝','FREE下TC挂起且CT绕回仍拒绝'] for fmt in ['Y8','RGB565','YUV422']]
fields=['fail_line','checks','dma_init','dcmi_init','dma_deinit','nvic_enabled','nvic_cleared','dma_callback','dcmi_callback','dma_cr','dcmi_cr','request','ready','result','event_flags','dcmi_error','dma_error','stop_required','primask','active','start_calls','stop_calls']
rows=[]
for id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x40020000,0x10000),(0x48020000,0x1000),(0x58020000,0x10000),(0x5C001000,0x1000),(0xE0000000,0x10000),(0xD0000000,0x20000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 access={'sdram':0,'started':False,'model_error':None}
 def word(address):return struct.unpack('<I',u.mem_read(address,4))[0]
 def put(address,value):u.mem_write(address,struct.pack('<I',value&0xFFFFFFFF))
 def memory(uc,kind,address,size,value,data):
  if 0xD0000000<=address<0xD0020000:access['sdram']+=1
  if kind==16 and address==0x48020010:put(address,word(0x48020008)&word(0x4802000C))
  if kind==16 and address==0x40020010 and word(symbols['g_stuck_stream']):put(address,word(address)|1)
  if kind==17:
   if address==0x40020008:put(0x40020000,word(0x40020000)&~value)
   if address==0x48020014:
    put(0x48020008,word(0x48020008)&~value);put(0x48020010,word(0x48020010)&~value)
   if address==0x40020010 and value&1:access['started']=True
   if address==0x48020000 and value&1 and word(symbols['g_inject_start']):
    put(symbols['g_queued_start'],1);put(symbols['g_inject_start'],0)

 u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=150000000,timeout=15000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<13Ii8I',u.mem_read(symbols['g_result'],88))))
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line']
 rows.append(dict(name=name,result='PASS' if passed else 'FAIL',state=state,access=access,error=error,risk_reproduction=id//3 in [28,29,32,33],reference_epochs={key:word(symbols[key]) for key in ['g_ref_tc','g_ref_frame','g_ref_delivered']}))
 rows[-1]['registers']={key:word(addr) for key,addr in [('CR',0x40020010),('NDTR',0x40020014),('M0AR',0x4002001C),('M1AR',0x40020020),('DMA_FLAGS',0x40020000),('DCMI_CR',0x48020000),('DCMI_FLAGS',0x48020008)]}
 rows[-1]['capture']=dict(zip(['mode','state','last_result_raw','m0_index','m1_index','consumer','sequence','drops','bytes','buffer0_state','buffer1_state','buffer2_state','paired','tc_pending','frame_pending','bsp_state','stop_required','reference_dma_count','reference_frame','reference_tc'],[word(symbols['g_joint_view']+i*4) for i in range(20)]))
 if rows[-1]['risk_reproduction']:rows[-1]['interpretation']='风险复现成功，不能解释为配对安全通过'
 print(id,rows[-1]['result'],name,state,access,error)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows),assertions=sum(x['state']['checks'] for x in rows))
summary.update(functional_total=sum(not r['risk_reproduction'] for r in rows),functional_passed=sum(r['result']=='PASS' and not r['risk_reproduction'] for r in rows),risk_reproduced=sum(r['result']=='PASS' and r['risk_reproduction'] for r in rows))
(here/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows),ensure_ascii=False,indent=2),encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(bool(summary['failed']))
