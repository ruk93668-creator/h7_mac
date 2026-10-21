import json,struct,subprocess,sys,re
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parent.parent
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_C1_C0_2
from elftools.elf.elffile import ELFFile
commands=[]
def run(cmd):
 p=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True)
 output=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append(dict(command=subprocess.list2cmdline([str(x) for x in cmd]),code=p.returncode,output=output))
 (here/'production_commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(commands[-1]['command']);print(output,end='')
 if p.returncode:raise RuntimeError('build failed')
# Always compile the current HAL query implementations verbatim.
hal_source=(root/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c').read_text(encoding='utf-8')
def hal_function(name):
 match=re.search(r'^(?:void|uint32_t) '+name+r'\([^\n]*\)\s*\{',hal_source,re.M)
 assert match
 start=match.start();a=hal_source.index('{',start);depth=1;b=a+1
 while depth:
  if hal_source[b]=='{':depth+=1
  if hal_source[b]=='}':depth-=1
  b+=1
 return hal_source[start:b]
(here/'hal_clock_queries.inc').write_text(hal_source[:hal_source.index('/* Includes')]+ '\n\n'.join(hal_function(name) for name in ['HAL_RCCEx_GetPeriphCLKFreq','HAL_RCCEx_GetPLL2ClockFreq']),encoding='utf-8')

run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',root/'CM7/BSP/bsp_sdram_hal_config.h']
for inc in ['CM7/Core/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/STM32H7xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Drivers/CMSIS/Include','CM7/BSP','Tests/SdramV0/diagnostics','CM7/Device/Inc','CM7/Application','CM7/Framework','CM7/Platform','CM7/Service','Common/Inc','Board/Inc']:args+=['-I',root/inc]
run(args+['-c',here/'production_test.c','-o',here/'production_test.o'])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'production_test.o','--entry','Sdram_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','-o',here/'production_test.axf'])
with (here/'production_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['正式初始化不读写SDRAM、MPU/时钟/引脚/时序正确', 'PLL2占用', 'PLL源错误', 'HSI分频错误', 'HSI未就绪', 'PLL配置IO', 'PLL配置超时', 'PLL2R频率不符', 'HAL SDRAM初始化失败', 'HAL SDRAM初始化超时', 'MSP缺失', 'CLK_ENABLE失败', 'PALL失败', 'AUTOREFRESH失败', 'LOAD_MODE失败', '命令超时', '刷新设置失败', '刷新设置超时', '重复Framework初始化无测试无重复配置', 'FMC选择器不符']
fields=['fail_line','checks','clock_calls','gpio_calls','hal_init_calls','command_calls','refresh_calls','delay_ms','check_state','words','failed_address','expected','actual','sdclk','init_result','check_result']
rows=[]
for id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x58020000,0x10000),(0xD0000000,0x10000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 original=b''.join(struct.pack('<I',0x13579BDF^i) for i in range(256));u.mem_write(0xD0000000,original)
 accesses=dict(reads=0,writes=0,min=0xFFFFFFFF,max=0,injected=False,outside=False)
 def memory(uc,kind,address,size,value,data):
  if address<0xD0000000 or address+size>0xD0000400:accesses['outside']=True;uc.emu_stop();return
  accesses['min']=min(accesses['min'],address);accesses['max']=max(accesses['max'],address+size-1)
  if kind==16: # UC_MEM_READ
   accesses['reads']+=1
   count=accesses['reads']
   if not accesses['injected'] and ((id==20 and count==257) or (id==21 and count==1793)):
    uc.mem_write(address,struct.pack('<I',0xBAD0BAD0));accesses['injected']=True
  else:accesses['writes']+=1
 u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory,begin=0xD0000000,end=0xD000FFFF)
 u.reg_write(UC_ARM_REG_C1_C0_2,0x00F00000)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=20000000,timeout=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<14I2i',u.mem_read(symbols['g_result'],64))))
 restored=bytes(u.mem_read(0xD0000000,1024))==original
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line'] and not accesses['outside']
 passed=passed and accesses['reads']==0 and accesses['writes']==0 and restored
 if id==21:passed=passed and accesses['injected']
 rows.append(dict(name=name,input=name,result='PASS' if passed else 'FAIL',state=state,accesses=accesses,restored=restored,error=error))
 print(id,rows[-1]['result'],name,state,'restored=',restored,'error=',error)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows),assertions=sum(x['state']['checks'] for x in rows))
(here/'production_results.json').write_text(json.dumps(dict(summary=summary,cases=rows),ensure_ascii=False,indent=2),encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(bool(summary['failed']))
