"""Execute the H2 state machine and production BSP under the existing ARM mock."""
from pathlib import Path

harness = Path(__file__).resolve().parents[1] / 'run_tests.py'
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/mock'", "'output/QspiFlashQ1/h2/mock'")
source = source.replace('behavior_test.c', 'h2/h2_mock.c').replace('Q1_TestRun', 'H2_TestRun')
source = source.replace("'-DCORE_CM7',", "'-DCORE_CM7', '-DQSPI_H2_TEST',")
source = source.replace('count=2000000', 'count=25000000')
names = ['both gates, injected receive, successful Recover', 'real receive error',
         'short receive', 'Recover JEDEC mismatch', 'Recover Tick stuck', 'DeInit failure',
         'post-Recover Quad mismatch', 'deny all prohibited Flash commands']
start = source.index('names = [')
end = source.index('\nrows = ', start)
source = source[:start] + 'names = ' + repr(names) + source[end:]
anchor = '    rows.append(row)'
source = source.replace(anchor, """    if row['pass'] and case in (0, 4):
        address = syms['g_qspi_h2_report']
        size = struct.unpack('<I', u.mem_read(address + 8, 4))[0]
        (OUT / f'synthetic_report_{case}.bin').write_bytes(bytes(u.mem_read(address, size)))
        if case == 0:
            for name in ('before', 'after'):
                data = u.mem_read(syms['g_qspi_h2_' + name], 8192)
                (OUT / f'synthetic_{name}.bin').write_bytes(bytes(data))
""" + anchor)
exec(compile(source, str(harness), 'exec'), {'__file__': str(harness), '__name__': '__main__'})
