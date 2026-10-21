import json,struct,subprocess,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parent.parent.parent
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
names=['真实HAL初始化/AF13/DMA/DCMI/NVIC','重复EarlyInit和Init幂等','直接Init补齐引脚','DMA Stream运行中拒绝','DCMI已使能拒绝','DCMI Capture运行中拒绝','DMA HAL_ERROR','DMA HAL_BUSY','DMA HAL_TIMEOUT','DCMI HAL_ERROR及DMA清理','DCMI HAL_BUSY','DCMI HAL_TIMEOUT','失败后DMA清理失败','DMA失败后重试','DCMI失败后重试','真实DMA/DCMI IRQ路由','非DCMI MSP不操作硬件','BUS Initcall/无采集无Framebuffer','旧NVIC挂起先清理','不改变SDRAM/SCCB/I2C引脚和PLL']
fields=['fail_line','checks','dma_init','dcmi_init','dma_deinit','nvic_enabled','nvic_cleared','dma_callback','dcmi_callback','dma_cr','dcmi_cr','request','ready','result']
rows=[]
for id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x40020000,0x10000),(0x48020000,0x1000),(0x58020000,0x10000),(0x5C001000,0x1000),(0xE0000000,0x10000),(0xD0000000,0x10000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 access={'sdram':0,'started':False}
 def memory(uc,kind,address,size,value,data):
  if 0xD0000000<=address<0xD0010000:access['sdram']+=1
  if kind==17 and id not in [3,4,5]:
   if address==0x40020010 and value&1:access['started']=True
   if address==0x48020000 and value&0x4001:access['started']=True
 u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=20000000,timeout=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<13Ii',u.mem_read(symbols['g_result'],56))))
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line'] and not access['sdram'] and not access['started']
 rows.append(dict(name=name,result='PASS' if passed else 'FAIL',state=state,access=access,error=error))
 print(id,rows[-1]['result'],name,state,access,error)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows),assertions=sum(x['state']['checks'] for x in rows))
(here/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows),ensure_ascii=False,indent=2),encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(bool(summary['failed']))
