"""Run the original BSP and both H1 entry points under ARMCC and Unicorn."""
from pathlib import Path
import sys

profile = sys.argv[1]
assert profile in ('backup', 'backup120', 'write')
harness = Path(__file__).resolve().with_name('run_tests.py')
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", repr('output/QspiFlashQ1/h1/mock/' + profile))
source = source.replace('behavior_test.c', 'h1_test.c').replace('Q1_TestRun', 'H1_TestRun')
source = source.replace("'-DCORE_CM7',", f"'-DCORE_CM7', '-DQSPI_H1_{profile.upper()}',")
source = source.replace('count=2000000', 'count=25000000')
names = (['dual read backup and one shot', '03 and 0B differ'] if profile == 'backup' else
         ['120 MHz dual read and SFDP', '0B and 6B differ', 'SFDP control fails', 'Quad disabled'] if profile == 'backup120' else
         ['pattern and both reads', 'wrong target', 'wrong live UID', 'neighbor differs', 'current differs',
          'bad CRC', 'operation does not match arm', 'erase current snapshot', 'restore nonblank backup',
          'blank restore requires no WREN', 'QE clear before program', 'quad mismatch after program',
          'original pattern range occupied'])
start = source.index('names = [')
end = source.index('\nrows = ', start)
source = source[:start] + 'names = ' + repr(names) + source[end:]
exec(compile(source, str(harness), 'exec'))
