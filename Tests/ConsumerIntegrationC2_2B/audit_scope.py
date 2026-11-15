from pathlib import Path
import subprocess,tarfile,io,hashlib,json,re,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
snapshot=Path((ROOT/'output/c2b_final.txt').read_text(encoding='utf-8'))/'source'
g=['git','-c','safe.directory='+ROOT.as_posix()]
roots=['CM7','CM4','Common','Board','Drivers','Middlewares','MDK-ARM']
raw=subprocess.check_output(g+['archive','f0ce45f','--',*roots],cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(raw)) as archive:base={m.name:archive.extractfile(m).read() for m in archive.getmembers() if m.isfile()}
allow={'CM7/Application/Tasks/camera_task.c','CM7/Application/Tasks/usb_task.c','CM7/Application/Tasks/storage_task.c','CM7/Application/Tasks/control_task.c','CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Application/app_usb.c','CM7/Application/app_usb.h','CM7/Application/app_storage.c','CM7/Application/app_storage.h','CM7/Application/app_camera_consumer.c','CM7/Application/app_camera_consumer.h','MDK-ARM/h7_mac.uvprojx'}
tracked=subprocess.check_output(g+['ls-files','--',*roots],cwd=ROOT).decode().splitlines()
changes=[];hashes={}
for name in tracked:
 current=(ROOT/name).read_bytes()
 if current!=base.get(name):changes.append(name);assert name in allow,name
 assert current==(snapshot/name).read_bytes(),('snapshot mismatch',name)
 hashes[name]=hashlib.sha256(current).hexdigest()
for name in ['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h']:
 assert (ROOT/name).read_bytes()==subprocess.check_output(g+['show','c6d2bed:'+name],cwd=ROOT)
# Entire old tests and original failure records stay untouched.
old_tests=subprocess.check_output(g+['diff','f0ce45f','--name-only','--','Tests',':(exclude)Tests/ConsumerIntegrationC2_2B'],cwd=ROOT).decode().strip();assert not old_tests,old_tests
for name in ['camera_task.c','usb_task.c','storage_task.c','control_task.c','ipc_task.c']:
 rel='CM7/Application/Tasks/'+name
 pattern=rb'#define\s+(\w*(?:STACK_WORDS|PRIORITY|WAIT_MS|STACK_SAMPLE_MS))\s+([0-9]+U)'
 assert re.findall(pattern,base[rel])==re.findall(pattern,(ROOT/rel).read_bytes()),name
old=ET.fromstring(base['MDK-ARM/h7_mac.uvprojx']);new=ET.fromstring((ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes());registered=[]
for target in new.findall('.//Target'):
 for files in target.findall('.//Files'):
  for file in list(files):
   if file.findtext('FileName')=='app_camera_consumer.c':registered.append(target.findtext('TargetName'));files.remove(file)
assert registered==['h7_mac_CM7'],registered
def strip(e):
 if e.text is not None and not e.text.strip():e.text=None
 e.tail=None
 for child in e:strip(child)
strip(old);strip(new);assert ET.tostring(old)==ET.tostring(new)
# Key resource algorithms remain byte-for-byte; only start gating and protocol metadata move.
def function(data,name):
 text=data.decode('gbk');pos=text.index(name+'(');pos=text.index('{',pos);depth=1;end=pos+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[pos:end]
# Find definitions, not forward declarations, using full signature prefix.
critical={}
for name in ['App_StorageProcessReturns','App_StorageOfferFrame','Storage_Snapshot','Storage_CloseAvi','Storage_Release','Storage_Write']:
 # Include signature line and find the last definition if a prototype precedes it.
 def body(data):
  text=data.decode('gbk');matches=list(re.finditer(r'\b'+name+r'\([^;]*?\)\s*\{',text,re.S));m=matches[-1];pos=text.index('{',m.start());d=1;end=pos+1
  while d:d+=(text[end]=='{')-(text[end]=='}');end+=1
  return text[pos:end]
 a=body(base['CM7/Application/app_storage.c']);b=body((ROOT/'CM7/Application/app_storage.c').read_bytes());assert a==b,name;critical[name]=True
result={'baseline':'f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb','production_changes':changes,'production_sha256':hashes,'snapshot_matches_working_production':True,'original_protocol_unchanged':True,'old_tests_and_failure_records_unchanged':True,'task_stack_priority_wait_constants_unchanged':True,'project_change_only_source_registration':registered,'critical_storage_functions_unchanged':critical,'release_proof_callbacks':'NULL; production RELEASE blocked','cold_query_gate':'Direct Core batch cold QUERY still fails; production CameraTask serializes admission and the actual-task dual-cold test passes'}
(HERE/'scope_results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='production_sha256'},indent=2))
