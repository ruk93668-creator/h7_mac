"""Audit exact source scope, default BSP preprocessing and MCU-only images."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from build import ROOT, BASE, QSPI, FILES

OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(parents=True, exist_ok=False)
sha = lambda b: hashlib.sha256(b).hexdigest()
git = lambda *a: subprocess.check_output(['git', *a], cwd=ROOT)
changed = sorted(set(git('diff','--name-only',BASE,'--','CM7','CM4','Common','MDK-ARM','Drivers','Board','Middlewares').decode().splitlines()) |
                 set(git('ls-files','--others','--exclude-standard','--','CM7','CM4','Common','MDK-ARM').decode().splitlines()))
assert set(changed) <= set(FILES), changed
for f in ('CM7/ApplicationV3','CM4','CM7/Framework','Common','CM7/Platform','CM7/Media','CM7/Service'):
    assert not git('diff',BASE,'--',f), f
old=git('show',BASE+':MDK-ARM/h7_mac.uvprojx')
new=(ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()
ot={t.findtext('TargetName'):t for t in ET.fromstring(old).findall('./Targets/Target')}
nt={t.findtext('TargetName'):t for t in ET.fromstring(new).findall('./Targets/Target')}
canon=lambda x: ET.tostring(x,encoding='unicode')
assert canon(ot['h7_mac_CM4']) == canon(nt['h7_mac_CM4']), 'CM4 target changed'
for name in ot: assert canon(ot[name].find('TargetOption'))==canon(nt[name].find('TargetOption'))
getfiles=lambda t: {f.findtext('FilePath') for f in t.findall('.//File')}
extra=getfiles(nt['h7_mac_CM7'])-getfiles(ot['h7_mac_CM7'])
assert extra == {'..\\CM7\\BSP\\bsp_qspi.c','..\\Drivers\\STM32H7xx_HAL_Driver\\Src\\stm32h7xx_hal_qspi.c'},extra
source=ROOT/'CM7/BSP/bsp_qspi.c'
text=source.read_text(encoding='utf-8')
assert 'Tests/' not in text and 'Tests\\' not in text and 'QSPI_H1_' not in text
assert 's_write_policy = {0U, 0U}' in text
original=OUT/'original_bsp.c'
original.write_bytes(git('show',QSPI+':CM7/BSP/bsp_qspi.c'))
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-E',
    '-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER']
for inc in ['CM7/Core/Inc','CM7/BSP','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc',
            'Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include']:
    cc+=['-I',str(ROOT/inc)]
tokens=[]
for name,p in [('original',original),('product',source)]:
    r=subprocess.run([*cc,str(p)],capture_output=True)
    (OUT/(name+'.i')).write_bytes(r.stdout)
    assert r.returncode==0,r.stderr
    code=re.sub(rb'^\s*#.*$',b'',r.stdout,flags=re.M)
    tokens.append(re.findall(rb'"(?:\\.|[^"\\])*"|\w+|[^\s]',code))
assert tokens[0]==tokens[1], 'Default BSP tokens changed'
report=dict(baseline=BASE,qspi=QSPI,changed_production=changed,extra_cm7_files=sorted(extra),
            cm4_target_equal=True,target_options_equal=True,default_bsp_tokens_equal=True,
            default_bsp_tokens_sha256=sha(b'\0'.join(tokens[1])),write_policy_empty=True,
            production_source_sha256={f:sha((ROOT/f).read_bytes()) for f in FILES})
(OUT/'scope.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
