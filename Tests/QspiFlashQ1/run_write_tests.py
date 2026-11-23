"""Run Q1.3 production code with the private write policy inside ARM emulation."""
from pathlib import Path

harness = Path(__file__).resolve().with_name('run_tests.py')
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", "'output/QspiFlashQ1/write_mock'")
source = source.replace('behavior_test.c', 'write_test.c').replace('Q1_TestRun', 'Write_TestRun')
names = ['public policy closed', 'out of capacity', 'size exceeds capacity',
         'non-erased byte on later page', 'BP protection', 'WEL not set',
         'program command error', 'transmit timeout', 'busy forever', 'readback mismatch',
         'second-page failure', 'abort also fails', 'CMP protection', 'WPS protection',
         'suspended', 'tick wrap across four fragments', 'last byte of device',
         'short transmit', 'WEL stays set after BUSY', 'WREN command error',
         'time exhausted during preflight', 'outside policy', 'shared deadline',
         'initial busy', 'initial WEL already set', 'single page', 'owner occupied',
         'masked or interrupt context']
start = source.index('names = [')
end = source.index('\nrows = ', start)
source = source[:start] + 'names = ' + repr(names) + source[end:]
exec(compile(source, str(harness), 'exec'))
