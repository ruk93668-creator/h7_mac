import json, subprocess, sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent; root=here.parents[1]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(args):
    p=subprocess.run(list(map(str,args)),cwd=root,capture_output=True)
    commands.append({'command':subprocess.list2cmdline(list(map(str,args))),'returncode':p.returncode,'output':(p.stdout+p.stderr).decode(errors='replace')})
    (here/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
    if p.returncode: raise RuntimeError(commands[-1])
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for p in ['Tests/UvcBridge/stubs','Common/Inc','CM7/Media/Inc','CM7/Service','CM7/Framework']: a+=['-I',root/p]
run(a+['-c',here/'bridge_test.c','-o',here/'bridge_test.o'])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'bridge_test.o','--entry','Bridge_TestRun','--scatter',here/'test.sct','--no_scanlib','-o',here/'bridge_test.axf'])
with (here/'bridge_test.axf').open('rb') as f:
    e=ELFFile(f); entry=e.header['e_entry']
    sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['USB not ready', 'query error in IDLE', 'no Media frame', 'valid acquisition to HELD', 'successful loan', 'Submit BUSY', 'Submit invalid state', 'Submit IO', 'Submit invalid arg', 'Release failure no resubmit', 'invalid width', 'invalid height', 'invalid format', 'invalid byte length', 'null data', 'invalid frame Release retry', 'owned WAIT_FRAME', 'owned WAIT_SLOT', 'owned IN_FLIGHT', 'owned WAIT_CONFIRM', 'owned CANCELLING', 'owned RECOVERY', 'ownership returned', 'safe Class destruction', 'loan query IO', 'loan query BUSY', 'loan query TIMEOUT', 'Media release retry after USB return', 'IRQ cannot interpret class absence', 'PRIMASK cannot interpret class absence', 'IRQ no side effects', 'PRIMASK no side effects', 'two frame cycles', 'Alt0 between acquisition and submit', 'class absent in IDLE']
rows=[]
for i,name in enumerate(names):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:u.mem_map(addr,size)
    for addr,data in sections:u.mem_write(addr,data)
    u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,i)
    u.emu_start(entry|1,0x01000000,count=1000000)
    result=u.reg_read(UC_ARM_REG_R0)
    passed=result==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
    rows.append({'id':i+1,'name':name,'pass':passed,'failure_line':result})
    print(name,'PASS' if passed else 'FAIL',result)
summary={'total':len(rows),'pass':sum(x['pass'] for x in rows),'fail':sum(not x['pass'] for x in rows),'cases':rows}
(here/'results.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
assert not summary['fail']
