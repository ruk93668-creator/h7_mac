import argparse,json,struct,sys
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('image',type=Path)
parser.add_argument('--deps',type=Path,required=True)
parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args();sys.stdout.reconfigure(encoding='utf-8');sys.path.insert(0,str(args.deps))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
names=['成功配置、硬件顺序及重复Init','错误内核频率拒绝初始化']
for phase in ['RCC','HAL_I2C_Init','模拟滤波','数字滤波']:
 for status in ['ERROR→IO','BUSY→BUSY','TIMEOUT→TIMEOUT']:names.append(phase+' '+status+'及失败后重试')
names+=['初始化重入返回BUSY','MSP忽略NULL和非I2C2','成功后频率变化拒绝复用Timing','MSP DeInit释放管脚和I2C2时钟','BUS注册项','BUS成功后才执行DEVICE','BUS失败中止DEVICE']
fields=['failure_line','result','nested_result','rcc_calls','init_calls','analog_calls','digital_calls','deinit_calls','gpio_calls','gpio_deinit_calls','device_stage_calls','trace']
with args.image.open('rb') as f:
 elf=ELFFile(f);entry=elf.header['e_entry']
 symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
rows=[]
for case,name in enumerate(names):
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 uc.mem_map(0x08000000,0x100000);uc.mem_map(0x20000000,0x40000);uc.mem_map(0x01000000,0x1000)
 for address,data in sections:uc.mem_write(address,data)
 uc.reg_write(UC_ARM_REG_SP,0x2003FFF0);uc.reg_write(UC_ARM_REG_LR,0x01000001);uc.reg_write(UC_ARM_REG_R0,case)
 error=None
 try:uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<Iii9I',uc.mem_read(symbols['g_bsp_result'],48))))
 passed=error is None and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and state['failure_line']==0
 row={'case':case,'name':name,'result':'Pass' if passed else 'Fail','state':state,'emulator_error':error};rows.append(row)
 print(f"{case+1:02d} {row['result']}: {name}; result={state['result']}; fail_line={state['failure_line']}")
args.report.mkdir(parents=True,exist_ok=True)
(args.report/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
lines=['# BSP IIC 隔离测试','', '使用实际BSP/Framework代码和可控HAL测试桩；验证调用逻辑，不模拟I2C电气行为。','', '| 用例 | 结果 | 返回值 | 失败行 |','|---|---|---|---|']
lines += [f"| {r['name']} | {r['result']} | {r['state']['result']} | {r['state']['failure_line']} |" for r in rows]
lines += ['',f"{sum(r['result']=='Pass' for r in rows)}/{len(rows)} Pass。",'成功路径检查RCC选择→GPIOB时钟→AF4开漏→I2C2时钟→HAL初始化→模拟滤波→数字滤波。']
(args.report/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
raise SystemExit(0 if all(r['result']=='Pass' for r in rows) else 1)
