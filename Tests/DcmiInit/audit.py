from pathlib import Path
import hashlib,json,re,xml.etree.ElementTree as E,sys
sys.stdout.reconfigure(encoding='utf-8');p=Path('Tests/DcmiInit')
old=json.loads((p/'baseline.json').read_text(encoding='utf-8'));changed=[f for f,h in old.items() if not Path(f).exists() or hashlib.sha256(Path(f).read_bytes()).hexdigest()!=h]
assert set(f.replace('\\','/') for f in changed)=={'CM7/BSP/bsp_dcmi.c','CM7/BSP/bsp_dcmi.h','CM7/Core/Src/main.c','MDK-ARM/h7_mac.uvprojx'},changed
before=(p/'before/CM7/Core/Src/main.c').read_bytes().decode('gbk');after=Path('CM7/Core/Src/main.c').read_bytes().decode('gbk')
without_dcmi=after.replace('#include "bsp_dcmi.h"\r\n','').replace('BSP_DCMI_EarlyInit();\r\n','')
additional_main_change=without_dcmi!=before
if additional_main_change:
 assert without_dcmi.count('SysResult_t init_error;')==before.count('SysResult_t init_error;')==1
 assert ''.join(without_dcmi.replace('SysResult_t init_error;','').split())==''.join(before.replace('SysResult_t init_error;','').split())
# Early pin setup precedes both sensor pin setup and HSEM release.
assert after.index('BSP_DCMI_EarlyInit();')<after.index('BSP_Camera_EarlyInit();')<after.index('HAL_HSEM_Release(HSEM_ID_0,0)')
code=Path('CM7/BSP/bsp_dcmi.c').read_bytes().decode('gbk')
assert not re.search(r'HAL_DCMI_Start|HAL_DMA_Start|FrameBuffer|Framebuffer|0xD000',code)
for name in ['DMA1_Stream0_IRQHandler','DCMI_IRQHandler']:
 definitions=[]
 for folder in ['CM7','Common','Board']:
  for f in Path(folder).rglob('*.c'):
   if re.search(r'void\s+'+name+r'\s*\([^)]*\)\s*\{',f.read_bytes().decode('gbk',errors='replace')):definitions.append(str(f))
 assert len(definitions)==1,(name,definitions)
root=E.parse('MDK-ARM/h7_mac.uvprojx').getroot()
for target in root.findall('Targets/Target'):
 if target.findtext('TargetName')=='h7_mac_CM4':
  assert 'bsp_dcmi_hal_config.h' not in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/MiscControls','')
  group=next(g for g in target.findall('Groups/Group') if g.findtext('GroupName')=='CM7\\BSP')
  assert group.findtext('GroupOption/CommonProperty/IncludeInBuild')=='0'
(p/'audit.json').write_text(json.dumps({'changed_existing_files':changed,'new_file':'CM7/BSP/bsp_dcmi_hal_config.h','init_error_relocated_preserved':additional_main_change,'protected_sources_unchanged':True,'boot_change_only_early_pins':True,'irq_unique':True,'no_capture_start':True,'cm4_excludes_dcmi':True},ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS',changed)
