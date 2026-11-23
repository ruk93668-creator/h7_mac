"""Compile audit for the isolated BLE port; does not claim executable or board validation."""
from pathlib import Path
import subprocess,json,concurrent.futures
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent;OUT=HERE/'build';OUT.mkdir(exist_ok=True)
N=ROOT/'Middlewares/Third_Party/NimBLE'
incs=['CM7/Core/Inc','CM7/BSP','CM7/Framework','Common/Inc','CM7/Platform','CM7/Platform/BLE','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include']
incs += [p.relative_to(ROOT).as_posix() for p in N.rglob('include')]
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','--gnu','-O2','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER']
for inc in incs:cc+=['-I',str(ROOT/inc)]
sources=[ROOT/'CM7/Platform/bt_hci_h5.c',*sorted((ROOT/'CM7/Platform/BLE').glob('*.c'))]
sources+=[p for p in (N/'nimble/host/src').glob('*.c') if p.name!='ble_gatts_lcl.c']+list((N/'nimble/host/util/src').glob('*.c'))+list((N/'nimble/transport/src').glob('*.c'))
sources+=[N/'porting/nimble/src'/f for f in ['endian.c','mem.c','nimble_port.c','os_mbuf.c','os_mempool.c','os_msys_init.c']]
for svc in ['gap','gatt']:sources+=list((N/f'nimble/host/services/{svc}/src').glob('*.c'))
def compile_one(item):
 i,source=item;cmd=cc.copy()
 if source.name not in ['bt_hci_h5.c','ble_heap.c']:cmd+=['--preinclude',str(ROOT/'CM7/Platform/BLE/ble_nimble_config.h')]
 cmd+=['-c',str(source),'-o',str(OUT/f'{i:03}_{source.stem}.o')]
 p=subprocess.run(cmd,cwd=ROOT,capture_output=True);return {'file':source.relative_to(ROOT).as_posix(),'exit':p.returncode,'output':(p.stdout+p.stderr).decode(errors='replace')}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(compile_one,enumerate(sources)))
(HERE/'compile_results.json').write_bytes((json.dumps({'linked':False,'hardware_verified':False,'files':rows},indent=2)+'\n').encode())
fail=[r for r in rows if r['exit']];print('Compile:',len(rows)-len(fail),'/',len(rows))
for row in fail[:8]:print(row['file'],row['output'][:1800])
raise SystemExit(bool(fail))
