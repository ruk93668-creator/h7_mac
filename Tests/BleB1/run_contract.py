"""ARMCC5 production driver execution with a minimal MMIO model; not board validation."""
from pathlib import Path
import sys, subprocess, json, hashlib,struct
HERE=Path(__file__).resolve().parent; ROOT=HERE.parents[1]
sys.path.insert(0,str(HERE/'python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(cmd):
    cmd=list(map(str,cmd)); p=subprocess.run(cmd,cwd=ROOT,capture_output=True)
    commands.append({'command':subprocess.list2cmdline(cmd),'exit':p.returncode,'output':(p.stdout+p.stderr).decode(errors='replace')})
    (HERE/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
    if p.returncode: raise RuntimeError(commands[-1]['output'])
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O2','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER']
for inc in ['CM7/Core/Inc','CM7/BSP','CM7/Framework','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include']: cc+=['-I',ROOT/inc]
objs=[]
for src in [HERE/'uart_contract.c',ROOT/'CM7/BSP/bsp_bt_uart.c',ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c']:
    obj=HERE/(src.stem+'.o'); run(cc+['-c',src,'-o',obj]); objs.append(obj)
axf=HERE/'uart_contract.axf'
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry','B1_TestRun','--scatter',ROOT/'Tests/UsbDeviceStage1/test.sct','-o',axf])
with axf.open('rb') as f:
    elf=ELFFile(f); diag_addr=next(sym['st_value'] for sym in elf.get_section_by_name('.symtab').iter_symbols() if sym.name=='d');entry=elf.header['e_entry'];sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
UART=0x40004400; ISR=UART+0x1c; RDR=UART+0x24; TDR=UART+0x28
names=['init_8e1_gpio','rx_full_and_wrap','tx_full_and_wire_completion','overflow_and_recover','ore','fe','pe','ne','combined_errors','deinit_gpio_restore','ack_timeout_stalled_tick','clock_reject','invalid_and_zero_args','reject_foreign_owner','recover_timeout','masked_context_reject','bidirectional_8192','partial_write_and_abort']
rows=[]
for case,name in enumerate(names):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x40000000,0x100000),(0x58000000,0x40000),(0xE000E000,0x2000)]: u.mem_map(a,n)
    for a,d in sections: u.mem_write(a,d)
    state={'enable':0,'ue_count':0,'rx':0};tx=[]
    def rd32(a): return struct.unpack('<I',u.mem_read(a,4))[0]
    def wr32(a,v): u.mem_write(a,struct.pack('<I',v))
    def read_hook(uc,access,address,size,value,user):
        if address==RDR: wr32(ISR,rd32(ISR)&~32);state['rx']+=1
        if address==0xe000e104: wr32(address,state['enable'])
    def write_hook(uc,access,address,size,value,user):
        if address==UART:
            old=rd32(UART)
            if value&1 and not old&1: state['ue_count']+=1
            ack=not(case==10 or case==14 and state['ue_count']>1)
            flags=rd32(ISR)&~((1<<21)|(1<<22))
            if value&1 and ack:
                flags|=(1<<21)|(1<<22)
                if not old&1: flags|=128|64
            wr32(ISR,flags)
        elif address==UART+0x20: wr32(ISR,rd32(ISR)&~(value&15))
        elif address==UART+0x18 and value&8: wr32(ISR,rd32(ISR)&~32)
        elif address==TDR: tx.append(value&255);wr32(ISR,rd32(ISR)&~64)
        elif address==0xe000e104: state['enable']|=value
        elif address==0xe000e184: state['enable']&=~value
    u.hook_add(UC_HOOK_MEM_READ,read_hook);u.hook_add(UC_HOOK_MEM_WRITE,write_hook)
    u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
    error=None
    try: u.emu_start(entry|1,0x01000000,count=30000000)
    except Exception as e: error=str(e)
    line=u.reg_read(UC_ARM_REG_R0);ok=not error and line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
    expected=bytes(range(64))*16 if case==2 else bytes(range(256))*32 if case==16 else b''
    wire_ok=bytes(tx)==expected;ok=ok and wire_ok
    rows.append({'id':case,'name':name,'pass':ok,'failure_line':line,'diagnostic_words':list(struct.unpack('<23I',u.mem_read(diag_addr,92))),'error':error,'tx_bytes':len(tx),'tx_sequence_match':wire_ok,'tx_sha256':hashlib.sha256(bytes(tx)).hexdigest()})
    print(name,'PASS' if ok else 'FAIL',line,error,flush=True)
result={'driver_sha256':hashlib.sha256((ROOT/'CM7/BSP/bsp_bt_uart.c').read_bytes()).hexdigest(),'hardware_verified':False,'model_limits':'No timing, real UART serialization/parity, analog signals, automatic exception scheduling, dual-core or NVIC nesting validation. TXE immediate; explicit software IRQ invocation; ACK and W1C modeled. HAL PCLK1 query stub=120MHz with RCC mux selection modeled.','summary':{'total':len(rows),'passed':sum(r['pass'] for r in rows)},'cases':rows}
(HERE/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
raise SystemExit(not all(r['pass'] for r in rows))
