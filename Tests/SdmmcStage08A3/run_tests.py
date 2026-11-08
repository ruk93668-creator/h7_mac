"""ARMCC5 compile of actual BSP, CMSIS MMIO emulated by Unicorn; only HAL calls mocked."""
from pathlib import Path
import json, subprocess, sys, struct
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/SdmmcStage08A3';OUT.mkdir(parents=True,exist_ok=True)
sys.stdout.reconfigure(encoding='utf-8');sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_IPSR
from elftools.elf.elffile import ELFFile
names=['成功识卡及32GiB容量','无卡不调用HAL','PLL1Q不符','RCC失败','RCC超时','内核频率无效','MUX回读失败','HAL初始化CRC错误','HAL_ERROR包装命令超时','HAL_TIMEOUT','HAL_BUSY','卡信息读取失败','卡信息超时','零块数','非法逻辑块大小','未知卡类型','非TRANSFER状态','状态超时','状态CRC错误','实际分频为零','实际4bit','意外IDMA使能','意外中断使能','清理失败与重试','初始化内部重入','重复初始化','失败后重新识卡','DeInit使信息失效','空参数','初始化前读取','ISR拒绝','PRIMASK拒绝','BASEPRI屏蔽Tick拒绝','BASEPRI允许TIM6','TIM6停止','TIM6中断停用','NVIC未使能','外部HAL句柄忽略','拔卡拒绝旧信息','PH14原始输入','关闭探针无副作用','检测模式','完整探针成功','探针HAL失败不阻断','探针无卡不阻断','探针只执行一次','SDMMC2异源保护','SDMMC2同源允许','Tick回绕','PLL1未运行拒绝']
commands=[]
def run(args):
 args=list(map(str,args));r=subprocess.run(args,cwd=ROOT,capture_output=True);text=(r.stdout+r.stderr).decode(errors='replace')
 commands.append(dict(command=subprocess.list2cmdline(args),exit_code=r.returncode,output=text));(HERE/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if r.returncode or 'warning:' in text.lower():raise RuntimeError(text)
 return text
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');version=run([cc,'--vsn']);rows=[]
for mode in ['off','detect','probe','probe_high']:
 for opt in ['O0','O2']:
  obj=OUT/(mode+'_'+opt+'.o');axf=obj.with_suffix('.axf')
  flags=[cc,'--cpu','Cortex-M7','--c99','-'+opt,'-DCORE_CM7','-DSTM32H747xx','--preinclude',ROOT/'CM7/BSP/bsp_sdmmc_hal_config.h','-DAPP_SDMMC_TEST_ENABLED='+('0' if mode=='off' else '1'),'-DAPP_SDMMC_DETECT_ONLY='+('0' if mode.startswith('probe') else '1')]
  flags+=['-DBSP_SDMMC_DETECT_ACTIVE_LOW='+('0' if mode=='probe_high' else '1')]
  for inc in ['CM7/BSP','CM7/Core/Inc','CM7/Framework','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include']:flags+=['-I',ROOT/inc]
  run(flags+['-c',HERE/'sdmmc_test.c','-o',obj])
  run([cc.with_name('armlink.exe'),obj,'--entry','Sdmmc_TestRun','--scatter',HERE/'test.sct','--keep=*(.framework_init)','--no_scanlib','-o',axf])
  with axf.open('rb') as f:
   elf=ELFFile(f);entry=elf.header['e_entry'];sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']];symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
  for case,name in enumerate(names):
   u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
   for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x58020000,0x10000),(0x52007000,0x1000),(0x40001000,0x1000),(0xe000e000,0x2000)]:u.mem_map(addr,size)
   for addr,data in sections:u.mem_write(addr,data)
   u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
   if case==30:u.reg_write(UC_ARM_REG_IPSR,16)
   error=None
   try:u.emu_start(entry|1,0x01000000,count=3000000)
   except Exception as exc:error=str(exc)
   checks,line=struct.unpack('<2I',u.mem_read(symbols['g_test_result'],8));passed=not error and not line and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
   row=dict(mode=mode,optimization=opt,case=case,name=name,result='PASS' if passed else 'FAIL',failure_line=line,checks=checks,error=error);rows.append(row)
   if not passed:print(row,flush=True)
  print(mode,opt,sum(r['result']=='PASS' for r in rows if r['mode']==mode and r['optimization']==opt),'/',len(names),flush=True)
summary=dict(total=len(rows),passed=sum(r['result']=='PASS' for r in rows),failed=sum(r['result']=='FAIL' for r in rows))
(HERE/'results.json').write_text(json.dumps(dict(compiler=version,summary=summary,cases=rows,hardware_verified=False),ensure_ascii=False,indent=2),encoding='utf-8');print(summary)
raise SystemExit(bool(summary['failed']))
