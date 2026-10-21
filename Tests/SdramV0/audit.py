from pathlib import Path
import json,hashlib,re,xml.etree.ElementTree as E,sys
sys.stdout.reconfigure(encoding='utf-8');t=Path('Tests/SdramV0')
old=json.loads((t/'baseline.json').read_text(encoding='utf-8'))
changed=[f for f,h in old.items() if not Path(f).exists() or hashlib.sha256(Path(f).read_bytes()).hexdigest()!=h]
print('Changed existing files:',changed)
assert set(f.replace('\\','/') for f in changed)=={'CM7/Core/Src/main.c','MDK-ARM/h7_mac.uvprojx'}
a=(t/'before/CM7/Core/Src/main.c').read_bytes().decode('gbk');b=Path('CM7/Core/Src/main.c').read_bytes().decode('gbk')
# Remove user sections before comparing generated/boot/MPU0/1 code.
def outside(s):return re.sub(r'/\* USER CODE BEGIN ([^*]+?) \*/.*?/\* USER CODE END \1 \*/','',s,flags=re.S)
assert outside(a)==outside(b)
for tag in ['Boot_Mode_Sequence_0','Boot_Mode_Sequence_1','Boot_Mode_Sequence_2']:
 pattern=r'/\* USER CODE BEGIN '+tag+r' \*/.*?/\* USER CODE END '+tag+r' \*/'
 assert re.search(pattern,a,re.S).group()==re.search(pattern,b,re.S).group()
root=E.parse('MDK-ARM/h7_mac.uvprojx').getroot();targets={}
for target in root.findall('Targets/Target'):
 name=target.findtext('TargetName');targets[name]={}
 for group in target.findall('Groups/Group'):
  if group.findtext('GroupName') in ['CM7\\Diagnostics','CM7\\BSP']:
   targets[name][group.findtext('GroupName')]=group.findtext('GroupOption/CommonProperty/IncludeInBuild')
  if name=='h7_mac_CM4' and group.findtext('GroupName') in ['CM7\\Diagnostics','CM7\\BSP']:
   assert group.findtext('GroupOption/CommonProperty/IncludeInBuild')=='0'
print('Main generated/dual boot code unchanged: PASS; mirrored CM7 groups excluded from CM4: PASS')
(t/'audit.json').write_text(json.dumps({'changed_existing_files':changed,'main_changes_inside_user_sections':True,'dual_boot_unchanged':True,'targets':targets,'new_files':['CM7/BSP/bsp_sdram.c','CM7/BSP/bsp_sdram.h','CM7/BSP/bsp_sdram_hal_config.h','CM7/Diagnostics/sdram_check.c','CM7/Diagnostics/sdram_check.h']},ensure_ascii=False,indent=2),encoding='utf-8')
