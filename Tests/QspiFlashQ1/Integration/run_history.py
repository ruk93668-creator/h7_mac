"""Retain original FIFO assertions and compare all historical failures."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R2, UC_ARM_REG_PC

from build import ROOT

build_root=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve()
out.mkdir(parents=True,exist_ok=False)
rows=[]
commands=[]
for variant in ('baseline','candidate'):
    tree=build_root/(variant+'_tree')
    for suite in ('SdramRange','DcmiReplaceTarget','UsbUvcDescriptors'):
        p=subprocess.run(['python',str(tree/'Tests'/suite/'run_tests.py')],cwd=tree,capture_output=True)
        (out/(variant+'_'+suite+'.log')).write_bytes(p.stdout+p.stderr)
        result=json.loads((tree/'Tests'/suite/'results.json').read_text(encoding='utf-8'))
        (out/(variant+'_'+suite+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        rows.append(dict(variant=variant,suite=suite,exit=p.returncode))
    for opt in ('O0','O2'):
        dest=out/(variant+'_'+opt)
        (dest/'sys').mkdir(parents=True)
        (dest/'sys/mman.h').write_text('''#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 1
#define MAP_ANONYMOUS 2
#define MAP_FIXED_NOREPLACE 4
void *mmap(void *, unsigned, int, int, int, int);
''')
        source=dest/'history.c'
        source.write_text('''#define main UnusedHostMain
#include "'''+(tree/'Tests/MjpegSdramFifo/fifo_test.c').as_posix()+'''"
#undef main
void *mmap(void *address, unsigned size, int protection, int flags, int fd, int offset)
{ (void)address; (void)size; (void)protection; (void)flags; (void)fd; (void)offset; return 0; }
unsigned HistoryRun(unsigned id) { if (id < 15U) Run(id+1U); else PayloadCase(); return 0U; }
''')
        cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','--split_sections','-'+opt,
            '-D__disable_irq=Test_FifoDisableIrq','-I',str(dest),'-I',str(tree/'Tests/MjpegSdramFifo/stubs')]
        for inc in ('Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Service','CM7/Platform'):
            cc+=['-I',str(tree/inc)]
        scatter=dest/'history.sct'
        scatter.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x80000 { .ANY (+RW +ZI) } }\n')
        axf=dest/'history.axf'
        for cmd in (cc+['-c',str(source),'-o',str(dest/'history.o')],
                    ['D:/32/ARM/ARMCC/Bin/armlink.exe',str(dest/'history.o'),'--entry','HistoryRun','--scatter',str(scatter),'-o',str(axf)]):
            p=subprocess.run(cmd,capture_output=True)
            commands.append(dict(command=cmd,exit=p.returncode,output=(p.stdout+p.stderr).decode(errors='replace')))
            (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
            if p.returncode: raise RuntimeError(commands[-1]['output'])
        with axf.open('rb') as f:
            e=ELFFile(f)
            syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
            sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
        cases=[]
        assert_symbol = '__aeabi_assert' if '__aeabi_assert' in syms else 'abort'
        for case in range(16):
            u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
            for addr,size in ((0x08000000,0x100000),(0x20000000,0x80000),(0xD0000000,0x1000000),(0x01000000,0x1000)):
                u.mem_map(addr,size)
            for addr,data in sections: u.mem_write(addr,data)
            failure={}
            def check(machine,address,size,context):
                if address == (syms[assert_symbol] & ~1):
                    if assert_symbol == '__aeabi_assert':
                        ptr=machine.reg_read(UC_ARM_REG_R0)
                        failure.update(line=machine.reg_read(UC_ARM_REG_R2),expression=bytes(machine.mem_read(ptr,300)).split(b'\0')[0].decode())
                    else:
                        failure.update(line=None,expression='ARMCC optimized standard assert reached abort')
                    machine.emu_stop()
            u.hook_add(UC_HOOK_CODE,check,begin=syms[assert_symbol]&~1,end=syms[assert_symbol]&~1)
            u.reg_write(UC_ARM_REG_SP,0x2007fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
            error=None
            try:u.emu_start(syms['HistoryRun']|1,0x01000000,count=100000000)
            except Exception as exc:error=str(exc)
            cases.append(dict(case=case+1,failure=failure,error=error,
                              passed=not error and not failure and u.reg_read(UC_ARM_REG_PC)==0x01000000))
        (dest/'results.json').write_text(json.dumps(cases,indent=2)+'\n')
        print(variant,opt,'FIFO',sum(r['passed'] for r in cases),'PASS',flush=True)
comparison=[]
for opt in ('O0','O2'):
    a=json.loads((out/('baseline_'+opt)/'results.json').read_text())
    b=json.loads((out/('candidate_'+opt)/'results.json').read_text())
    comparison.append(dict(suite='FIFO_'+opt,equal=a==b,failed_cases=[r['case'] for r in b if not r['passed']]))
for suite in ('SdramRange','DcmiReplaceTarget','UsbUvcDescriptors'):
    a=json.loads((out/('baseline_'+suite+'.json')).read_text(encoding='utf-8'))
    b=json.loads((out/('candidate_'+suite+'.json')).read_text(encoding='utf-8'))
    if suite=='SdramRange': same=a['cases']==b['cases'] and a['compiled_values']==b['compiled_values']
    elif suite=='DcmiReplaceTarget':same=a['cases']==b['cases']
    else:same=a['summary']==b['summary'] and a['core_cases']==b['core_cases']
    comparison.append(dict(suite=suite,equal=same))
(out/'comparison.json').write_text(json.dumps(comparison,indent=2)+'\n')
(out/'runs.json').write_text(json.dumps(rows,indent=2)+'\n')
assert all(r['equal'] for r in comparison),comparison
