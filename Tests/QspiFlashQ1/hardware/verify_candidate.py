"""Read-only artifact audit; no probe enumeration or download actions."""
from pathlib import Path
import sys, json, hashlib, subprocess, xml.etree.ElementTree as ET
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'output/QspiFlashQ1' / sys.argv[1]
assert OUT.resolve().is_relative_to((ROOT / 'output/QspiFlashQ1').resolve())
BASE = 'd35c6464a9392d97f04f103bc828f59800d2c751'
scope = json.loads((OUT / 'download_scope.json').read_text())
assert scope['execution_authorized'] is False
rows = []
for core in ['CM7', 'CM4']:
    project = OUT / 'source/MDK-ARM' / ('qspi_readonly_' + core + '.uvprojx')
    tree = ET.parse(project)
    targets = tree.findall('./Targets/Target')
    assert len(targets) == 1
    files = [f.findtext('FilePath', '').replace('\\', '/') for f in tree.findall('.//File')]
    assert not any('/Application/' in p or '/Framework/' in p or '/Service/' in p for p in files)
    assert not any(p.endswith('/Core/Src/main.c') for p in files)
    startups = [p for p in files if p.startswith('startup_')]
    assert startups == ['startup_stm32h747xx_' + core + '.s']
    assert all(n.text == '0' for n in tree.iter('UpdateFlashBeforeDebugging'))
    name = 'h7_mac_' + core
    axf = OUT / 'source/MDK-ARM' / name / (name + '.axf')
    with axf.open('rb') as f:
        elf = ELFFile(f)
        symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()
                   if s['st_value'] and s['st_shndx'] != 'SHN_UNDEF'}
    row = dict(core=core, startup=startups, source_files=files,
               axf_sha256=hashlib.sha256(axf.read_bytes()).hexdigest())
    if core == 'CM7':
        required = ['BSP_QSPI_Init', 'BSP_QSPI_GetId', 'BSP_QSPI_GetDiagnostics',
                    'HAL_QSPI_Init', 'HAL_QSPI_Command', 'HAL_QSPI_Receive', 'HAL_QSPI_Abort']
        assert all(s in symbols for s in required)
        assert not any(s in symbols for s in ['HAL_QSPI_Transmit', 'HAL_QSPI_MemoryMapped',
                                             'HAL_QSPI_Transmit_DMA', 'HAL_QSPI_Receive_DMA'])
        row['retained_symbols'] = {s:hex(symbols[s]) for s in required}
        row['ram_symbols'] = {s:hex(symbols[s]) for s in ['g_q1_hw_arm', 'g_q1_hw_report']}
    rows.append(row)
for rel in ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h',
            'CM7/Core/Inc/stm32h7xx_hal_conf.h', 'MDK-ARM/h7_mac.uvprojx']:
    original = subprocess.check_output(['git', 'show', BASE + ':' + rel], cwd=ROOT)
    assert (ROOT / rel).read_bytes() == original
    assert (OUT / 'source' / rel).read_bytes() == original
(OUT / 'artifact_audit.json').write_text(json.dumps(rows, indent=2))
print('Artifact scope and accepted production baseline: PASS. Hardware execution: NOT_VERIFIED.')
