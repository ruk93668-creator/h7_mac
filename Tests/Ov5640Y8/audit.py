from pathlib import Path
import json,hashlib,xml.etree.ElementTree as E
r=Path('.');t=r/'Tests/Ov5640Y8';old=json.loads((t/'baseline.json').read_text(encoding='utf-8'))
changed=[p for p,h in old.items() if not Path(p).exists() or hashlib.sha256(Path(p).read_bytes()).hexdigest()!=h]
targets=E.parse('MDK-ARM/h7_mac.uvprojx').getroot().findall('Targets/Target')
groups={}
for target in targets:
 name=target.findtext('TargetName');groups[name]={}
 for group in target.findall('Groups/Group'):
  if group.findtext('GroupName') in ['CM7\\Application','CM7\\Device']:
   groups[name][group.findtext('GroupName')]={'include':group.findtext('GroupOption/CommonProperty/IncludeInBuild'),'files':[f.findtext('FileName') for f in group.findall('Files/File')]}
comp=r/'CM7/Device/Components/OV5640'
same={f: (comp/f).read_bytes()==(t/'reference'/f).read_bytes() for f in ['ov5640.c','ov5640_reg.c','ov5640.h','ov5640_reg.h']}
audit={'changed_protected_files':changed,'component_identical_to_old':same,'groups':groups}
(t/'project_audit.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(audit,ensure_ascii=False,indent=2))
