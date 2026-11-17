from pathlib import Path
import os,sys,json,struct,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/V2Phase3/tests';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as arm
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0
from elftools.elf.elffile import ELFFile
resources={}
def probe(name,case):
 syms,sections=arm.images[name];u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for addr,n in [(0x08000000,0x100000),(0x20000000,0x80000),(0xE000E000,0x2000),(0x01000000,0x1000)]:u.mem_map(addr,n)
 for addr,d in sections:u.mem_write(addr,d)
 sp=0x2007fff0;minimum=[sp]
 def visit(uc,a,n,user):minimum[0]=min(minimum[0],uc.reg_read(UC_ARM_REG_SP))
 u.hook_add(UC_HOOK_CODE,visit);u.reg_write(UC_ARM_REG_SP,sp);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
 u.emu_start(syms['EM_Run']|1,0x01000000,count=3000000)
 assert u.reg_read(UC_ARM_REG_R0)==0
 return {'case':case,'stack':sp-minimum[0],'sizes':list(struct.unpack('<8I',u.mem_read(syms['g_sizes'],32))) if case==0 else None}

original=arm.run
def run(cmd):
 cmd=list(cmd)
 if Path(str(cmd[0])).name.lower()=='armlink.exe':
  axf=Path(cmd[cmd.index('-o')+1]);cmd+=['--map','--list',axf.with_suffix('.map'),'--callgraph','--symbols']
 return original(cmd)
arm.run=run
rows=[]
for opt in ['O0','O2']:
 arm.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in arm.cc]
 arm.build('execution_'+opt,[HERE/'execution_test.c',*sorted((ROOT/'CM7/ApplicationV2').rglob('*.c'))],'EM_Run')
 for n in range(79):
  r=arm.execute('execution_'+opt,'EM_Run',n);rows.append({'id':n,'optimization':opt,**r});print(opt,n,r,flush=True)
 resources[opt]={'probes':[probe('execution_'+opt,i) for i in [0,31,32,36,39,44,56]],'objects':[]}
 for index,src in enumerate([HERE/'execution_test.c',*sorted((ROOT/'CM7/ApplicationV2').rglob('*.c'))]):
  if src.parent.name not in ['Lifecycle','Supervisor']:continue
  obj=OUT/('execution_'+opt+str(index)+'.o')
  with obj.open('rb') as f:
   elf=ELFFile(f);totals={'RO':0,'RW':0,'ZI':0}
   for sec in elf.iter_sections():
    if sec['sh_flags']&2:totals['ZI' if sec['sh_type']=='SHT_NOBITS' else 'RW' if sec['sh_flags']&1 else 'RO']+=sec['sh_size']
  resources[opt]['objects'].append(totals)
(HERE/'resources.json').write_text(json.dumps(resources,indent=2),encoding='utf-8')
summary={'total':len(rows),'pass':sum(x['pass'] for x in rows),'fail':sum(not x['pass'] for x in rows)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'CM7/ApplicationV2').rglob('*')) if p.is_file()},'backend':'controlled module owner/transport ports; NOT real tasks or hardware'},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['fail']))
