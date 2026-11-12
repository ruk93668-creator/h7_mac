"""Read-only scope, prerequisite identity, project, queue, Map and stack audit."""
from pathlib import Path
import subprocess,hashlib,json,re,html,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='c6d2bedc28683883ffa433f802f8c06aaec3c542'
def git(*a):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'-c','core.quotepath=false',*a],cwd=ROOT)
def old(p):return git('show',BASE+':'+p)
changed=git('diff',BASE,'--name-only').decode().splitlines()
untracked=git('ls-files','--others','--exclude-standard').decode().splitlines()
allowed={'CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Application/app_camera_qualification.c','CM7/Application/app_camera_qualification.h','CM7/Application/app_camera_qualification_internal.h','MDK-ARM/h7_mac.uvprojx','Docs/Design/C2.2_Consumer资格管理设计.md','Docs/Design/C2.2A_ConsumerQualificationCore实施报告.md'}
paths=set(changed+untracked)
checks={'scope_only':all(p in allowed or p.startswith(('Tests/ConsumerQualificationC2_2A/','output/')) for p in paths)}
protected=['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h','CM7/Application/app_messages.c','CM7/Application/app_messages.h','CM7/Application/Tasks/camera_task.c','CM7/Application/app_usb.c','CM7/Application/app_storage.c','CM7/Application/app_uvc_bridge.c','CM7/Application/app_frame_dispatcher.c']
checks['prerequisite_and_existing_boundaries_unchanged']=all((ROOT/p).read_bytes()==old(p) for p in protected)
existing_tests=git('ls-tree','-r','--name-only',BASE,'--','Tests').decode().splitlines()
checks['all_old_test_files_byte_identical']=not (set(changed) & set(existing_tests))
checks['original_c6d2bed_is_ancestor']=subprocess.run(['git','-c','safe.directory='+ROOT.as_posix(),'merge-base','--is-ancestor',BASE,'HEAD'],cwd=ROOT).returncode==0
project='MDK-ARM/h7_mac.uvprojx'
a=ET.fromstring(old(project));b=ET.parse(ROOT/project).getroot();registrations=[]
for target in b.findall('./Targets/Target'):
 for files in target.findall('./Groups/Group/Files'):
  for node in list(files):
   if node.findtext('FileName')=='app_camera_qualification.c':
    registrations.append({'target':target.findtext('TargetName'),'path':node.findtext('FilePath')});files.remove(node)
def norm(e):
 e.text=(e.text or '').strip();e.tail=(e.tail or '').strip()
 for child in e:norm(child)
norm(a);norm(b)
checks['only_cm7_file_registration']=registrations==[{'target':'h7_mac_CM7','path':'../CM7/Application/app_camera_qualification.c'}] and ET.tostring(a)==ET.tostring(b)
formal=(ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.map').read_text(encoding='utf-8',errors='replace')
expected={'s_message_queues':360,'s_message_storage':1040,'s_message_handles':20,'s_lifecycle_queues':216,'s_lifecycle_storage':672,'s_lifecycle_handles':12,'s_camera_stack':6144,'s_camera':948}
symbols={}
for key,size in expected.items():
 m=re.search(r'\b'+key+r'\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)',formal)
 symbols[key]={'address':m[1],'bytes':int(m[2])} if m else None
 checks['map_'+key]=bool(m) and int(m[2])==size
checks['core_compiled_not_business_activated']='Removing app_camera_qualification.o(i.App_QualificationProcess)' in formal
sizes={}
for t in ['h7_mac_CM7','h7_mac_CM4']:
 text=(HERE/(t+'_rebuild.log')).read_text(encoding='utf-8',errors='replace')
 m=re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)',text)
 sizes[t]=list(map(int,m.groups())) if m else None
 checks[t+'_full_build_zero_errors_warnings']='0 Error(s), 0 Warning(s).' in text
stacks=[]
for opt in ['O0','O2']:
 s=(ROOT/('output/ConsumerQualificationC2_2A/arm/adapter_'+opt+'.htm')).read_text(encoding='utf-8',errors='replace')
 fs={}
 for fn in ['App_QualificationProcess','Qual_Submit','Qual_Execute','App_CameraQualificationProcess']:
  m=re.search(r'<P><STRONG><a name="[^\"]+"></a>'+fn+r'</STRONG>(.*?)(?=<P><STRONG>|$)',s,re.S)
  text=html.unescape(re.sub('<[^>]+>',' ',m[1])) if m else ''
  local=re.search(r'Stack size (\d+) bytes',text);depth=re.search(r'Max Depth = (\d+)',text)
  fs[fn]={'local':int(local[1]) if local else None,'known_depth':int(depth[1]) if depth else None}
 stacks.append({'optimization':opt,'functions':fs,'indirect_callbacks_and_isr_not_fully_bounded':True})
 checks[opt+'_stack_evidence']=all(v['local'] is not None for v in fs.values())
checks['no_business_activation']=not any('App_CameraQualification' in (ROOT/p).read_text(encoding='utf-8',errors='replace') for p in ['CM7/Application/Tasks/camera_task.c','CM7/Application/Tasks/usb_task.c','CM7/Application/Tasks/storage_task.c','CM7/Application/Tasks/control_task.c'])
baseline_artifacts={}
manifest=json.loads((ROOT/'Tests/CameraLifecycleC2_1_1/artifact_manifest.json').read_text(encoding='utf-8'))
for target in ['h7_mac_CM7','h7_mac_CM4']:
 path=ROOT/'output/ConsumerQualificationC2_2A/build/previous_output'/target/(target+'.map')
 digest=hashlib.sha256(path.read_bytes()).hexdigest()
 matches=digest==manifest['MDK-ARM/'+target+'/'+target+'.map']['sha256']
 baseline_artifacts[target]={'preserved_map':str(path),'sha256':digest,'matches_c6d2bed_manifest':matches}
 checks[target+'_baseline_map_preserved']=matches
result={'baseline_artifacts':baseline_artifacts,'baseline':BASE,'checks':checks,'all_pass':all(checks.values()),'map_symbols':symbols,'program_sizes':sizes,'core_stack':stacks,'changed_paths_excluding_output':sorted(p for p in paths if not p.startswith('output/')),'hardware_verified':False}
(HERE/'scope_memory_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'all_pass':result['all_pass'],'checks':checks,'sizes':sizes},ensure_ascii=False,indent=2))
raise SystemExit(not result['all_pass'])
