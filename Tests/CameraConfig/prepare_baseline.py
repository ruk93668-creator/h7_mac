import hashlib
import json
import shutil
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parent.parent
paths = []
for folder in ['CM7', 'CM4', 'Common', 'Drivers', 'Board']:
    paths += [p for p in (root / folder).rglob('*') if p.is_file() and p.suffix in ['.c', '.h']]
paths += list((root / 'MDK-ARM').glob('*.sct'))
paths += [root / 'MDK-ARM/h7_mac.uvprojx']
baseline = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
(here / 'baseline.json').write_text(json.dumps(baseline, indent=2), encoding='utf-8')
for relative in ['CM7/Application/app_camera.c', 'CM7/Application/app_camera.h',
                 'CM7/Device/Inc/ov5640_device.h', 'CM7/Device/Src/ov5640_device.c',
                 'MDK-ARM/h7_mac.uvprojx']:
    dest = here / 'before' / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / relative, dest)

dst = here / 'Camera'
dst.mkdir(exist_ok=True)
src = root / 'Tests/SdramV0/Camera'
shutil.copytree(src / 'stubs', dst / 'stubs', dirs_exist_ok=True)
shutil.copyfile(src / 'test.sct', dst / 'test.sct')
test = (src / 'camera_test.c').read_text(encoding='utf-8')
test = test.replace('uint8_t g_wire_tx[128];',
                    'uint8_t g_wire_tx[128];\nuint32_t g_write_trace[2048];\nuint32_t g_write_trace_count;')
test = test.replace('registers[reg_pointer]=(uint8_t)acc;writes++;',
                    'registers[reg_pointer]=(uint8_t)acc;writes++;\n'
                    '       if(g_write_trace_count<2048)g_write_trace[g_write_trace_count++]=(reg_pointer<<8)|acc;')
(dst / 'camera_test.c').write_text(test, encoding='utf-8')
runner = (src / 'run_tests.py').read_text(encoding='utf-8')
runner = runner.replace('import argparse,json,struct,subprocess,sys', 'import argparse,json,struct,subprocess,sys,hashlib')
runner = runner.replace('Tests/SdramV0/Camera/stubs', 'Tests/CameraConfig/Camera/stubs')
marker = " print(f\"{id+1:02d} {rows[-1]['result']}:"
assert marker in runner
extra = ''' trace_count=struct.unpack('<I',u.mem_read(syms['g_write_trace_count'],4))[0]
 rows[-1]['write_trace_count']=trace_count
 rows[-1]['write_trace_sha256']=hashlib.sha256(u.mem_read(syms['g_write_trace'],trace_count*4)).hexdigest()
 rows[-1]['register_map_sha256']=hashlib.sha256(u.mem_read(syms['registers'],65536)).hexdigest()
'''
runner = runner.replace(marker, extra + marker)
(dst / 'run_tests.py').write_text(runner, encoding='utf-8')
print('Baseline prepared; run Camera/run_tests.py before production edits.')
