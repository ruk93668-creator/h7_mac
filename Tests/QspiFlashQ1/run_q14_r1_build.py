"""Rebuild the R1 production tree in isolation; both BSP files must be unchanged."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
baseline = 'aa2248cd24d915dfb0054a33032067a62d28f956'
for path in ('CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h'):
    frozen = subprocess.check_output(['git', 'show', f'{baseline}:{path}'], cwd=root)
    assert (root / path).read_bytes() == frozen
harness = Path(__file__).with_name('run_q14_build.py')
source = harness.read_text(encoding='utf-8')
source = source.replace("OUT = ROOT / 'output/QspiFlashQ1/q14'",
                        "OUT = ROOT / 'output/QspiFlashQ1/q14_r1'")
source = source.replace("SOURCE = 'f480cc10485bf5939c13783f650887414d19516d'",
                        f"SOURCE = '{baseline}'")
sys.argv[1:] = ['current']
exec(compile(source, str(harness), 'exec'))
