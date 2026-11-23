"""Run the 30 and 120 MHz firmware logic through the existing ARM harness."""
from pathlib import Path
import sys

mhz = sys.argv[1]
assert mhz in ('30', '120')
harness = Path(__file__).resolve().with_name('run_tests.py')
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", repr(f'output/QspiFlashQ1/quad_id_mock/{mhz}'))
source = source.replace('behavior_test.c', 'quad_id_test.c').replace('Q1_TestRun', 'QuadId_TestRun')
source = source.replace("'-DCORE_CM7',", f"'-DCORE_CM7', '-DQSPI_QUAD_ID_DIAGNOSTIC={int(mhz)*1000000}',")
names = ['EF 17 repeated', 'IO0/IO3 swapped', 'extra dummy shifts output',
         'command timeout', 'receive timeout', 'QE clear',
         'short receive', 'IO1/IO2 swap indistinguishable', 'device busy',
         'operation suspended', 'invalid pointer and clock']
start = source.index('names = [')
end = source.index('\nrows = ', start)
source = source[:start] + 'names = ' + repr(names) + source[end:]
exec(compile(source, str(harness), 'exec'))
