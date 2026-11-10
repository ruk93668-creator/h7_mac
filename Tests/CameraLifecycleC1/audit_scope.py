"""C1 fixed-baseline scope audit; not a replacement for runtime or hardware tests."""
from pathlib import Path
import subprocess,json,re,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f'
def git(*a):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'-c','core.quotepath=false',*a],cwd=ROOT)
def old(p):return git('show',BASE+':'+p)
allowed=['CM7/Application/app_messages.c','CM7/Application/app_messages.h','CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','MDK-ARM/h7_mac.uvprojx']
changes=git('diff',BASE,'--name-only').decode().splitlines();checks={}
checks['only_approved_tracked_changes']=all(p in allowed or p.startswith('Tests/CameraLifecycleC1/') or p=='Docs/Design/C1_Camera生命周期协议基础.md' for p in changes)
checks['all_old_message_c_bytes_unchanged']=(ROOT/allowed[0]).read_bytes().startswith(old(allowed[0]))
b=(ROOT/allowed[1]).read_bytes();h=old(allowed[1]);checks['old_header_layout_and_apis_unchanged']=b.startswith(h[:-len(b'#endif\r\n')])
p='MDK-ARM/h7_mac.uvprojx';project=(ROOT/p).read_bytes();prior=old(p)
entry=b'\r\n            <File>\r\n              <FileName>app_camera_lifecycle.c</FileName>\r\n              <FileType>1</FileType>\r\n              <FilePath>../CM7/Application/app_camera_lifecycle.c</FilePath>\r\n            </File>'
checks['project_only_new_cm7_file']=project.count(entry)==1 and project.replace(entry,b'')==prior
root=ET.fromstring(project);base=ET.fromstring(prior)
get=lambda r,name:next(t for t in r.findall('./Targets/Target') if t.findtext('TargetName')==name)
checks['cm4_target_unchanged']=ET.tostring(get(root,'h7_mac_CM4'))==ET.tostring(get(base,'h7_mac_CM4'))
checks['cm7_options_unchanged']=ET.tostring(get(root,'h7_mac_CM7').find('TargetOption'))==ET.tostring(get(base,'h7_mac_CM7').find('TargetOption'))
excludes=[':(exclude)'+p for p in allowed]+[':(exclude)Tests/CameraLifecycleC1/**']
checks['no_other_production_or_old_test_changes']=not git('diff',BASE,'--name-only','--','CM7','CM4','Common','Drivers','Middlewares','MDK-ARM','Tests',*excludes).strip()
text=(ROOT/allowed[2]).read_bytes().decode('gbk')
checks['protocol_has_no_rtos_or_hardware_dependency']=not re.search(r'#include\s+"(?:FreeRTOS|task|queue|app_events|bsp_|usb_|camera_capture|device)',text) and not re.search(r'\b(?:CameraCapture_|Device_Start|Device_Stop|USB_UVC_|StorageFS_)',text)
checks['business_modules_have_no_protocol_calls']=not any(b'App_Lifecycle' in (ROOT/p).read_bytes() for p in ['CM7/Application/app_camera.c','CM7/Application/app_usb.c','CM7/Application/app_storage.c','CM7/Application/app_uvc_bridge.c','CM7/Application/Tasks/camera_task.c','CM7/Application/Tasks/usb_task.c','CM7/Application/Tasks/storage_task.c','CM7/Application/Tasks/control_task.c'])
# The formal map confirms retained old storage and the exact new buffer allocation.
m=(ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.map').read_text(encoding='utf-8',errors='replace')
expected={'s_message_queues':360,'s_message_storage':1040,'s_message_handles':20,'s_lifecycle_queues':216,'s_lifecycle_storage':672,'s_lifecycle_handles':12}
actual={}
for name,n in expected.items():
 match=re.search(r'\b'+name+r'\s+0x[0-9a-fA-F]+\s+Data\s+(\d+)',m);actual[name]=int(match[1]) if match else None
checks['formal_map_queue_sizes']=actual==expected
rw=re.search(r'Total RW\s+Size[^\n]*?\s(\d+)\s+\(',m);actual_rw=int(rw[1]) if rw else None
checks['bounded_memory_increase']=actual_rw==101376
result={'baseline':BASE,'checks':checks,'all_pass':all(checks.values()),'map_sizes':actual,'baseline_rw_bytes':100472,'current_rw_bytes':actual_rw,'ram_delta_bytes':actual_rw-100472 if actual_rw else None,'note':'Only preparation is live; consumer qualification, hardware control, timeout policy and STOP integration are not implemented in C1.'}
(HERE/'scope_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2));raise SystemExit(not result['all_pass'])
