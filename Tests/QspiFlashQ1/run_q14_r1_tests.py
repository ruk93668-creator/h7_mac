"""Run new R1 tests or separately preserve the strict multi-page boundary result."""
from pathlib import Path
import sys

boundary = sys.argv[1:] == ['--boundary']
assert boundary or not sys.argv[1:]
harness = Path(__file__).with_name('run_tests.py')
source = harness.read_text(encoding='utf-8')
folder = 'boundary' if boundary else 'required'
source = source.replace("'output/QspiFlashQ1/mock'", repr('output/QspiFlashQ1/q14_r1/' + folder))
source = source.replace('behavior_test.c', 'recovery_test.c').replace('Q1_TestRun','R1_TestRun')
source = source.replace("'-DCORE_CM7',", "'-DCORE_CM7', '-DQSPI_Q14_R1_TEST',")
source = source.replace('count=2000000', 'count=25000000')
names = ['R1-01 Program valid WEL=0', 'R1-02 Erase valid WEL=0',
         'R1-03a SR1 HAL_ERROR', 'R1-03b SR1 HAL_TIMEOUT and Abort error',
         'R1-03c SR1 short receive', 'R1-03d SR1 HAL_OK with HAL error',
         'R1-04a WREN HAL_BUSY', 'R1-04b WREN HAL_ERROR', 'R1-04c WREN HAL_TIMEOUT']
if boundary:
    names = ['R1-B01 prior verified page, next valid WEL=0 (strict whole-call contract)']
    source = source.replace('enumerate(names):', 'enumerate(names, start=9):')
start = source.index('names = [')
end = source.index('\nrows = ',start)
source = source[:start] + 'names = ' + repr(names) + source[end:]
anchor = "    row['pass'] = "
assert anchor in source
source = source.replace(anchor, """    row['observed'] = {k:value('g_r1_'+k) for k in
        ('ready','latched','wrens','modifications','mutation_possible','completed','first_result')}
    row['observed']['first_result'] = struct.unpack('<i',struct.pack('<I',row['observed']['first_result']))[0]
""" + anchor)
exec(compile(source, str(harness), 'exec'))
