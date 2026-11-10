"""Verify approved scope, unchanged synchronization and exact pre-existing behavior bodies."""
from pathlib import Path
import subprocess,json,re,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='d725a698203b3bcde479a290714a75b16bc9de16'
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),*args],cwd=ROOT)
def original(p):return git('show',BASE+':'+p)
paths=['CM7/Application/Tasks/storage_task.c','CM7/Application/app_storage.c','CM7/Application/app_storage.h']
changed=git('diff',BASE,'--name-only').decode().splitlines()
checks={}
checks['tracked_changes_are_only_approved']=all(p in paths or p.startswith('Tests/StorageModuleB3/') for p in changed)
app=(ROOT/paths[1]).read_bytes();old=original(paths[1])
# Removing precisely the approved substitutions must restore the complete old C file.
restored=re.sub(rb'//[^\r\n]*\r\nstatic struct\r\n\{\r\n(?:[^\r\n]*\r\n)*?\} s_business;\r\n\r\n',b'',app,count=1)
restored=re.sub(rb'//[^\r\n]*\r\nbool App_StorageIsReady\(void\)\r\n\{\r\n    return s_prepared;\r\n\}\r\n\r\n',b'',restored,count=1)
restored=restored.replace(b'g_storage_status.saved_frames = ++s_business.saved_frames;',b'g_storage_status.saved_frames++;')
restored=restored.replace(b'amount = s_business.saved_frames - s_fps_frames;',b'amount = g_storage_status.saved_frames - s_fps_frames;')
restored=restored.replace(b's_fps_frames = s_business.saved_frames;',b's_fps_frames = g_storage_status.saved_frames;')
restored=restored.replace(b's_business.file_number = s_next_file - 1U;\r\n        g_storage_status.file_number = s_business.file_number;',b'g_storage_status.file_number = s_next_file - 1U;')
restored=restored.replace(b'value = s_business.file_number;',b'value = g_storage_status.file_number;')
checks['entire_app_restores_exactly_after_approved_inverse']=restored==old
new_task=(ROOT/paths[0]).read_bytes()
checks['task_only_ready_gate_changed']=new_task.replace(b'!App_StorageIsReady()',b'g_storage_status.ready == 0U')==original(paths[0])
header=(ROOT/paths[2]).read_bytes();old_header=original(paths[2])
layout=lambda b:b[b.index(b'typedef struct',b.index(b'AppStorageConfig_t') if b'AppStorageConfig_t' in b else 0):b.index(b'} AppStorageStatus_t;')+len(b'} AppStorageStatus_t;')]
checks['diagnostic_layout_unchanged']=layout(header)==layout(old_header)
checks['synchronization_sequence_unchanged']=re.findall(rb'Platform_Sync\w+\([^;]*;',old)==re.findall(rb'Platform_Sync\w+\([^;]*;',app)
checks['no_debug_ready_business_read']=b'g_storage_status.ready' not in new_task
checks['no_debug_fps_read']=b'amount = g_storage_status.saved_frames' not in app and b's_fps_frames = g_storage_status.saved_frames' not in app
checks['no_debug_filename_read']=b'value = g_storage_status.file_number' not in app
# All tracked files outside the three approved production files must still equal baseline.
protected=[]
for p in git('ls-tree','-r','--name-only',BASE).decode().splitlines():
 if p.startswith(('CM7/','CM4/','Common/','Drivers/','Middlewares/','MDK-ARM/','Tests/')) and p not in paths:
  current=ROOT/p
  protected.append({'path':p,'sha256':hashlib.sha256(current.read_bytes()).hexdigest()} if current.exists() else {'path':p,'missing':True})
checks['no_protected_tracked_differences']=not git('diff',BASE,'--name-only','--','CM7','CM4','Common','Drivers','Middlewares','MDK-ARM','Tests',*[':(exclude)'+p for p in paths],':(exclude)Tests/StorageModuleB3/**').strip()
# The archive audit is read-only; report exact actual checks instead of treating them as runtime tests.
result={'baseline':BASE,'checks':checks,'all_pass':all(checks.values()),'protected_file_count':len(protected),'build_or_hardware_verification':False}
(HERE/'scope_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'checks':checks,'protected_file_count':len(protected)},indent=2));raise SystemExit(not result['all_pass'])
