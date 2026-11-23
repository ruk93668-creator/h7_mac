"""Q1.2 ARMCC5/Unicorn matrix; reuse the unchanged Q1.1 execution harness."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
source = (Path(__file__).parent / 'run_tests.py').read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", "'output/QspiFlashQ1/q12/mock'")
source = source.replace('behavior_test.c', 'read_test.c').replace('Q1_TestRun', 'Q12_TestRun')
start = source.index('names = [')
end = source.index('\nrows = []', start)
names = [f'pattern={i//70} address-index={(i//10)%7} length-index={i%10}' for i in range(420)]
names += [f'invalid parameter {i}' for i in range(20)]
names += [f'fault={i%12+1} command={i//12+1}' for i in range(48)]
names += ['QE zero', 'SR1 busy', 'suspend', '03 rejected at120MHz', 'ISR', 'PRIMASK',
          'BASEPRI', 'FAULTMASK', 'TIM stopped', 'TIM IRQ disabled', 'NVIC disabled',
          'lock occupied', 'not ready', 'fault latched', 'nested six APIs', 'tick wrap',
          'second chunk timeout', 'second chunk short', 'abort failure', 'tick context lost',
          'unexpected DMA', 'shared shrinking deadline', 'memory mapped rejected',
          'DCR corrupt', '03 safe simulated48MHz', 'raw status decoding',
          'status output atomic on failure', 'HCLK drift']
source = source[:start] + 'names = ' + repr(names) + source[end:]
source = source.replace('count=2000000', 'count=10000000')
source = source.replace('from unicorn import Uc,', 'from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, Uc,')
hook = '''    def invalid_mmio(uc, kind, address, size, val, user):
        if address >= 0x40000000 and value('g_guard_invalid'):
            uc.mem_write(syms['g_invalid_mmio'], struct.pack('<I', value('g_invalid_mmio') + 1))
    u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, invalid_mmio, begin=0x40000000, end=0xFFFFFFFF)
'''
#value() 在 emu_start 前即定义；只监控外设地址，跳过普通 SRAM/Flash。
source = source.replace('    error = None\n', '''    def value(symbol):
        return struct.unpack('<I', u.mem_read(syms[symbol], 4))[0]
''' + hook + '    error = None\n')
exec(compile(source, str(Path(__file__).parent / 'run_tests.py'), 'exec'))
