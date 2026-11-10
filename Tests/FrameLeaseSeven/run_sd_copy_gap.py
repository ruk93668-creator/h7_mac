"""ARMCC5 actual BSP + actual HAL DMA/IRQ/Abort, LL card responses mocked."""
from pathlib import Path
import json, subprocess,sys,struct,ast
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.stdout.reconfigure(encoding='utf-8');sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_IPSR
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/FrameLeaseSeven/sd_copy_gap';OUT.mkdir(parents=True,exist_ok=True)
commands=[];rows=[]
def run(args):
 args=list(map(str,args));p=subprocess.run(args,cwd=ROOT,capture_output=True);s=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(command=subprocess.list2cmdline(args),exit_code=p.returncode,output=s));(HERE/'sd_copy_gap_commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if p.returncode or 'warning:' in s.lower():raise RuntimeError(s)
 return s
def write_cases(path, data):
 # Keep every observation, but place repeated cases on one line instead of duplicating thousands of pretty-print lines.
 if isinstance(data,dict) and isinstance(data.get('cases'),list):
  metadata={k:v for k,v in data.items() if k!='cases'}
  text=json.dumps(metadata,ensure_ascii=False,indent=2)[:-1].rstrip()
  text+=',\n  "cases": [\n'+',\n'.join('    '+json.dumps(c,ensure_ascii=False,separators=(',',':')) for c in data['cases'])+'\n  ]\n}\n'
 else:text=json.dumps(data,ensure_ascii=False,indent=2)+'\n'
 path.write_text(text,encoding='utf-8')

cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');version=run([cc,'--vsn'])
incs=['CM7/Media/Inc','CM7/BSP','CM7/Core/Inc','CM7/Framework','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','CM7/Platform','CM7/Application','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1']
base=[cc,'--cpu','Cortex-M7','--c99','--split_sections','-DCORE_CM7','-DSTM32H747xx','--preinclude',ROOT/'CM7/BSP/bsp_sdmmc_hal_config.h']
for p in incs:base+=['-I',ROOT/p]
names=['单扇区读取','64扇区读取','129扇区分批读取','64扇区写入','129扇区分批写入','零扇区拒绝','LBA越界拒绝','中断丢失仍检查真实DATAEND','CRC与DATAEND同时出现','数据超时','传输拔卡','无终止事件超时','启动命令超时遗留IDMA','IDMA访问错误','Abort失败保留所有权','并发提交BUSY','RX溢出','TX欠载','写后等待卡内部编程','非法等待上下文','MPU缓存属性错误拒绝','开启DCache仍使用非缓存区','Tick回绕','重复与迟到回调','普通卡24MHz回退','4bit协商失败','HighSpeed切换失败','多块CMD12失败后完成回调不覆盖错误','非法参数与DMA地址拒绝','CPU目的地址回绕和越界']
names+=['分批请求统一截止时间','硬件复位未生效保留所有权','LBA加法回绕拒绝','新卡不得继承高速标志','等待适配失败','完成后CMD13失败','Cache开启拒绝重配MPU','MPU关闭拒绝传输','单独回调不能证明硬件完成','连续事务不重复交付']
names+=['运行期时钟被改写拒绝','最后合法扇区','CPU缓冲区无需DMA对齐','DATAEND但字节计数未清零']
legacy=ast.parse((ROOT/'Tests/SdmmcStage08A3/run_tests.py').read_text(encoding='utf-8'))
legacy_names=next(ast.literal_eval(n.value) for n in legacy.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='names' for t in n.targets))
adapter_names=['未准备时拒绝','静态资源创建失败','Scheduler未启动拒绝','PRIMASK拒绝阻塞','BASEPRI拒绝阻塞','二值唤醒与零超时','重复准备不重复识卡','适配不能被替换','只读单块多块CRC一致','IRQ唤醒任务','错误CID禁止写','错误容量禁止写','没有备份禁止恢复','写入回读并恢复原数据','恢复身份错误保留备份','授权区域越界禁止写']
write_defines=['-DAPP_SD_BLOCK_TEST_ENABLED=1','-DAPP_SD_BLOCK_WRITE_TEST_ENABLED=1','-DSD_TEST_CID0=1U','-DSD_TEST_CID1=2U','-DSD_TEST_CID2=3U','-DSD_TEST_CID3=4U','-DSD_TEST_CAPACITY_BYTES=34359738368ULL','-DSD_TEST_WRITE_FIRST_LBA=100U','-DSD_TEST_WRITE_BLOCKS=8U']
profiles=[('idma','Sdmmc08B_TestRun',names,[]),('adapter','SdBlockAdapter_TestRun',adapter_names,write_defines+['-DSD_ADAPTER_TIMER_STUB=1'])]
stress_names=['65扇区64加1分批及轮数上限','到时停止且不补发','在途手动停止等待读完','第二轮CRC不一致立即退出','轮间手动停止','首次驱动错误不恢复重试','待恢复原数据拒绝压力','识卡失败退出','Tick回绕','本地计时停滞仍有轮数上限','缓冲区哨兵损坏','读取超时退出','Watch不能扩大边界','重复在途启动拒绝','卡容量不足拒绝','忙时命令不在退出后自动执行','重复运行重新建基准']
profiles += [('stress','SdBlockStress_TestRun',stress_names,['-DAPP_SD_BLOCK_TEST_ENABLED=1','-DAPP_SD_BLOCK_STRESS_TEST_ENABLED=1','-DSD_BLOCK_STRESS_DURATION_MS=1000U','-DSD_BLOCK_STRESS_MAX_ROUNDS=3U']),('stress_off','SdBlockStress_TestRun',['关闭压力拒绝请求'],['-DAPP_SD_BLOCK_TEST_ENABLED=1'])]

for mode,enabled,detect,low in [('off',0,1,0),('detect',1,1,0),('probe',1,0,0),('probe_low',1,0,1)]:
 profiles.append((mode,'Sdmmc_TestRun',legacy_names,['-DAPP_SDMMC_TEST_ENABLED='+str(enabled),'-DAPP_SDMMC_DETECT_ONLY='+str(detect),'-DBSP_SDMMC_DETECT_ACTIVE_LOW='+str(low)]))
profiles=[("gap"+str(gap),"SdCopyGap_TestRun",['copy_4k','copy_512','tail_boundary','invalid_size','internal_ram_unchanged','stalled_tick_bounded','wait_error','stale_signal','tick_wrap','total_deadline','idma_stays_64_sectors','prepare_input','prepare_stale_signal','prepare_bounds','prepare_busy','prepare_stalled_tick','prepare_wait_error'],['-DSDMMC_COPY_GAP_MS='+str(gap)+'U']) for gap in [1,0]]
for profile,entry_name,case_names,defines in profiles:
 for opt in ['O0','O2']:
  obj=OUT/(profile+'_'+opt+'.o');hal=OUT/('hal_'+opt+'.o');axf=OUT/(profile+'_'+opt+'.axf')
  run(base+defines+['-'+opt,'-c',HERE/('stress_test.c' if profile.startswith('stress') else 'adapter_test.c' if profile=='adapter' else 'sd_copy_gap_test.c'),'-o',obj])
  renames=['HAL_SD_Init','HAL_SD_DeInit','HAL_SD_GetCardInfo','HAL_SD_GetCardState','HAL_SD_GetState','HAL_SD_GetError','HAL_SD_ConfigWideBusOperation','HAL_SD_ConfigSpeedBusOperation']
  run(base+['-'+opt]+['-D'+s+'=Vendor_'+s for s in renames]+['-c',ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c','-o',hal])
  run([cc.with_name('armlink.exe'),obj,hal,'--entry',entry_name,'--scatter',ROOT/'Tests/SdmmcStage08B/test.sct','-o',axf])
  with axf.open('rb') as f:
   e=ELFFile(f);entry=e.header['e_entry'];sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  for case,name in enumerate(case_names):
   u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
   for a,n in [(0xD0000000,0x800000),(0x08000000,0x100000),(0x20000000,0x40000),(0x24000000,0x80000),(0x01000000,0x1000),(0x58020000,0x10000),(0x52007000,0x1000),(0x40001000,0x1000),(0x40020000,0x1000),(0x48020000,0x1000),(0xe000e000,0x2000)]:u.mem_map(a,n)
   for a,b in sections:u.mem_write(a,b)
   # The old fixture advances time on every HAL_GetTick call. For copy-gap cases,
   # model time independently: only the wait callback advances the tick.
   def manual_tick(uc,address,size,data):
    if address==(symbols['HAL_GetTick']&~1) and int.from_bytes(uc.mem_read(symbols['copy_manual_clock'],4),'little'):
     uc.reg_write(UC_ARM_REG_R0,int.from_bytes(uc.mem_read(symbols['s_tick'],4),'little'))
     uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
   u.hook_add(UC_HOOK_CODE,manual_tick,begin=symbols['HAL_GetTick']&~1,end=symbols['HAL_GetTick']&~1)
   def w1c(uc,access,address,size,value,data):
    if address==0x52007038:
     flags=int.from_bytes(uc.mem_read(0x52007034,4),'little');uc.mem_write(0x52007034,(flags&~value).to_bytes(4,'little'))
    if address==0x5802447c and value&(1<<16):
     stuck=profile=='idma' and case==31 and uc.mem_read(symbols['s_abort_stuck'],1)[0]!=0
     if not stuck:uc.mem_write(0x52007000,bytes(0x90))
   u.hook_add(UC_HOOK_MEM_WRITE,w1c)
   u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
   if profile not in ("idma","adapter","stress","stress_off") and case==30:u.reg_write(UC_ARM_REG_IPSR,16)
   err=None
   try:u.emu_start(entry|1,0x01000000,count=30000000)
   except Exception as exc:err=str(exc)
   checks,line=struct.unpack('<2I',u.mem_read(symbols['g_test_result'],8));passed=not err and not line and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
   row=dict(profile=profile,case=case,name=name,optimization=opt,result='PASS' if passed else 'FAIL',checks=checks,failure_line=line,error=err);rows.append(row)
   if not passed:print(row,flush=True)
  print(profile,opt,sum(x['result']=='PASS' for x in rows if x['optimization']==opt and x['profile']==profile),'/',len(case_names),flush=True)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows))
write_cases(HERE/'sd_copy_gap_results.json',dict(compiler=version,summary=summary,cases=rows,hardware_verified=False));print(summary)
raise SystemExit(bool(summary['failed']))
