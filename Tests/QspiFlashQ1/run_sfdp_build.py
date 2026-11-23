"""Build the SFDP diagnostic on an isolated copy of the original M7 target."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import xml.etree.ElementTree as ET

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1/sfdp30'
BASE = 'c58f7d3899a3536c2db0dfdf0f777de781a999de'
TARGET = 'h7_mac_CM7'

OUT.mkdir(parents=True)
archive = subprocess.check_output(['git', 'archive', BASE], cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(archive)) as files:
    files.extractall(OUT, filter='data')

inputs = ['CM7/BSP/bsp_qspi.c', 'Tests/QspiFlashQ1/hardware/m7_project_test.c',
          'Tests/QspiFlashQ1/hardware/qspi_sfdp_diagnostic.h',
          'Tests/QspiFlashQ1/hardware/qspi_sfdp_probe.h']
for rel in inputs:
    (OUT / rel).write_bytes((ROOT / rel).read_bytes())

project_path = Path('MDK-ARM/h7_mac.uvprojx')
original_project = (ROOT / project_path).read_bytes()
project, count = re.subn(rb'(<Define>CORE_CM7,[^<]+)(</Define>)',
                         rb'\g<1>,QSPI_SFDP_DIAGNOSTIC\g<2>', original_project)
assert count == 1
targets = ET.fromstring(project).findall('./Targets/Target')
assert [t.findtext('TargetName') for t in targets
        if any('QSPI_SFDP_DIAGNOSTIC' in (d.text or '') for d in t.iter('Define'))] == [TARGET]
(OUT / project_path).write_bytes(project)

log = OUT / 'cm7.log'
command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(OUT / project_path),
           '-t', TARGET, '-o', str(log)]
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
code = subprocess.run(command, cwd=OUT, startupinfo=startup,
                      creationflags=subprocess.CREATE_NO_WINDOW).returncode
raw = log.read_bytes()
counts = re.findall(rb'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)
assert code == 0 and counts[-1:] == [(b'0', b'0')], log

axf = OUT / 'MDK-ARM' / TARGET / (TARGET + '.axf')
with axf.open('rb') as f:
    elf = ELFFile(f)
    loads = [(s['p_paddr'], s['p_paddr'] + s['p_filesz'])
             for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
    sym = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
assert loads and all(0x08000000 <= lo < hi <= 0x08040000 for lo, hi in loads)
assert 'g_q12_hw_arm' not in sym
symbols = {name: hex(sym[name]) for name in ('g_q1_hw_arm', 'g_q1_hw_report',
                                           'g_qspi_sfdp_arm', 'g_qspi_sfdp_report',
                                           'QspiDiagnostic_ReadSfdp')}
sha = lambda data: hashlib.sha256(data).hexdigest()
result = dict(baseline=BASE, target=TARGET, errors=0, warnings=0,
              original_project_sha256=sha(original_project), diagnostic_project_sha256=sha(project),
              axf_sha256=sha(axf.read_bytes()), flash_loads=[[hex(lo), hex(hi)] for lo, hi in loads],
              symbols=symbols, sources={p: sha((OUT / p).read_bytes()) for p in inputs},
              log_sha256=sha(raw))
(OUT / 'cm7.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
