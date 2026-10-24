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
for inc in ['CM7/Core/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/STM32H7xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Drivers/CMSIS/Include','CM7/BSP','CM7/Platform','CM7/Framework','Common/Inc','Board/Inc']:args+=['-I',root/inc]
run(args+['-c',here/'dcmi_test.c','-o',here/'dcmi_test.o'])
objects=[here/'dcmi_test.o']
for source in ['stm32h7xx_hal_dcmi','stm32h7xx_hal_dma','stm32h7xx_hal_dma_ex','stm32h7xx_hal_gpio']:
 dest=here/(source+'.o');objects.append(dest)
 run(args+['-c',root/('Drivers/STM32H7xx_HAL_Driver/Src/'+source+'.c'),'-o',dest])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','Dcmi_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','-o',here/'dcmi_test.axf'])
with (here/'dcmi_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['未初始化拒绝调用', '初始化不自动采集', '参数边界和对齐', 'ISR拒绝启动/停止', '关中断拒绝启动/停止', '空闲Stop', '清理旧事件和硬件挂起', 'Active重复Start拒绝', 'DMA完成尚无Frame', '正常Snapshot完成', '连续两帧和ERR/OVR重启', '同步错先通知再Stop', '溢出先通知再Stop', '错误和Frame同时到达', '真实DMA TE错误', '纯FIFO错误回调', '过滤其他句柄回调', '早到Frame拒绝', 'Start HAL_ERROR', 'Start HAL_BUSY', 'Start HAL_TIMEOUT', 'Abort无回调仍能Stop恢复', 'HAL Stop虚假成功仍核对DMA', 'HAL Stop超时但恢复硬件', 'HAL Stop错误但恢复硬件', 'DMA无法停下时超时并阻止Start', '恢复DMA DeInit失败', '恢复DMA Init失败', '恢复DCMI DeInit失败', '恢复DCMI Init失败', 'ABORT+Lock遗留恢复', 'Stop后迟到回调丢弃', 'NULL Take不消费', '读清期间完成IRQ延后交付', '读清期间新错误不丢失', 'PRIMASK=1保持及中断延后', '未处理硬件错误覆盖FRAME', 'Stop保留已完成事件', '控制接口重入BUSY', '真实异步Abort完成IRQ', '遗留DCMI Lock停止恢复', 'Stop等待Tick回绕', '新Start清除旧完成', '读出错误后Frame仍不变成功', '非法Start不清事件', '重复Stop不产生完成事件']
names += ['Snapshot: Normal Stop', 'Snapshot: DMA stuck', 'Snapshot: DMA DeInit error', 'Snapshot: DMA Init error', 'Snapshot: DCMI DeInit error', 'Snapshot: DCMI Init error', 'Snapshot: HAL Stop timeout', 'Snapshot: HAL Stop IO error', 'Continuous: Normal Stop', 'Continuous: DMA stuck', 'Continuous: DMA DeInit error', 'Continuous: DMA Init error', 'Continuous: DCMI DeInit error', 'Continuous: DCMI Init error', 'Continuous: HAL Stop timeout', 'Continuous: HAL Stop IO error']
fields=['fail_line','checks','dma_init','dcmi_init','dma_deinit','nvic_enabled','nvic_cleared','dma_callback','dcmi_callback','dma_cr','dcmi_cr','request','ready','result','event_flags','dcmi_error','dma_error','stop_required','primask','active','start_calls','stop_calls']
rows=[]
for id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x40020000,0x10000),(0x48020000,0x1000),(0x58020000,0x10000),(0x5C001000,0x1000),(0xE0000000,0x10000),(0xD0000000,0x10000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 access={'sdram':0,'started':False,'model_error':None}
 def word(address):return struct.unpack('<I',u.mem_read(address,4))[0]
 def put(address,value):u.mem_write(address,struct.pack('<I',value&0xFFFFFFFF))
 def memory(uc,kind,address,size,value,data):
  if 0xD0000000<=address<0xD0010000:access['sdram']+=1
  if kind==16 and address==0x48020010:put(address,word(0x48020008)&word(0x4802000C))
  if kind==16 and address==0x40020010 and word(symbols['g_stuck_stream']):put(address,word(address)|1)
  if kind==17:
   if address==0x40020008:put(0x40020000,word(0x40020000)&~value)
   if address==0x48020014:
    put(0x48020008,word(0x48020008)&~value);put(0x48020010,word(0x48020010)&~value)
   if address==0x40020010 and value&1:access['started']=True

 u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=50000000,timeout=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<13Ii8I',u.mem_read(symbols['g_result'],88))))
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line'] and not access['sdram']
 rows.append(dict(name=name,result='PASS' if passed else 'FAIL',state=state,access=access,error=error))
 print(id,rows[-1]['result'],name,state,access,error)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows),assertions=sum(x['state']['checks'] for x in rows))
(here/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows),ensure_ascii=False,indent=2),encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(bool(summary['failed']))
