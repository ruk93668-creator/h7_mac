"""Execute the isolated task against BSP API stubs at ARMCC5 O0/O2."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(parents=True, exist_ok=False)
os.environ['C1_TEST_OUTPUT_ROOT'] = str(OUT)
spec = importlib.util.spec_from_file_location('arm_harness', ROOT/'Tests/CameraLifecycleC1/run_tests.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

h.cc += ['-I', ROOT/'CM7/ApplicationV3/Core', '--split_sections']
names = ['unready no QSPI', 'startup failure stops', 'invalid latch ignored',
         'successful nonuniform reads', 'Init error no retry', 'JEDEC mismatch',
         'QE absent', 'WEL unsafe', 'read timeout', 'short completed',
         'quad mismatch', 'Init occupied', 'readiness revoked', 'Tick wrap deadline',
         'latch before readiness', 'static task creation fails']
rows = []
for opt in ('O0', 'O2'):
    h.cc = ['-'+opt if x in ('-O0', '-O2') else x for x in h.cc]
    h.build('coexist_'+opt, [Path(__file__).with_name('coexistence_mock.c')], 'Coexist_TestRun')
    symbols, sections = h.images['coexist_'+opt]
    for case, name in enumerate(names):
        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for a, size in [(0x08000000,0x100000),(0x20000000,0x80000),(0xE000E000,0x2000),(0x01000000,0x1000)]:
            u.mem_map(a,size)
        for a,data in sections: u.mem_write(a,data)
        u.reg_write(UC_ARM_REG_SP,0x2007fff0)
        u.reg_write(UC_ARM_REG_LR,0x01000001)
        u.reg_write(UC_ARM_REG_R0,case)
        error = None
        try: u.emu_start(symbols['Coexist_TestRun']|1,0x01000000,count=3000000)
        except Exception as e: error = str(e)
        value = lambda name: struct.unpack('<I',u.mem_read(symbols[name],4))[0]
        row = dict(optimization=opt,case=case,name=name,assertions=value('g_assertions'),
                   failure_line=value('g_failure'),error=error)
        row['pass'] = not error and not row['failure_line'] and u.reg_read(UC_ARM_REG_R0)==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
        rows.append(row)
        print(json.dumps(row),flush=True)
summary=dict(cases=len(rows),passed=sum(x['pass'] for x in rows),assertions=sum(x['assertions'] for x in rows),rows=rows)
(OUT/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
sys.exit(not all(r['pass'] for r in rows))
