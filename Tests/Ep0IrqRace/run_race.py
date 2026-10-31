"""ARMCC5 + Unicorn: real Mode/BSP/Platform/Core, mocked HAL hardware."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "Tests/OledAsyncAcceptance/python_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

files = ["CM7/Service/usb_mode.c", "CM7/Service/usb_mode.h",
         "CM7/Platform/usb_device.c", "CM7/BSP/bsp_usb.c",
         "CM7/Platform/usb_device.h", "CM7/BSP/bsp_usb.h", "CM7/Service/usb_test_device.c"]
before = {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files}
commands = []

def run(args):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True)
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    commands.append({"command": subprocess.list2cmdline(list(map(str, args))),
                     "returncode": result.returncode, "output": output})
    (HERE / "commands.json").write_text(json.dumps(commands, ensure_ascii=False, indent=2), encoding="utf-8")
    if result.returncode:
        raise RuntimeError(output)

run(["D:/32/ARM/ARMCC/Bin/armcc.exe", "--vsn"])
args = ["D:/32/ARM/ARMCC/Bin/armcc.exe", "--cpu", "Cortex-M7", "--c99", "-O0",
        "-DSTM32H747xx", "-DCORE_CM7", "-DUSE_HAL_DRIVER", "--preinclude",
        ROOT / "CM7/BSP/bsp_usb_hal_config.h"]
for inc in ["CM7/Core/Inc", "CM7/BSP", "CM7/Platform", "CM7/Service", "CM7/Framework", "Common/Inc",
            "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
            "Drivers/CMSIS/Device/ST/STM32H7xx/Include",
            "Middlewares/ST/STM32_USB_Device_Library/Core/Inc"]:
    args += ["-I", ROOT / inc]
objects = []
for source in [HERE / "race_test.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
    obj = HERE / (source.stem + ".o")
    run(args + ["-c", source, "-o", obj])
    objects.append(obj)

import re
pcd=ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c'
exports=re.findall(r'(?m)^(?:__weak\s+)?[\w *]+\b(HAL_PCD_\w+)\([^;{}]*\)\s*\{',pcd.read_text())
renames=['-D'+name+'=Race_'+name for name in sorted(set(exports)) if not name.endswith('Callback') and name not in ['HAL_PCD_MspInit','HAL_PCD_MspDeInit']]
obj=HERE/'real_pcd.o';run(args+renames+['-c',pcd,'-o',obj]);objects.append(obj)
obj=HERE/'real_ll.o';run(args+['-c',ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c','-o',obj]);objects.append(obj)
run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "Ep0Race_Run", "--scatter",
     ROOT / "Tests/UsbDeviceStage1/test.sct", "--keep=*(.framework_init)", "--no_scanlib",
     "-o", HERE / "mode_test.axf"])
with (HERE / "mode_test.axf").open("rb") as f:
    elf = ELFFile(f)
    entry = elf.header["e_entry"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]


from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R1
base=0x40080000
reg_in=[base+0x908+i*0x20 for i in range(9)]
reg_out=[base+0xB08+i*0x20 for i in range(9)]
rows=[]
for case in range(4):
    uc=Uc(UC_ARCH_ARM, UC_MODE_THUMB|UC_MODE_MCLASS)
    for address,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x20000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(base,0x40000)]:uc.mem_map(address,size)
    for address,data in sections:uc.mem_write(address,data)
    rd=lambda a:struct.unpack('<I',uc.mem_read(a,4))[0]
    wr=lambda a,v:uc.mem_write(a,struct.pack('<I',v&0xFFFFFFFF))
    pending={};returns=[];events=[];model={'late_fifo_flush':0,'new_in':0,'rx_fifo_flush':0}
    functions={symbols[n]&~1:n for n in ['HAL_PCD_EP_Open','HAL_PCD_EP_Close','HAL_PCD_EP_Abort','HAL_PCD_EP_Transmit','HAL_PCD_EP_Receive']}
    def summarize():
        ins=0;outs=0
        for i in range(9):
            if rd(reg_in[i])&rd(base+0x810):ins|=1<<i
            if rd(reg_out[i])&rd(base+0x814):outs|=1<<i
        wr(base+0x818,ins|(outs<<16))
        flags=rd(base+0x14)&~((1<<18)|(1<<19))
        dmask=rd(base+0x81C)
        wr(base+0x14,flags|((1<<18) if (ins&dmask&0xFFFF) else 0)|((1<<19) if (outs&(dmask>>16)) else 0))
    def code(uc,pc,size,user):
        for address,value in list(pending.items()):wr(address,value)
        pending.clear()
        if returns and pc==returns[-1][0]:
            _,name,ep=returns.pop()
            if uc.reg_read(UC_ARM_REG_R0)==0:
                index=ep&15;bit=1<<(index+(0 if ep&0x80 else 16))
                if name=='HAL_PCD_EP_Open':wr(base+0x81C,rd(base+0x81C)|bit)
                if name=='HAL_PCD_EP_Close':wr(base+0x81C,rd(base+0x81C)&~bit)
                if name=='HAL_PCD_EP_Abort':
                    ctl=base+(0x900 if ep&0x80 else 0xB00)+index*0x20
                    if rd(ctl)&(1<<31):
                        wr(ctl,rd(ctl)&~(1<<31))
                        flag=(reg_in if ep&0x80 else reg_out)[index];wr(flag,rd(flag)|2)
                if name=='HAL_PCD_EP_Receive':
                    ctl=base+0xB00+index*0x20;wr(ctl,rd(ctl)|(1<<31))
                if name=='HAL_PCD_EP_Transmit' and ep==0x80 and rd(symbols['g_race_phase'])==1:
                    model['new_in']+=1;wr(base+0x834,rd(base+0x834)|1)
            events.append([name,ep,uc.reg_read(UC_ARM_REG_R0),rd(symbols['g_race_phase'])])
        if pc in functions:returns.append((uc.reg_read(UC_ARM_REG_LR)&~1,functions[pc],uc.reg_read(UC_ARM_REG_R1)))
        if pc==(symbols['USB_FlushTxFifo']&~1) and model['new_in']:
            model['late_fifo_flush']+=1
        if pc==(symbols['USB_FlushRxFifo']&~1) and rd(symbols['g_race_phase'])==1:model['rx_fifo_flush']+=1
        if rd(symbols['g_race_phase']) in [1,2]:summarize()
    def write(uc,access,address,size,value,user):
        if rd(symbols['g_race_phase']) not in [1,2]:return
        if address in reg_in+reg_out:
            pending[address]=rd(address)&~value;events.append(['W1C',hex(address),value])
        if address==base+0x14:pending[address]=rd(address)&~value
        if address==base+0x10:pending[address]=(value&~0x31)|0x80000000
    uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_MEM_WRITE,write)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0);uc.reg_write(UC_ARM_REG_LR,0x01000001);uc.reg_write(UC_ARM_REG_R0,case)
    error=None
    try:uc.emu_start(entry|1,0x01000000,count=10000000,timeout=20000000)
    except Exception as exc:error=str(exc)
    values=struct.unpack('<6I',uc.mem_read(symbols['g_result'],24));obs=list(struct.unpack('<16I',uc.mem_read(symbols['g_race'],64)))
    ok=not error and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and values[0]==0
    ordering=True
    if case!=2:
        ack_addr=hex(reg_out[0] if case==3 else reg_in[0]);ack_bits=3 if case==3 else 11
        ack=next((i for i,e in enumerate(events) if e[:3]==['W1C',ack_addr,ack_bits]),-1)
        opened=next((i for i,e in enumerate(events) if e[:2]==['HAL_PCD_EP_Open',0 if case==3 else 128] and len(e)>3 and e[3]==1),-1)
        started=next((i for i,e in enumerate(events) if e[:2]==['HAL_PCD_EP_Transmit',128] and len(e)>3 and e[3]==1),-1)
        ordering=0<=ack<opened<started
    ok=ok and model['late_fifo_flush']==0 and model['rx_fifo_flush']==0 and ordering and (model['new_in']==0 if case==2 else model['new_in']==1)
    row={'ordering_pass':ordering,'scenario':['A_old_XFRC_pending','B_no_old_XFRC','C_abort_failure','D_cancel_STATUS_OUT'][case],'pass':ok,'error':error,'failed_line':values[0],'actual':values[2],'expected':values[3],'assertions':values[1],'observation':obs,'model':model,'events':events}
    rows.append(row);print(row['scenario'],ok,'line',values[0],'actual/expected',values[2:4],'obs',obs[:12],model)
(HERE/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(not all(x['pass'] for x in rows))
