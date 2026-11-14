"""Verify formal logic, protected protocol/tests, and opt-in-only linkage boundary."""
from pathlib import Path
import subprocess,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='1c97d7cca0c19927f4405d2abd34e5cc918fcd51'
G=['git','-c','safe.directory='+ROOT.as_posix()]
def git(*args):return subprocess.check_output(G+list(args),cwd=ROOT)
path='CM7/Application/Tasks/control_task.c'
old=git('show',BASE+':'+path).decode('gbk').replace('\r\n','\n')
new=(ROOT/path).read_bytes().decode('gbk').replace('\r\n','\n')
preamble='\n#ifndef APP_R2_HW_ACCEPTANCE_ENABLED\n#define APP_R2_HW_ACCEPTANCE_ENABLED 0\n#endif\n#if APP_R2_HW_ACCEPTANCE_ENABLED != 0 && APP_R2_HW_ACCEPTANCE_ENABLED != 1\n#error APP_R2_HW_ACCEPTANCE_ENABLED_must_be_zero_or_one\n#endif\n#if APP_R2_HW_ACCEPTANCE_ENABLED\n#include "HardwareControl.h"\n#endif\n'
hook='\n#if APP_R2_HW_ACCEPTANCE_ENABLED\n        //独立验收构建才读取Watch邮箱，不改变原Control状态处理与发布顺序\n        (void)R2_HardwareControlProcess();\n#endif\n'
assert new.count(preamble)==1 and new.count(hook)==1
assert new.replace(preamble,'').replace(hook,'')==old,'Formal Control logic changed'
assert 'ControlTask_Process(events);\n        ControlTask_PublishStatus();\n'+hook in new
production=[p for p in git('diff','--name-only',BASE).decode().splitlines() if p.startswith(('CM7/','CM4/','Common/','Drivers/','Middlewares/','MDK-ARM/'))]
assert production==[path],production
protected=[]
for folder in ['Tests/CameraLifecycleC1','Tests/CameraLifecycleC1_1','Tests/CameraLifecycleC2_1_1']:
 for rel in git('ls-tree','-r','--name-only',BASE,folder).decode().splitlines():
  if rel.endswith(('.c','.h','.py')):
   assert (ROOT/rel).read_bytes()==git('show',BASE+':'+rel),rel;protected.append(rel)
for rel in ['CM7/Application/app_messages.c','CM7/Application/app_camera.c','CM7/Application/app_usb.c','CM7/Application/app_storage.c','CM7/Application/app_uvc_bridge.c','CM7/Platform/storage_fs.c','CM7/Platform/sd_block.c','CM7/BSP/bsp_sdmmc.c','CM7/Application/app_camera_lifecycle_replay.c','MDK-ARM/h7_mac.uvprojx']:
 assert (ROOT/rel).read_bytes()==git('show',BASE+':'+rel),rel;protected.append(rel)
report={'baseline':BASE,'branch':git('branch','--show-current').decode().strip(),'production_delta':production,'formal_control_body_identical_after_removing_opt_in_hook':True,'hook_after_existing_process_and_publish':True,'default_disabled':True,'protected_files':protected,'new_tasks':0,'new_queues':0,'dynamic_memory':False,'main':git('rev-parse','main').decode().strip(),'r2_branch':git('rev-parse','codex/stage-C2-2A-R2-stop-ownership').decode().strip(),'basepri_branch':git('rev-parse','fix/lifecycle-init-basepri').decode().strip()}
assert report['basepri_branch']==BASE
(HERE/'watch_scope_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='protected_files'},indent=2))
