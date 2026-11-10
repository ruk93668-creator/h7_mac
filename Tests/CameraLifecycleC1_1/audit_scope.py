"""C1.1 fixed-baseline scope, semantic encoding and memory audit."""
from pathlib import Path
import subprocess,json,re,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='b684b76914b46da20314b27235f44b7d5d214fd5'
MAIN='35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f'
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'-c','core.quotepath=false',*args],cwd=ROOT)
def old(commit,path):return git('show',commit+':'+path)
production=['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_messages.c','CM7/Application/app_messages.h']
tests=['Tests/CameraLifecycleC1/protocol_test.c','Tests/CameraLifecycleC1/run_tests.py','Tests/CameraLifecycleC1/run_regression.py','Tests/CameraLifecycleC1/regression_compare.py','Tests/CameraLifecycleC1/results.json']
docs=['Docs/Design/C1_Camera生命周期协议基础.md']
changed=git('diff',BASE,'--name-only').decode('utf-8').splitlines()
checks={}
checks['approved_scope_only']=all(p in production+tests+docs or p.startswith('Tests/CameraLifecycleC1_1/') for p in changed)
# Reconstruct the old readable text; only the C1 suffix used GBK.
for path in production[2:]:
    original=old(BASE,path);prefix=old(MAIN,path)
    size=len(prefix) if path.endswith('.c') else len(prefix)-len(b'#endif\r\n')
    normalized=original[:size].decode('utf-8')+original[size:].decode('gbk')
    current=(ROOT/path).read_bytes().decode('utf-8')
    checks[Path(path).name+'_utf8_text_unchanged']=current==normalized
# The only implementation changes are the new READY cancel API and fail-closed retirement.
path=production[0];b=(ROOT/path).read_bytes()
start=b.index('//仅取消尚未提交的请求'.encode('gbk'));end=b.index('//相同请求只报告重复'.encode('gbk'),start)
restored=b[:start]+b[end:]
restored=restored.replace('    //没有可验证的旧请求失效边界，完成或ACK入队都不能授权退休\r\n    return SYS_ERR_NOT_SUPPORTED;'.encode('gbk'),b'    memset(record, 0, sizeof(*record));\r\n    return SYS_OK;')
checks['only_cancel_and_retire_implementation_changes']=restored==old(BASE,path)
path=production[1];b=(ROOT/path).read_bytes();original=old(BASE,path)
start=b.index('//仅取消READY请求'.encode('gbk'));end=b.index('//登记请求'.encode('gbk'),start)
restored=b[:start]+b[end:]
restored=restored.replace('//没有安全失效边界时不允许退休，已完成记录返回NOT_SUPPORTED'.encode('gbk'),'//仅允许显式撤销已终结缓存，C2负责安全退休时机'.encode('gbk'))
checks['existing_protocol_layout_and_signatures_unchanged']=restored==original
checks['project_unchanged_from_c1']=(ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()==old(BASE,'MDK-ARM/h7_mac.uvprojx')
exclude=[':(exclude)'+p for p in production]
checks['no_other_production_changes']=not git('diff',BASE,'--name-only','--','CM7','CM4','Common','Drivers','Middlewares','MDK-ARM',*exclude).strip()
# Keep the original 419 cases; only the unsafe retirement success expectation is replaced.
path='Tests/CameraLifecycleC1/protocol_test.c';b=(ROOT/path).read_bytes().decode('utf-8')
new='CHECK(App_LifecycleReplayComplete(&replay,&r)==0);CHECK(App_LifecycleReplayRetire(&replay)==SYS_ERR_NOT_SUPPORTED);CHECK(App_LifecycleReplayGetReply(&replay,&before)==0);CHECK(memcmp(&before,&r,sizeof(r))==0);break;'
oldcase='CHECK(App_LifecycleReplayComplete(&replay,&r)==0);CHECK(App_LifecycleReplayRetire(&replay)==0);break;'
checks['original_test_only_authorized_case56_strengthened']=b.replace(new,oldcase).encode('utf-8')==old(BASE,path)
map_text=(ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.map').read_text(encoding='utf-8',errors='replace')
expected={'s_message_queues':360,'s_message_storage':1040,'s_message_handles':20,'s_lifecycle_queues':216,'s_lifecycle_storage':672,'s_lifecycle_handles':12}
sizes={}
for key in expected:
    m=re.search(r'\b'+key+r'\s+0x[0-9a-fA-F]+\s+Data\s+(\d+)',map_text);sizes[key]=int(m[1]) if m else None
checks['queue_capacities_and_memory_unchanged']=sizes==expected
m=re.search(r'Total RW\s+Size[^\n]*?\s(\d+)\s+\(',map_text);rw=int(m[1]) if m else None
checks['ram_unchanged_from_c1']=rw==101376
result={'baseline':BASE,'checks':checks,'all_pass':all(checks.values()),'map_sizes':sizes,'current_rw_bytes':rw,'delta_from_c1_bytes':rw-101376 if rw else None,'delta_from_main_bytes':rw-100472 if rw else None,'hardware_verified':False}
(HERE/'scope_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2));raise SystemExit(not result['all_pass'])
