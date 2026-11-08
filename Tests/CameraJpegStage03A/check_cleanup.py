"""Check removal of the production acceptance branch without changing normal CameraTask."""
from pathlib import Path
import subprocess,re,json,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
checks={}
def normal_task(s):
    lines=[];active=False;keep=True
    for line in s.splitlines():
        if line.strip() in ['#if APP_CAMERA_JPEG_TEST_ENABLED','#if !APP_CAMERA_JPEG_TEST_ENABLED']:
            assert not active;active=True;keep='!' in line;continue
        if active and line.strip()=='#else':keep=not keep;continue
        if active and line.strip()=='#endif':active=False;keep=True;continue
        if keep and line.strip()!='#include "app_camera_jpeg.h"':lines.append(line)
    assert not active
    return ''.join(''.join(lines).split())
old=subprocess.check_output(['git','show','8908f2a:CM7/Application/Tasks/camera_task.c'],cwd=ROOT).decode('gbk')
current=(ROOT/'CM7/Application/Tasks/camera_task.c').read_bytes().decode('gbk')
checks['normal_task_path_identical']=normal_task(old)==normal_task(current)
for name in ['app_camera_jpeg.c','app_camera_jpeg.h']:
    checks['moved_byte_identical_'+name]=(HERE/'acceptance'/name).read_bytes()==subprocess.check_output(['git','show','8908f2a:CM7/Application/'+name],cwd=ROOT)
    checks['removed_from_application_'+name]=not (ROOT/'CM7/Application'/name).exists()
needles=['APP_CAMERA_JPEG_TEST_ENABLED','app_camera_jpeg','App_CameraJpegProcess','g_camera_jpeg_test']
checks['no_production_test_references']=all(not any(x in p.read_bytes().decode('latin1') for x in needles) for folder in ['CM7','CM4','Common'] for p in (ROOT/folder).rglob('*') if p.suffix in ['.c','.h'])
project=(ROOT/'MDK-ARM/h7_mac.uvprojx').read_text(encoding='utf-8');checks['no_project_test_reference']=not any(x in project for x in needles)
checks['snapshot_and_mjpeg_sources_unchanged']=not subprocess.check_output(['git','diff','--name-only','8908f2a','--','CM7/BSP','CM7/Device','CM7/Media','CM7/Platform','CM7/Service','CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Application/video_config.c','CM4','Common'],cwd=ROOT).strip()
with (ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf').open('rb') as f:
    elf=ELFFile(f);names={x.name for x in elf.get_section_by_name('.symtab').iter_symbols()}
checks['no_linked_acceptance_context']='g_camera_jpeg_test' not in names and 'App_CameraJpegProcess' not in names
p=subprocess.run([sys.executable,str(ROOT/'Tests/CameraMjpegStage03B/run_build.py'),'--targets','CM7_JPEG_SINGLE','--prepare-only'],capture_output=True)
checks['obsolete_variant_explicitly_rejected']=p.returncode!=0 and b'Tests-only' in p.stderr+p.stdout
report=dict(total=len(checks),passed=sum(checks.values()),checks=checks,hardware_verified=False)
(HERE/'cleanup_checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(report);assert all(checks.values())
