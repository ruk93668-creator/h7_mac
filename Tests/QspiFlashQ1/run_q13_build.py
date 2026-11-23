"""Rebuild the real M7 and M4 targets in an isolated, write-disabled snapshot."""
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1/q13'
BASE = 'b5c79ecb14956cd04dc2823c34273ca2be70d4da'
SOURCE = '7b36cbeb8228a444e379e3c261abd3addcdaeddf'
sha = lambda data: hashlib.sha256(data).hexdigest()

for profile, revision in (('baseline', BASE), ('current', SOURCE)):
    directory = OUT / profile
    directory.mkdir(parents=True)
    archive = subprocess.check_output(['git', 'archive', revision], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as files:
        files.extractall(directory, filter='data')

if '--prepare-only' in sys.argv:
    sys.exit(0)

relative_project = 'MDK-ARM/h7_mac.uvprojx'
local_project = (ROOT / relative_project).read_bytes()
pattern = rb',(QSPI_SFDP_DIAGNOSTIC|QSPI_QUAD_ID_DIAGNOSTIC(?:=[0-9]+)?)(?=[,<])'
project = re.sub(pattern, b'', local_project)
assert b'QSPI_SFDP_DIAGNOSTIC' not in project and b'QSPI_QUAD_ID_DIAGNOSTIC' not in project
directory = OUT / 'current'
(directory / relative_project).write_bytes(project)
rows = []
for target in ('h7_mac_CM7', 'h7_mac_CM4'):
    log = directory / (target + '.log')
    command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(directory / relative_project),
               '-t', target, '-o', str(log)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    code = subprocess.run(command, cwd=directory, startupinfo=startup,
                          creationflags=subprocess.CREATE_NO_WINDOW).returncode
    raw = log.read_bytes()
    counts = re.findall(rb'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)
    assert code == 0 and counts[-1:] == [(b'0', b'0')], log
    size = re.search(rb'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', raw)
    assert size
    artifact_folder = directory / 'MDK-ARM' / target
    axf = artifact_folder / (target + '.axf')
    with axf.open('rb') as infile:
        elf = ELFFile(infile)
        loads = [(p['p_paddr'], p['p_paddr'] + p['p_filesz']) for p in elf.iter_segments()
                 if p['p_type'] == 'PT_LOAD' and p['p_filesz']]
        names = {s.name for s in elf.get_section_by_name('.symtab').iter_symbols()}
    mutations = sorted(names & {'BSP_QSPI_Program', 'BSP_QSPI_EraseSector', 'HAL_QSPI_Transmit'})
    assert not mutations
    htm = (artifact_folder / (target + '.htm')).read_text(errors='replace')
    stack = re.search(r'Maximum Stack Usage =\s*(\d+)', htm)
    row = dict(target=target, errors=0, warnings=0, sizes=list(map(int, size.groups())),
               stack_bytes=int(stack[1]) if stack else None, write_symbols=mutations,
               loads=[[hex(a),hex(b)] for a,b in loads], axf_sha256=sha(axf.read_bytes()),
               log_sha256=sha(raw))
    rows.append(row)
    print(json.dumps(row), flush=True)
(OUT / 'build.json').write_text(json.dumps(dict(baseline=BASE, source=SOURCE,
    original_project_sha256=sha(local_project), build_project_sha256=sha(project),
    builds=rows), indent=2) + '\n')
