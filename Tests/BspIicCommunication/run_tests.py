"""ARMCC5 build + execution of the real BSP with deterministic Mock HAL."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--deps', type=Path)
args = parser.parse_args()
sys.stdout.reconfigure(encoding='utf-8')
here = Path(__file__).resolve().parent
root = here.parent.parent
if args.deps is None:
    marker = Path(tempfile.gettempdir()) / 'h7_timer_latest_audit.txt'
    if marker.exists():
        args.deps = Path(marker.read_text().strip()) / 'python_deps'
if args.deps:
    sys.path.insert(0, str(args.deps))
import unicorn
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

cases = [
 ('T01','异步正常启动','地址0x3C，长度2','SYS_OK；TRANSFERRING；HAL地址0x78'),
 ('T02','正常完成','启动后投递Tx完成IRQ，再TakeResult','true、SYS_OK，进入IDLE'),
 ('T03','结果只交付一次','正常完成后读取两次','第一次true、0，第二次false且输出不变'),
 ('T04','发送期间重入发送','异步未完成，分别同步和异步发送','均BUSY=-5，无新HAL发送'),
 ('T05','99ms未超时','启动后Tick增加99，再TakeResult','false，Abort次数0，保持TRANSFERRING'),
 ('T06','100ms请求Abort','启动后Tick增加100，再TakeResult','调用一次Abort，地址0x78，保持ABORTING'),
 ('T07','Abort成功及完成','Abort内产生完成IRQ，PRIMASK屏蔽时挂起','恢复中断后完成，下次TakeResult为true、TIMEOUT=-8；只交付一次'),
 ('T08','Abort启动失败','100ms后Abort返回HAL_ERROR','立即交付true、IO=-9，进入FAULT'),
 ('T09','FAULT拒绝发送','Abort失败进入FAULT，再同步和异步发送','均IO=-9，不复位为IDLE'),
 ('T10','普通HAL错误','投递AF错误IRQ','true、IO=-9，进入IDLE'),
 ('T11','HAL超时错误','投递TIMEOUT|AF错误IRQ','true、TIMEOUT=-8'),
 ('T12','连续不同地址','0x08、0x3C、0x77逐个异步完成并同步发送','HAL逐次收到0x10、0x78、0xEE'),
 ('T13','Abort使用当前地址','先完成0x3C，再启动0x55并超时','Abort收到0xAA，而非前一事务地址'),
 ('T14','129字节边界','异步129字节，启动后改写调用方缓冲区','接受，HAL副本及BSP内部副本保持全部原始129字节'),
 ('T15','130字节越界','异步130字节','INVALID_ARG=-1，无HAL调用'),
 ('T16','非法参数矩阵','NULL、零长度、所有非法uint8地址，同步及异步','均INVALID_ARG=-1，无HAL调用'),
 ('T17','完成与超时交错','100ms时在读取PRIMASK后、屏蔽中断前完成','不Abort、不覆盖COMPLETED，下次交付SYS_OK'),
 ('T18','恢复初始PRIMASK','分别以PRIMASK=1和0进入Abort，并产生完成IRQ','准确恢复原值；mask=1时回调仍挂起，手动解屏蔽后交付超时'),
 ('T19','HAL_BUSY启动失败可重试','启动返回HAL_BUSY，之后HAL_OK重试','先BUSY=-5并回IDLE，之后启动并完成成功'),
 ('T20','Abort成功但无回调','Abort返回HAL_OK，不投递回调，再轮询8秒','保持ABORTING；不重复Abort、不交付结果；后续发送BUSY'),
 ('E01','同步返回值映射','HAL_OK/ERROR/BUSY/TIMEOUT同步发送','分别0/-9/-5/-8，100ms参数，BSP仍IDLE'),
 ('E02','HAL状态准入','RESET、BUSY_TX状态分别请求同步/异步','同步RESET=-4；其他BUSY=-5；无HAL发送'),
 ('E03','NVIC反初始化','mask=1挂起完成IRQ，然后MSP DeInit，再解屏蔽','EV/ER均禁用，Pending清除，不投递回调'),
 ('E04','Tick 32位回绕','起点0xFFFFFFF0，+99、+100','99ms不Abort；100ms Abort；回调交付TIMEOUT'),
 ('E05','PRIMASK调度器自检','mask=1投递完成IRQ，再恢复0','屏蔽期间不回调、不改BSP状态；恢复后才完成'),
 ('E06','TakeResult空指针','传输中及已完成时传NULL，再正常读取','false且不消费结果，随后正常交付'),
 ('E07','HAL_ERROR/TIMEOUT启动重试','完成前一事务，再模拟两种启动错误，最后成功启动','错误分别-9/-8并回IDLE，最后SYS_OK'),
 ('E08','Tick读取时完成竞态','100ms时在HAL_GetTick中投递完成IRQ','不Abort、不覆盖完成状态，下次交付SYS_OK'),
 ('E09','CPSID前最后边界完成','100ms时在__disable_irq执行前投递完成IRQ','不Abort，恢复PRIMASK，下次交付SYS_OK'),
 ('E10','初始化拒绝错误时钟源','RCC配置返回OK但模拟I2C2选择其他时钟源','INVALID_STATE=-4；不执行HAL I2C初始化、不启用NVIC'),
 ('E11','初始化拒绝错误频率','D2PCLK1源正确，但PCLK1为60MHz','INVALID_STATE=-4；不执行HAL I2C初始化、不启用NVIC'),
 ('E12','重复初始化时钟校验','120MHz重复Init，然后改变源、改变频率，再恢复','正常重复成功不重配硬件；两种时钟变更均拒绝；恢复后成功'),
]
commands = []
def run(command):
    p = subprocess.run([str(x) for x in command], cwd=root, capture_output=True)
    output = (p.stdout+p.stderr).decode('utf-8',errors='replace')
    record = {'argv':[str(x) for x in command], 'command':subprocess.list2cmdline([str(x) for x in command]), 'exit_code':p.returncode,'output':output}
    commands.append(record)
    (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
    print(record['command']); print(output,end='')
    if p.returncode:
        raise RuntimeError(f'Command failed: {p.returncode}')
    return output

compiler = Path('D:/32/ARM/ARMCC/Bin')
version = run([compiler/'armcc.exe','--vsn'])
obj = here/'communication_test.o'; image=here/'communication_test.axf'
run([compiler/'armcc.exe','--cpu','Cortex-M4','--c99','-O0',
     '-I',here/'stubs','-I',root/'CM4/BSP','-I',root/'CM4/Framework','-I',root/'Common/Inc',
     '-c',here/'communication_test.c','-o',obj])
run([compiler/'armlink.exe',obj,'--entry','Communication_TestRun','--scatter',here/'test.sct',
     '--keep=*(.framework_init)','--no_scanlib','-o',image])
with image.open('rb') as f:
    elf=ELFFile(f);entry=elf.header['e_entry']
    symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections()
              if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
fields=['failure_line','checks','trace_count','delivered','deferred','sync_calls','async_calls',
        'abort_calls','primask','pending','state','last_address','abort_address','callback_cached_result',
        'ev_calls','er_calls','bad_mask_delivery','bad_handle']
test_lines=(here/'communication_test.c').read_text(encoding='utf-8').splitlines()
states=['IDLE','TRANSFERRING','COMPLETED','ABORTING','FAULT']
rows=[];output_lines=[]
for case_id,(label,name,inputs,expected) in enumerate(cases):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    uc.mem_map(0x08000000,0x100000);uc.mem_map(0x20000000,0x40000);uc.mem_map(0x01000000,0x1000)
    for address,data in sections:uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0);uc.reg_write(UC_ARM_REG_LR,0x01000001);uc.reg_write(UC_ARM_REG_R0,case_id)
    error=None
    try:uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
    except Exception as exc:error=str(exc)
    vals=struct.unpack('<13Ii4I',uc.mem_read(symbols['g_result'],72));state=dict(zip(fields,vals))
    checks=[dict(zip(['line','actual','expected'],struct.unpack('<Iii',uc.mem_read(symbols['g_checks']+i*12,12)))) for i in range(min(state['checks'],512))]
    for check in checks:
        check['expression']=test_lines[check['line']-1].strip()
    trace=[dict(zip(['kind','mask','state','value'],struct.unpack('<IIIi',uc.mem_read(symbols['g_trace']+i*16,16)))) for i in range(min(state['trace_count'],256))]
    passed=(error is None and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0
            and state['failure_line']==0 and state['bad_mask_delivery']==0 and state['bad_handle']==0)
    actual=f"断言{state['checks']}项；最终{states[state['state']] if state['state']<5 else state['state']}；HAL同步/异步/Abort={state['sync_calls']}/{state['async_calls']}/{state['abort_calls']}；IRQ交付/屏蔽挂起={state['delivered']}/{state['deferred']}；PRIMASK={state['primask']}"
    if not passed:actual+=f"；失败行{state['failure_line']}；异常{error}"
    row=dict(id=label,name=name,input=inputs,expected=expected,actual=actual,result='PASS' if passed else 'FAIL',state=state,assertions=checks,trace=trace,emulator_error=error)
    rows.append(row);line=f"{label} {row['result']}: {name} — {actual}";print(line);output_lines.append(line)
passed=sum(x['result']=='PASS' for x in rows)
summary={'total':len(rows),'pass':passed,'fail':len(rows)-passed,'not_run':0,
         'assertions':sum(x['state']['checks'] for x in rows),'compiler':version,'unicorn_version':unicorn.__version__}
(here/'results.json').write_text(json.dumps({'summary':summary,'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# CM4 I2C2 同步/异步通信模拟测试','',
       '直接包含当前真实 CM4/BSP/bsp_iic.c；Mock HAL；ARMCC5 编译 ARM 可执行映像，Unicorn Cortex-M 模式实际执行。',
       '普通中断在 PRIMASK=1 时只挂起，恢复 0 且 NVIC 使能后经真实 BSP IRQ 入口调用真实 BSP 回调。',
       '每例从新的仿真内存开始，通过真实 BUS Initcall 初始化；断言与时间线详见 results.json。','',
       '| 编号 | 测试 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|---|']
lines += [f"| {r['id']} | {r['name']} | {r['input'].replace('|',' / ')} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',f"总数 {len(rows)}，PASS {passed}，FAIL {len(rows)-passed}，未执行 0；断言 {summary['assertions']} 项。",'',
          '## 运行限制','',
          'Mock 不模拟 I2C 寄存器、总线电气行为或 HAL 内部中断分支。队列表示可交付的 HAL 回调事件，测试不等同于寄存器级外设仿真。',
          '无 Abort 完成回调时保持 ABORTING 属于本次已接受设计；未增加恢复、二次超时或新状态机。',
          '回调和 TakeResult 按当前主循环/中断使用场景测试，不支持多个前台调用者并发访问的保证。',
          '真实 SCL、上拉、电源、SSD1306 应答和显示结果均未验证。','',
          '## 实际编译命令','', '```text']
lines += [x['command'] for x in commands];lines+=['```','',version]
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(here/'execution_output.txt').write_text('\n'.join(output_lines)+f'\n{summary}\n',encoding='utf-8')
print(f"TOTAL {len(rows)} PASS {passed} FAIL {len(rows)-passed} NOT_RUN 0; assertions={summary['assertions']}")
raise SystemExit(0 if passed==len(rows) else 1)
