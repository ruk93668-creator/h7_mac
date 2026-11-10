"""Check B1 production scope and preserve baseline main/history; no migration-local absolute paths."""
from pathlib import Path
import subprocess,json,hashlib,argparse
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--baseline',default='e44f549ee39a18f51fa2473d5ef4b10ce3b5753e');args=parser.parse_args()
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),*args],cwd=ROOT)
allowed=['CM7/Application/Tasks/camera_task.c','CM7/Application/app_camera.c','CM7/Application/app_camera.h']
changed=git('diff','--name-only',args.baseline,'--','CM7','CM4','Common','Board','Drivers','Middlewares','MDK-ARM').decode().splitlines()
old_tests=git('diff','--name-only',args.baseline,'--','Tests',':(exclude)Tests/CameraModuleB1').decode().splitlines()
source=(ROOT/allowed[0]).read_bytes().decode('gbk')
app=(ROOT/allowed[1]).read_bytes().decode('gbk')
checks={
 'only_authorized_production_files':set(changed)==set(allowed),
 'historical_tests_unchanged':not old_tests,
 'task_stack_1536_words':'#define CAMERA_TASK_STACK_WORDS 1536U' in source,
 'task_priority_3':'#define CAMERA_TASK_PRIORITY 3U' in source,
 'task_wait_20ms':'#define CAMERA_TASK_WAIT_MS 20U' in source,
 'stack_scan_1000ms':'#define CAMERA_TASK_STACK_SAMPLE_MS 1000U' in source,
 'no_task_business_statics':all(x not in source for x in ['s_camera_running','s_camera_prepared','s_camera_stopping','s_camera_ack','s_camera_fault','s_camera_fps']),
 'no_business_freertos_calls':all(x not in app for x in ['xTask','xQueue','App_MessagesSend(','App_MessagesReceive(','App_EventsWait(']),
 'main_unchanged':git('rev-parse','main').decode().strip()==args.baseline,
}
report={'baseline':args.baseline,'checks':checks,'changed_production_files':changed,'historical_test_changes':old_tests,'production_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in allowed},'note':'Scope checks are static checks, not executable or hardware validation.'}
(HERE/'scope_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(checks,indent=2));raise SystemExit(not all(checks.values()))
