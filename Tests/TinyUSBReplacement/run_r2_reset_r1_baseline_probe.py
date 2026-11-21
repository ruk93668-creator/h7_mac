"""Observe the pre-fix full AXF attempting a blocking END send while locked.
Stop intentionally at xQueueGenericSend entry; do not pretend the scheduler-ready
MMIO model can execute a real blocked RTOS task or prove physical deadlock timing.
"""
from pathlib import Path
import json,hashlib,struct,subprocess
HERE=Path(__file__).resolve().parent
exec((HERE/'run_r2_reset_r1_tests.py').read_text(encoding='utf-8').split('\nrows=[]')[0],globals())
BASELINE='f4dada109a9302a5eab66bcad8e2d7ad82a16738'
receipt=json.loads(subprocess.check_output(['git','show',BASELINE+':Tests/TinyUSBReplacement/reports/R2ResetClosure_receipt.json'],cwd=ROOT).decode('utf-8'))
axf=Path(receipt['bundle'])/'h7_mac_CM7_TinyUSB_RAM.axf'
assert hashlib.sha256(axf.read_bytes()).hexdigest()==receipt['axf_sha256']
with axf.open('rb') as f:
 e=ELFFile(f);load_segments=[(p['p_paddr'],p.data()) for p in e.iter_segments() if p['p_type']=='PT_LOAD' and p['p_filesz']]
 symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sizes={s.name:s['st_size'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
# Obtain queue offsets from this baseline AXF too, never guess current ABI.
source=(HERE/'run_r2_reset_r1_tests.py').read_text(encoding='utf-8')
exec(source[source.index('# Observe the compiled'):source.index('class R1Model')],globals())
v=R1Model();board(v);old=v.source(2);v.end_ready();v.fill(32);seen=[]
def detect(c,a,n,ctx):
 if a==(symbols['xQueueGenericSend']&~1) and c.reg_read(UC_ARM_REG_R0)==v.rd(symbols['_usbd_q']):
  ptr=c.reg_read(UC_ARM_REG_R1)
  if v.cpu.mem_read(ptr+1,1)[0]==2:
   seen.append(dict(event=2,queue_count=v.count(),timeout=c.reg_read(UC_ARM_REG_R2),basepri=c.reg_read(UC_ARM_REG_BASEPRI),phase=v.phase(),source_before_reset=old,source_at_send=v.source(2)))
   c.emu_stop()
v.cpu.hook_add(UC_HOOK_CODE,detect)
try:v.call('dcd_task',0)
except RuntimeError:
 if not seen:raise
assert len(seen)==1 and seen[0]['queue_count']==32 and seen[0]['timeout']==0xffffffff and seen[0]['basepri']==0x50,seen
report=dict(baseline=BASELINE,artifact=str(axf),axf_sha256=receipt['axf_sha256'],observation=seen[0],meaning='VERIFIED unsafe blocking native queue call in locked consumer context; execution intentionally suspended before blocking, NOT a real scheduler/hardware deadlock run',hardware='NOT_TESTED')
p=HERE/'reports/r2-reset-r1-baseline-probe.json';p.write_text(json.dumps(report,indent=2),encoding='utf-8');print(report)
