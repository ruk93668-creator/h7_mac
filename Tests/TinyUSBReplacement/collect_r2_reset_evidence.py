"""Record exact final image, call sites, historical assertion preservation and scope."""
from pathlib import Path
import hashlib,json,subprocess,sys,struct,shutil,re
R=Path(__file__).resolve().parents[2];P=R/'Tests/TinyUSBReplacement/reports';BASELINE='5e640632e3d6f7d0a0a6c7e8dfe58b6fc798a1e3'
b=json.loads((P/'r2-build.json').read_text(encoding='utf-8'));assert b['project_sha256']==hashlib.sha256((R/'MDK-ARM/h7_mac_TinyUSB_RAM.uvprojx').read_bytes()).hexdigest();axf=Path(b['output'])/'h7_mac_CM7_TinyUSB_RAM.axf'
for f,h in b['source_sha256'].items():assert hashlib.sha256((R/f).read_bytes()).hexdigest()==h,('stale build',f)
hash_axf=hashlib.sha256(axf.read_bytes()).hexdigest()
assert json.loads((P/'r2-reset.json').read_text(encoding='utf-8'))['axf_sha256']==hash_axf
assert json.loads((P/'r2-closure.json').read_text(encoding='utf-8'))['axf_sha256']==hash_axf
assert json.loads((P/'r2-firmware-tests.json').read_text(encoding='utf-8'))['axf_sha256']==hash_axf
sys.path.insert(0,str(R/'output/UDLM-P1/python_deps'));from elftools.elf.elffile import ELFFile
with axf.open('rb') as f:
 e=ELFFile(f);sy={s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols()};a=sy['__Vectors'][0]+117*4
 sec=next(s for s in e.iter_sections() if s['sh_type']=='SHT_PROGBITS' and s['sh_addr']<=a<s['sh_addr']+s['sh_size']);v=struct.unpack_from('<I',sec.data(),a-sec['sh_addr'])[0];assert v==sy['OTG_FS_IRQHandler'][0]
 for n in ['HAL_PCD_IRQHandler','USBD_Init','USB_Mode_Start','StorageTask_Create','pvPortMalloc']:assert n not in sy
 functions={n:hex(sy[n][0]) for n in ['tud_deinit','dcd_deinit','dcd_task','dcd_deinit_ready','dcd_edpt_close_all_safe','tud_disconnect','tud_connect','__scatterload','reset_begin','reset_poll','handle_bus_reset','dcd_reset_pending','dcd_reset_generation']}
old=subprocess.check_output(['git','show',BASELINE+':Tests/TinyUSBReplacement/run_r2_firmware_tests.py'],cwd=R).decode('utf-8').replace('\r\n','\n');current=(R/'Tests/TinyUSBReplacement/run_r2_firmware_tests.py').read_text(encoding='utf-8')
assert old[old.index('\nrows=[]'):]==current[current.index('\nrows=[]'):],'Historical 19-case assertions changed'
kept=[]
for f in subprocess.check_output(['git','ls-tree','-r','--name-only',BASELINE,'Tests/TinyUSBReplacement'],cwd=R,text=True).splitlines():
 if Path(f).suffix not in ['.c','.h','.py'] or Path(f).name in ['audit_upstream.py','run_r2_firmware_tests.py']:continue
 assert subprocess.check_output(['git','show',BASELINE+':'+f],cwd=R)==(R/f).read_bytes(),f
 kept.append(f)
for f in ['MDK-ARM/h7_mac.uvprojx','MDK-ARM/h7_mac.uvoptx','MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h','MDK-ARM/h7_mac_CM7_custom.sct']:
 assert subprocess.check_output(['git','show',BASELINE+':'+f],cwd=R)==(R/f).read_bytes(),f
assert not subprocess.check_output(['git','diff',BASELINE,'--name-only','--','Drivers','Middlewares','CM4'],cwd=R,text=True).strip()
for name in ['r1b','r1b-waits','stack']:
 for f,h in json.loads((P/(name+'.json')).read_text(encoding='utf-8')).get('sources',{}).items():assert hashlib.sha256((R/f).read_bytes()).hexdigest()==h,(name,f)
bundle=R/'output/TinyUSBReplacement'/('R2ResetClosure-review-'+hash_axf[:12]);bundle.mkdir(exist_ok=True)
for source in [axf,axf.with_suffix('.map'),axf.with_suffix('.hex'),Path(b['output'])/'build.log']:
 dest=bundle/source.name
 if dest.exists():assert dest.read_bytes()==source.read_bytes()
 else:shutil.copy2(source,dest)
log=(Path(b['output'])/'build.log').read_text(errors='replace');sizes=re.search(r'Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)',log).groups()
prod=['CM7/TinyUSB/tinyusb_experiment.c','MDK-ARM/h7_mac_TinyUSB_RAM.uvprojx','ThirdParty/tinyusb/UPSTREAM.json','ThirdParty/tinyusb/src/device/dcd.h','ThirdParty/tinyusb/src/device/usbd.c','ThirdParty/tinyusb/src/device/usbd.h','ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c','ThirdParty/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c']
(P/'R2ResetClosure_production.patch').write_bytes(subprocess.check_output(['git','diff','--binary',BASELINE,'--',*prod],cwd=R))
api=['tud_disconnect','tud_connect','tud_deinit','dcd_task','dcd_deinit_ready','dcd_edpt_close_all_safe','dcd_deinit','dcd_edpt_close_all','dwc2_core_deinit'];calls={n:[] for n in api}
for f in (R/'ThirdParty/tinyusb/src').rglob('*'):
 if f.suffix not in ['.c','.h']:continue
 for line,text in enumerate(f.read_text(encoding='utf-8').splitlines(),1):
  for name in api:
   if re.search(r'\b'+name+r'\s*\(',text):calls[name].append(dict(path=f.relative_to(R).as_posix(),line=line,text=text.strip()))
(P/'R2ResetClosure_callers.json').write_text(json.dumps(calls,indent=2),encoding='utf-8')
receipt=dict(baseline=BASELINE,full_build=b['summary'],bundle=str(bundle),axf_sha256=hash_axf,sizes=dict(zip(['code','ro','rw','zi'],map(int,sizes))),unique_irq=dict(index=117,entry=hex(v)),final_functions=functions,old_test_sources_unchanged=kept,old_19_assertion_body_unchanged=True,production={f:hashlib.sha256((R/f).read_bytes()).hexdigest() for f in prod},summaries={n:json.loads((P/(n+'.json')).read_text(encoding='utf-8')).get('summary') for n in ['r2-reset','r2-closure','r2-firmware-tests','r1b','stack','r1']},historical_contract_change=dict(tests=['R2 active Bulk Bus Reset boundary','Closure active Bulk Bus Reset remains hardware BLOCKED'],old='immediate fault',new='pending reset-specific retirement, no release before proof',classification='CONTRACT_CHANGED by ResetClosure task; original assertions retained; prior Disconnect FAIL retained'),active_reset='VERIFIED: full AXF model normal reset succeeds, missing proof fails; silicon NOT_TESTED',hardware='NOT_TESTED',pdf_sha256=hashlib.sha256((R/'Docs/references/RM0399_Rev5.pdf').read_bytes()).hexdigest())
# Preserve exact original case bodies and emit per-case deltas, not just process exit codes.
deltas={}
for name in ['r2-closure','r2-firmware-tests','r1b','stack','r1']:
 old=json.loads(subprocess.check_output(['git','show',BASELINE+':Tests/TinyUSBReplacement/reports/'+name+'.json'],cwd=R).decode('utf-8'))
 now=json.loads((P/(name+'.json')).read_text(encoding='utf-8'))
 assert [x['name'] for x in old['cases']]==[x['name'] for x in now['cases']]
 deltas[name]=[dict(name=x['name'],old_passed=x['passed'],new_passed=y['passed'],old_error=x.get('error'),new_error=y.get('error')) for x,y in zip(old['cases'],now['cases'])]
(P/'R2ResetClosure_caseComparison.json').write_text(json.dumps(deltas,indent=2),encoding='utf-8')
receipt['artifact_manifest']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in bundle.iterdir() if f.is_file()}
receipt['wait_boundary_cases']=len(json.loads((P/'r1b-waits.json').read_text(encoding='utf-8'))['cases'])
(P/'R2ResetClosure_receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8');print(json.dumps({k:receipt[k] for k in ['full_build','bundle','axf_sha256','summaries']}))
