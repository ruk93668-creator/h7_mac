"""Execute the Q1.4 fault/recovery matrix with the established ARM harness."""
from pathlib import Path

harness = Path(__file__).resolve().with_name('run_tests.py')
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", "'output/QspiFlashQ1/q14/mock'")
source = source.replace('behavior_test.c','recovery_test.c').replace('Q1_TestRun','Q14_TestRun')
source = source.replace('count=2000000','count=25000000')
names = [
    'read command BUSY','read command ERROR','read command TIMEOUT','read receive BUSY',
    'read receive ERROR','read receive TIMEOUT','HAL_OK with error','short receive',
    'resource config changes during read','controller BUSY survives Abort','Abort fails',
    'DeInit fails','Flash BUSY then explicit retry','SUS set','WEL set','repeat on READY',
    'reentrant Recover','receive error during Recover','tick wrap during Recover','recovery deadline',
    'stalled Tick','released GPIO changed','foreign gate','prepare failure','Init HAL failure',
    'wrong JEDEC ID','deadline before WREN','WREN HAL_ERROR','program command BUSY',
    'program command ERROR','program command TIMEOUT','transmit BUSY','transmit ERROR',
    'transmit TIMEOUT','short transmit','program BUSY remains','verified idle mismatch',
    'second page failure confirmed first byte','post-transmit deadline','verification deadline',
    'Abort HAL_OK with error','DeInit HAL_OK still active','DMA drift retains ownership',
    'read fault recover recheck','erase BUSY remains'
]
start = source.index('names = [')
end = source.index('\nrows = ',start)
source = source[:start] + 'names = ' + repr(names) + source[end:]
exec(compile(source,str(harness),'exec'))
