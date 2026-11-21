"""Verify exact production allowlist, unchanged CM4 target and no write commands."""
from pathlib import Path
import subprocess, json, re, xml.etree.ElementTree as ET, hashlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
BASE = 'ce27b00062cba53ea90f137acc17ee360b8383ee'
def old(rel):
    return subprocess.check_output(['git', 'show', BASE + ':' + rel], cwd=ROOT)

project = 'MDK-ARM/h7_mac.uvprojx'
before = ET.fromstring(old(project))
after = ET.fromstring((ROOT / project).read_bytes())
targets = lambda tree: {t.findtext('TargetName'): t for t in tree.findall('./Targets/Target')}
b, a = targets(before), targets(after)
assert ET.tostring(b['h7_mac_CM4']) == ET.tostring(a['h7_mac_CM4'])
added = {'bsp_qspi.c', 'bsp_qspi.h', 'stm32h7xx_hal_qspi.c'}
seen = []
for parent in a['h7_mac_CM7'].iter('Files'):
    for f in list(parent):
        if f.findtext('FileName') in added:
            seen.append(f.findtext('FileName'))
            parent.remove(f)
assert sorted(seen) == sorted(added)
assert ET.tostring(before) == ET.tostring(after)
conf = 'CM7/Core/Inc/stm32h7xx_hal_conf.h'
assert old(conf).replace(b'/* #define HAL_QSPI_MODULE_ENABLED   */',
                         b'#define HAL_QSPI_MODULE_ENABLED') == (ROOT / conf).read_bytes()
source = (ROOT / 'CM7/BSP/bsp_qspi.c').read_text(encoding='utf-8')
header = (ROOT / 'CM7/BSP/bsp_qspi.h').read_text(encoding='utf-8')
assert re.findall(r'command.Instruction\s*=\s*(0x[0-9A-F]+)U', source) == ['0x9F']
assert not re.search(r'\b(malloc|free|xTaskCreate\w*|xSemaphore\w*|HAL_QSPI_Transmit\w*|HAL_QSPI_MemoryMapped|HAL_QSPI_\w+_DMA)\s*\(', source)
assert not re.search(r'QSPI_HandleTypeDef|FreeRTOS|TaskHandle_t', header)
assert '\t' not in source and '\t' not in header
for rel in ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h',
            'Tests/QspiFlashQ1/behavior_test.c', 'Tests/QspiFlashQ1/hal_link_test.c']:
    text = (ROOT / rel).read_text(encoding='utf-8')
    assert '\t' not in text
    assert not any(len(line) - len(line.lstrip()) == 0 and line.startswith('{ ') for line in text.splitlines())
row = dict(cm4_target_unchanged=True, cm7_only_three_file_entries=True,
           hal_conf_only_switch=True, only_flash_opcode='0x9F', private_hal_handle=True,
           static_allocation=True, sha256={rel:hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
             for rel in ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h', conf, project]})
(OUT / 'scope_audit.json').write_text(json.dumps(row, indent=2))
print(json.dumps(row, indent=2))
