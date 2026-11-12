"""Fixed-baseline production scope, original assertion hashes, ARMCC ABI and Map audit."""
from pathlib import Path
import subprocess,json,re,hashlib,html,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='182c84e7334f0a62d96f1d3109b0d7fb8efb8158'
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'-c','core.quotepath=false',*args],cwd=ROOT)
def old(path):return git('show',BASE+':'+path)
def sha(b):return hashlib.sha256(b).hexdigest()
new_sources=['CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h']
project='MDK-ARM/h7_mac.uvprojx'
allowed=new_sources+[project]
changed=git('diff',BASE,'--name-only').decode('utf-8').splitlines()
untracked=git('ls-files','--others','--exclude-standard','-z').decode('utf-8').split('\0')
paths=[p for p in changed+untracked if p and not p.startswith('output/')]
checks={'scope_only':all(p in allowed or p.startswith('Tests/CameraLifecycleC2_1_1/') or p=='Docs/Design/C2.1.1_Replay安全复用.md' for p in paths)}
tracked=git('ls-tree','-r','--name-only',BASE).decode('utf-8').splitlines()
protected=[p for p in tracked if p.startswith(('CM7/','CM4/','Common/','Board/','Drivers/','Middlewares/','MDK-ARM/','Tests/')) and p!=project]
# Git compares complete tracked contents, including deletions; avoid one subprocess per historical file.
modified=[p for p in changed if p in set(protected)]
checks['all_existing_production_and_tests_byte_unchanged']=not modified
# Remove only the approved CM7 file entry and compare the complete project tree.
a=ET.fromstring(old(project));b=ET.fromstring((ROOT/project).read_bytes())
registrations=[]
for target in b.findall('./Targets/Target'):
 for files in target.findall('.//Files'):
  for node in list(files):
   if node.findtext('FileName')=='app_camera_lifecycle_replay.c':
    registrations.append({'target':target.findtext('TargetName'),'path':node.findtext('FilePath')});files.remove(node)
def normalize(e):
 e.text=(e.text or '').strip();e.tail=(e.tail or '').strip()
 for child in e:normalize(child)
normalize(a);normalize(b)
checks['only_cm7_source_registration']=registrations==[{'target':'h7_mac_CM7','path':'../CM7/Application/app_camera_lifecycle_replay.c'}] and ET.tostring(a)==ET.tostring(b)
source=(ROOT/new_sources[0]).read_text(encoding='utf-8')
checks['no_dynamic_memory_tasks_or_business_hooks']=not re.search(r'\b(malloc|calloc|realloc|free|xTaskCreate|pvPortMalloc|FRAMEWORK_INIT_REGISTER|CameraCapture_|USB_UVC_|StorageFS_|Device_)\b',source)
checks['no_runtime_manager_instances']=not re.search(r'^static\s+AppLifecycle(?:Tx|Rx)Context_t\b',source,re.M)
# Formal map proves compilation plus dead stripping, not exercised business integration.
formal=(ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.map').read_text(encoding='utf-8',errors='replace')
checks['formal_manager_compiled_and_removed']='Removing app_camera_lifecycle_replay.o(i.App_LifecycleRxPromote)' in formal
checks['formal_manager_not_called']=not re.search(r'App_Lifecycle(?:Tx|Rx)\w+\s+0x[\da-fA-F]+\s+Thumb Code',formal)
expected={'s_message_queues':360,'s_message_storage':1040,'s_message_handles':20,'s_lifecycle_queues':216,'s_lifecycle_storage':672,'s_lifecycle_handles':12}
actual={}
for key in expected:
 m=re.search(r'\b'+key+r'\s+0x[\da-fA-F]+\s+Data\s+(\d+)',formal);actual[key]=int(m[1]) if m else None
checks['queue_memory_and_capacities_unchanged']=actual==expected
# Read actual test symbols and uninjected call graphs, retaining unknown indirect-call limits.
abi=[]
for opt in ['O0','O2']:
 mp=(ROOT/('output/CameraLifecycleC2_1_1/arm/abi_'+opt+'.map')).read_text(encoding='utf-8',errors='replace')
 symbols={}
 for name,size in [('s_probe_tx',144),('s_probe_rx',332)]:
  m=re.search(r'\b'+name+r'\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)',mp)
  symbols[name]={'address':m[1],'size':int(m[2])} if m else None
  checks[opt+'_'+name+'_map_size']=bool(m) and int(m[2])==size
 graph=(ROOT/('output/CameraLifecycleC2_1_1/arm/abi_'+opt+'.htm')).read_text(encoding='utf-8')
 stacks={}
 for name in ['App_LifecycleRxPromote','App_LifecycleTxFlush','App_LifecycleRxFlushReply']:
  m=re.search(r'<P><STRONG><a name="[^\"]+"></a>'+name+r'</STRONG>(.*?)(?=<P><STRONG>|$)',graph,re.S)
  text=html.unescape(re.sub('<[^>]+>',' ',m[1])) if m else ''
  local=re.search(r'Stack size (\d+) bytes',text);depth=re.search(r'Max Depth = (\d+)',text)
  stacks[name]={'local_bytes':int(local[1]) if local else None,'known_depth_bytes':int(depth[1]) if depth else None}
 checks[opt+'_stack_evidence_present']=all(v['local_bytes'] is not None for v in stacks.values())
 abi.append({'optimization':opt,'symbols':symbols,'stacks':stacks,'indirect_callback_stack_not_included':True})
# Baseline build reports are the committed C1.1 evidence; current formal rebuild is independent.
baseline_sizes={'h7_mac_CM7':[136802,3086,1328,100048],'h7_mac_CM4':[26302,1438,936,3576]}
sizes={}
for target,want in baseline_sizes.items():
 text=(HERE/(target+'_rebuild.log')).read_text(encoding='utf-8',errors='replace')
 m=re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)',text)
 got=list(map(int,m.groups())) if m else []
 sizes[target]={'baseline':want,'current':got,'delta':[x-y for x,y in zip(got,want)]}
 checks[target+'_program_size_unchanged']=got==want
 checks[target+'_zero_errors_warnings']='0 Error(s), 0 Warning(s).' in text
# Old outputs are neither deleted nor committed; build runner saved existing formal artifacts.
checks['previous_formal_outputs_preserved']=all((ROOT/'output/CameraLifecycleC2_1_1/build/previous_output'/t/(t+'.axf')).exists() for t in baseline_sizes)
result={'baseline':BASE,'checks':checks,'all_pass':all(checks.values()),'modified_protected_files':modified,'files':sorted(set(paths)), 'registration':registrations,'queue_symbols':actual,'program_sizes':sizes,'standalone_abi':abi,'future_four_domain_ram_bytes':1904,'formal_static_ram_delta_bytes':0,'formal_task_stack_delta_bytes':0,'hardware_verified':False,'new_source_sha256':{p:sha((ROOT/p).read_bytes()) for p in new_sources}}
(HERE/'scope_memory_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'all_pass':result['all_pass'],'checks':checks,'program_sizes':sizes,'abi':abi},ensure_ascii=False,indent=2));raise SystemExit(not result['all_pass'])
