"""Rebuild original Keil targets in a fresh snapshot. Never invokes download."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
BASE = 'ec5ba117a9483b62ee16f6f7b35a285f9d82bce3'
profile = sys.argv[1]
assert profile in ('30', '120', 'formal')
run = sys.argv[2] if len(sys.argv) > 2 else 'verified'
assert re.fullmatch(r'[a-zA-Z0-9_-]+', run)
dest = ROOT / 'output/QspiFlashQ1/quad_id' / run / profile
dest.mkdir(parents=True)
archive = subprocess.check_output(['git', 'archive', BASE], cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(archive)) as files:
    files.extractall(dest, filter='data')
inputs = ['CM7/BSP/bsp_qspi.c', 'Tests/QspiFlashQ1/hardware/m7_project_test.c',
          'Tests/QspiFlashQ1/hardware/qspi_quad_id.h', 'Tests/QspiFlashQ1/hardware/qspi_quad_id_probe.h']
sha = lambda data: hashlib.sha256(data).hexdigest()
for rel in inputs:
    (dest / rel).write_bytes((ROOT / rel).read_bytes())
rel_project = Path('MDK-ARM/h7_mac.uvprojx')
original = (ROOT / rel_project).read_bytes()
def select_profile(match):
    macros = match[1].split(b',')
    diagnostics = (b'QSPI_QUAD_ID_DIAGNOSTIC', b'QSPI_SFDP_DIAGNOSTIC')
    macros = [macro for macro in macros if macro.strip().split(b'=')[0] not in diagnostics]
    if profile != 'formal':
        macros.append(f'QSPI_QUAD_ID_DIAGNOSTIC={int(profile) * 1000000}'.encode())
    return b'<Define>' + b','.join(macros) + b'</Define>'

project, count = re.subn(rb'<Define>(CORE_CM7,[^<]+)</Define>', select_profile, original)
assert count == 1
targets = ET.fromstring(project).findall('./Targets/Target')
defined = [t.findtext('TargetName') for t in targets
           if any('QSPI_QUAD_ID_DIAGNOSTIC' in (d.text or '') or
                  'QSPI_SFDP_DIAGNOSTIC' in (d.text or '') for d in t.iter('Define'))]
assert defined == ([] if profile == 'formal' else ['h7_mac_CM7'])
(dest / rel_project).write_bytes(project)
rows = []
for target in (('h7_mac_CM7', 'h7_mac_CM4') if profile == 'formal' else ('h7_mac_CM7',)):
    log = dest / (target + '.log')
    command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(dest / rel_project), '-t', target, '-o', str(log)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    code = subprocess.run(command, cwd=dest, startupinfo=startup,
                          creationflags=subprocess.CREATE_NO_WINDOW).returncode
    raw = log.read_bytes()
    assert code == 0 and re.findall(rb'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)[-1:] == [(b'0', b'0')], log
    axf = dest / 'MDK-ARM' / target / (target + '.axf')
    with axf.open('rb') as f:
        elf = ELFFile(f)
        loads = [(p['p_paddr'], p['p_paddr'] + p['p_filesz']) for p in elf.iter_segments()
                 if p['p_type'] == 'PT_LOAD' and p['p_filesz']]
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    names = ('g_qspi_quad_id_arm', 'g_qspi_quad_id_report', 'QspiDiagnostic_ReadQuadId')
    if profile != 'formal':
        assert loads and all(0x08000000 <= a < b <= 0x08040000 for a, b in loads)
        assert all(n in symbols for n in names) and 'g_q12_hw_arm' not in symbols
    else:
        assert not any(n in symbols for n in names)
        if target == 'h7_mac_CM7':
            assert 'g_q12_hw_arm' in symbols
    row = dict(target=target, errors=0, warnings=0, loads=[[hex(a), hex(b)] for a,b in loads],
               axf_sha256=sha(axf.read_bytes()), log_sha256=sha(raw),
               symbols={n: hex(symbols[n]) for n in names if n in symbols})
    rows.append(row)
    print(json.dumps(row), flush=True)
(dest / 'results.json').write_text(json.dumps(dict(baseline=BASE, profile=profile,
    original_project_sha256=sha(original), build_project_sha256=sha(project),
    sources={p: sha((dest / p).read_bytes()) for p in inputs}, builds=rows), indent=2) + '\n')
