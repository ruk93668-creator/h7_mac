from pathlib import Path
import json,hashlib,re,sys
sys.stdout.reconfigure(encoding='utf-8')
r=Path('.');t=r/'Tests/Ov5640ProductionCleanup';old=json.loads((t/'baseline.json').read_text(encoding='utf-8'))
changed=[p for p,h in old.items() if not Path(p).exists() or hashlib.sha256(Path(p).read_bytes()).hexdigest()!=h]
patterns=['g_camera_report','AppCameraReport_t','Ov5640ConfigReport_t','OV5640_VERIFY_COUNT','s_verify','checked_count','failed_reg']
left=[]
for folder in ['CM7','CM4','Common']:
 for p in (r/folder).rglob('*'):
  if p.is_file() and p.suffix in ['.c','.h']:
   text=p.read_bytes().decode('gbk',errors='replace')
   for item in patterns:
    if item in text:left.append([str(p),item])
print('Changed production files:');print('\n'.join(changed));print('Old report/verification references:',left)
(t/'audit.json').write_text(json.dumps({'changed_files':changed,'old_report_references':left},ensure_ascii=False,indent=2),encoding='utf-8')
print(Path('CM7/Device/Src/ov5640_device.c').read_bytes().decode('gbk')[-1350:])
