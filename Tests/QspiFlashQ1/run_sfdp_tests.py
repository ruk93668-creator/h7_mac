"""Run the diagnostic BSP/probe in the existing ARMCC5 and Unicorn harness."""
from pathlib import Path

here = Path(__file__).resolve().parent
harness = here / 'run_tests.py'
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", "'output/QspiFlashQ1/sfdp_mock'")
source = source.replace('behavior_test.c', 'sfdp_test.c').replace('Q1_TestRun', 'Sfdp_TestRun')
start = source.index('names = [')
end = source.index('\nrows = ', start)
names = ['SFDP and equal nonuniform data', 'all FF and invalid SFDP signature',
         'command timeout preserves output', 'short receive preserves output',
         'single-line data mismatch', 'invalid pointer and clock rejected']
source = source[:start] + 'names = ' + repr(names) + source[end:]
exec(compile(source, str(harness), 'exec'))
