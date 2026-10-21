from pathlib import Path
import hashlib,json,xml.etree.ElementTree as E,re,sys
sys.stdout.reconfigure(encoding='utf-8');t=Path('Tests/SdramCleanup')
old=json.loads((t/'baseline.json').read_text(encoding='utf-8'))
changed=[f for f,h in old.items() if not Path(f).exists() or hashlib.sha256(Path(f).read_bytes()).hexdigest()!=h]
expected={'CM7/BSP/bsp_sdram.c','CM7/BSP/bsp_sdram.h','CM7/Diagnostics/sdram_check.c','CM7/Diagnostics/sdram_check.h','MDK-ARM/h7_mac.uvprojx','MDK-ARM/h7_mac.uvoptx'}
assert set(f.replace('\\','/') for f in changed)==expected,changed
print('Changed/removed:',changed)
text=Path('CM7/BSP/bsp_sdram.c').read_bytes().decode('gbk')
assert 'return Sdram_Init();' in text
assert 'HAL_RCCEx_GetPLL2ClockFreq(&clocks);' in text
assert 'g_sdram_' not in text
source=E.parse('MDK-ARM/h7_mac.uvprojx').getroot()
for target in source.findall('Targets/Target'):
 assert not any(g.findtext('GroupName')=='CM7\\Diagnostics' for g in target.findall('Groups/Group'))
 assert 'Diagnostics' not in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath','')
 if target.findtext('TargetName')=='h7_mac_CM7':
  assert '--preinclude ../CM7/BSP/bsp_sdram_hal_config.h' in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/MiscControls')
# Demonstrate BSP logic changes are only removal of recording assignments.
a=(t/'before/CM7/BSP/bsp_sdram.c').read_bytes().decode('gbk').replace('\r\r\n','\n').replace('\r\n','\n')
b=text.replace('\r\r\n','\n').replace('\r\n','\n')
a=a.replace('volatile SysResult_t g_sdram_init_result = SYS_ERR_INVALID_STATE;\nvolatile uint32_t g_sdram_sdclk_hz;\n\n','').replace('\tg_sdram_sdclk_hz = frequency / 2U;\n','').replace('\tg_sdram_init_result = Sdram_Init();\n\treturn g_sdram_init_result;','\treturn Sdram_Init();')
assert a==b
scan=[]
for folder in ['CM7','CM4','Common','Board']:
 for p in Path(folder).rglob('*'):
  if p.suffix in ['.c','.h'] and p.is_file():
   content=p.read_bytes().decode('gbk',errors='replace')
   if any(x in content for x in ['SDRAM_CheckRun','g_sdram_','sdram_check.h']):scan.append(str(p))
assert not scan,scan
(t/'audit.json').write_text(json.dumps({'changed_or_removed_files':changed,'protected_sources_unchanged':True,'mpu_region2_main_unchanged':True,'bsp_only_removes_diagnostic_state':True,'formal_diagnostic_references':scan,'test_diagnostics_path':'Tests/SdramV0/diagnostics'},ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS: init clock/GPIO/commands unchanged; main/MPU2/IPC/CM4/OV5640/Framework/Scatter unchanged; no formal diagnostic references.')
