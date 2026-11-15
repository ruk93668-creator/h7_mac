"""Final ID/source/resource audit, preserving frozen records and raw failures."""
from pathlib import Path
import json,subprocess,hashlib,struct,sys,re,collections,shutil
H=Path(__file__).resolve().parent;ROOT=H.parents[2];P=H.parent;E=H/'Evidence';g=['git','-c','safe.directory='+ROOT.as_posix()]
freeze=json.loads((H/'Freeze.json').read_text());assert subprocess.check_output(g+['rev-parse','HEAD']).decode().strip()==freeze['baseline']
assert not subprocess.check_output(g+['diff','177c0e1','--','CM7','CM4','Common','Board','MDK-ARM'])
for name,sha in freeze['freeze_files'].items():assert hashlib.sha256((H/'RawResults/Frozen'/name).read_bytes()).hexdigest()==sha
base_gate=json.loads((H/'RawResults/Frozen/153_gate.json').read_text());records=[json.loads(p.read_text(encoding='utf-8')) for p in (H/'Records').glob('*.json')]
assert len(records)==153 and {(r['original_suite'],r['original_id']) for r in records}=={(r['suite'],r['id']) for r in base_gate['cases']}
assert sum(r['original_status']=='BLOCKED' for r in records)==128
# Full rerun ID collections/order/outcomes, including legacy known FAILs.
comparisons=[]
for suite in ['c1','c1_1','c2_1_1','c2a','r2','b1','b1_media','b1_integration','b2','b3','business','consumer','power','i2c']:
 a=json.loads((P/'ID17/Evidence/history'/f'{suite}.json').read_text());b=json.loads((E/'history'/f'{suite}.json').read_text())
 if isinstance(a,list):a={'cases':a,'summary':{'total':len(a),'passed':sum(x['pass'] for x in a)}}
 if isinstance(b,list):b={'cases':b,'summary':{'total':len(b),'passed':sum(x['pass'] for x in b)}}
 key=lambda x:tuple(str(x.get(k)) for k in ['group','optimization','id','case'])
 ka=[key(x) for x in a['cases']];kb=[key(x) for x in b['cases']];assert ka==kb and len(set(kb))==len(kb)
 assert [x['pass'] for x in a['cases']]==[x['pass'] for x in b['cases']]
 comparisons.append({'suite':suite,'IDs':len(kb),'missing':[],'new':[],'duplicate':[],'order_equal':True,'outcome_changes':[],'summary':b['summary'],'raw_status':'PASS' if all(x['pass'] for x in b['cases']) else 'FAIL'})
(H/'RegressionComparison.json').write_text(json.dumps(comparisons,indent=2))
recheck=json.loads((H/'rechecks.json').read_text());assert recheck['exit']==0
r=json.loads((H/'RawResults/ID17Clean/results.json').read_text());assert str(r['artifact_root']).startswith(recheck['snapshot']) and all(x['expectation_matched'] for x in r['cases'])
execution=json.loads((H/'execution.json').read_text());assert [x['name'] for x in execution['runs'] if not x['exit_as_expected']]==['id17']
# Do not accept stale archived result files following a nonzero preflight exit.
(H/'ExecutionVerdict.json').write_text(json.dumps({'initial_runs':execution['runs'],'ignored_stale_initial_result':'Evidence/id17_results.json','fresh_ID17_recheck':recheck,'authoritative_ID17':'RawResults/ID17Clean/results.json','unexpected_failures_remaining':[]},indent=2))
# Compare actual rebuilt runtime Map columns and loadable ELF sections with baseline177.
now=json.loads((E/'resources_abi_map_stack.json').read_text());baseline=json.loads((P/'ID17/Evidence/resources_abi_map_stack.json').read_text());build=json.loads((E/'build_build.json').read_text());oldbuild=json.loads((P/'ID17/Evidence/build_build.json').read_text());resource=[]
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'));from elftools.elf.elffile import ELFFile

def load_hash(path):
 with Path(path).open('rb') as f:
  elf=ELFFile(f);parts=sorted((x['sh_addr'],x.data()) for x in elf.iter_sections() if x['sh_flags']&2 and x['sh_type']=='SHT_PROGBITS' and x['sh_size'])
  return hashlib.sha256(b''.join(struct.pack('<II',a,len(b))+b for a,b in parts)).hexdigest()
for a,b,t,old in zip(now['map'],baseline['map'],build['targets'],oldbuild['targets']):
 assert a['totals'][:5]==b['totals'][:5] and a['symbols']==b['symbols'] and a['stack']==b['stack']
 assert t['exit']==0 and t['errors']==0 and t['warnings']==0
 x,y=load_hash(t['artifacts']['axf']['path']),load_hash(old['artifacts']['axf']['path']);assert x==y
 resource.append({'target':t['target'],'RW_ZI_current':a['totals'][3:5],'RW_ZI_delta':[0,0],'runtime_columns_equal':True,'debug_bytes_delta':a['totals'][5]-b['totals'][5],'stack':a['stack'],'symbols':a['symbols'],'loadable_SHA256':x,'baseline_loadable_SHA256':y,'artifacts':t['artifacts']})
assert now['ABI']==[232,144,332,824,56,52,24,40,144,200]
(H/'Resources.json').write_text(json.dumps({'ABI':now['ABI'],'related_offset_reference':json.loads((P/'ID17/resources.json').read_text())['measured_ABI'],'Map_comparison_to_177c0e1':resource,'production_RAM_delta':0,'all_indirect_stack_proven':False},indent=2))
# Freeze all new compiler commands and logs, keep binaries/maps in their unique output directories.
m=json.loads((H/'probe_results.json').read_text());commands=Path(m['artifact_root'])/'arm/commands.json';shutil.copy2(commands,H/'RawResults/final_probe_compile_commands.json');c=json.loads(commands.read_text());assert all(x['exit']==0 and 'Warning:' not in x['output'] for x in c)
checks=json.loads((H/'contracts.json').read_text());assert checks['passed']==306
assert json.loads((H/'gate_selftests.json').read_text())['passed']==11
assert json.loads((H/'UniqueFail.json').read_text())['root_closed']
print('PASS: all 153 IDs; 306 contract executions; frozen failures; full regression; actual firmware resources; no production changes')
