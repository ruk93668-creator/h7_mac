"""Fault-inject MMIO around the real HAL/LL. No silicon or new production implementation is claimed."""
from pathlib import Path
import hashlib, json, struct, subprocess, sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "output/UDLM-P1/python_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
OUT = ROOT / "output/MSC24/udlm-p1/hal-contract"
REPORT = ROOT / "Tests/EndpointLifecycle/reports"
OUT.mkdir(parents=True, exist_ok=True)
REPORT.mkdir(parents=True, exist_ok=True)
CC = Path("D:/32/ARM/ARMCC/Bin")
args = [str(CC/"armcc.exe"), "--cpu", "Cortex-M7", "--c99", "--split_sections", "-O0", "-DSTM32H747xx",
        "-DCORE_CM7", "-DUSE_HAL_DRIVER", "-DHAL_USB_TIMEOUT=4U", "--preinclude",
        str(ROOT/"CM7/BSP/bsp_usb_hal_config.h")]
for inc in ["CM7/Core/Inc", "CM7/BSP", "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
            "Drivers/CMSIS/Device/ST/STM32H7xx/Include"]:
    args += ["-I", str(ROOT/inc)]
files = ["Tests/EndpointLifecycle/hal_contract_test.c",
         "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c",
         "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c"]
commands = []
def run(argv):
    r = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True)
    log = (r.stdout+r.stderr).decode("utf-8", errors="replace")
    commands.append(dict(argv=list(map(str, argv)), exit=r.returncode, output=log))
    if r.returncode or "warning:" in log.lower():
        raise RuntimeError(log)
objects = []
for source in files:
    obj = OUT/(Path(source).stem+".o")
    run(args+["-c", ROOT/source, "-o", obj]); objects.append(obj)
image = OUT/"hal_contract.axf"
run([CC/"armlink.exe", *objects, "--entry", "EndpointContract_Run", "--no_scanlib", "--scatter",
     ROOT/"Tests/EndpointLifecycle/test.sct", "-o", image])
with image.open("rb") as f:
    elf=ELFFile(f)
    symbols={s.name:s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections=[(s["sh_addr"],s.data()) for s in elf.iter_sections()
              if s["sh_flags"]&2 and s["sh_type"]=="SHT_PROGBITS" and s["sh_size"]]
names=["HAL Tx Flush hides AHB-idle timeout", "HAL Rx Flush hides AHB-idle timeout",
       "LL Tx Flush returns true timeout", "LL Rx Flush returns true timeout",
       "HAL Stop hides Tx Flush timeout", "Active IN2 Abort times out as HAL_ERROR",
       "Active OUT2 Abort times out as HAL_ERROR", "IN2 EPENA zero without EPDISD is HAL_OK",
       "OUT2 EPENA zero without EPDISD is HAL_OK", "IN2 EPENA clears without EPDISD",
       "OUT2 EPENA clears without EPDISD", "HAL Flush lock contention is HAL_BUSY",
       "HAL Stop lock contention is HAL_BUSY", "HAL Abort ignores HAL lock",
       "HAL Tx Flush hides stuck flush-command timeout", "LL Tx Flush normal completion",
       "LL Rx Flush normal completion", "HAL Abort leaves preexisting XFRC/EPDISD unretired"]
rows=[]
BASE=0x40080000; GRST=BASE+0x10; INCTL=BASE+0x900+2*0x20; OUTCTL=BASE+0xb00+2*0x20
for case,name in enumerate(names):
    cpu=Uc(UC_ARCH_ARM, UC_MODE_THUMB|UC_MODE_MCLASS)
    for address,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),
                         (BASE,0x40000),(0xe0000000,0x100000)]:cpu.mem_map(address,size)
    for address,data in sections:cpu.mem_write(address,data)
    def write32(a,v):cpu.mem_write(a,struct.pack("<I",v))
    def read32(a):return struct.unpack("<I",cpu.mem_read(a,4))[0]
    if case in (5,9):write32(INCTL,1<<31)
    if case in (6,10):write32(OUTCTL,1<<31)
    if case==17:
        write32(INCTL+8,3);write32(OUTCTL+8,3)
    reads={"in":0,"out":0,"grst":0}
    def mmio(uc,access,address,size,value,context):
        if address==GRST:
            reads["grst"]+=1
            if case in (14,15,16):
                v=read32(GRST)|(1<<31)
                if case in (15,16):v &= ~((1<<4)|(1<<5))
                write32(GRST,v)
        elif address in (INCTL,OUTCTL):
            key="in" if address==INCTL else "out";reads[key]+=1
            if case in (9,10) and reads[key]>=3:write32(address,read32(address)&~(1<<31))
    cpu.hook_add(UC_HOOK_MEM_READ,mmio,begin=BASE,end=BASE+0x3ffff)
    cpu.reg_write(UC_ARM_REG_SP,0x2003fff0);cpu.reg_write(UC_ARM_REG_LR,0x01000001)
    cpu.reg_write(UC_ARM_REG_R0,case)
    error=None
    try:cpu.emu_start(symbols["EndpointContract_Run"]|1,0x01000000,count=3000000,timeout=10000000)
    except Exception as e:error=str(e)
    values=list(struct.unpack("<11I",cpu.mem_read(symbols["g_endpoint_contract"],44)))
    expected_codes=[0,0,3,3,0,1,1,0,0,0,0,2,2,0,0,0,0,0]
    checks=[]
    def check(name, actual, expected):
        checks.append(dict(name=name,actual=actual,expected=expected,passed=actual==expected))
    check("returned to sentinel",cpu.reg_read(UC_ARM_REG_PC),0x01000000)
    check("entry result",cpu.reg_read(UC_ARM_REG_R0),0)
    check("case identity",values[0],case)
    check("public status",values[1],expected_codes[case])
    check("no unrelated stub called",values[10],0)
    if case in (5,6):
        check("EPENA still set",bool(values[4 if case==5 else 5] & (1<<31)),True)
        check("bounded loop executed",reads["in" if case==5 else "out"]>=10000,True)
    if case in (7,8,9,10):
        check("EPENA clear",bool(values[4 if case in (7,9) else 5] & (1<<31)),False)
        check("EPDISD absent",bool(values[6 if case in (7,9) else 7] & 2),False)
    if case in (4,12):check("global IRQ enable",bool(values[8]&1),case==12)
    if case in (15,16):check("FIFO flush command clear",values[3]&0x30,0)
    if case==17:
        check("old IN events remain",values[6]&3,3)
        check("old OUT events remain",values[7]&3,3)
    passed=error is None and all(x["passed"] for x in checks)
    # The deliberately bad HAL outcomes are characterization findings, not safe cancellation passes.
    rows.append(dict(id=case,name=name,passed=passed,assertions=checks,assertion_count=len(checks),
                     input_state=dict(endpoint_type="BULK",max_packet=64,in_epena=case in (5,9),out_epena=case in (6,10),
                                      existing_xfrc_epdisd=case==17,pcd_locked=case in (11,12,13)),
                     injection="MMIO model only: AHBIDL low / stuck flush / stuck EPENA / EPENA clear without EPDISD as named",
                     expectation_kind="characterize existing defects; not acceptance of a safe stop",
                     register_reads=reads,actual=values[1],expected=expected_codes[case],
                     registers=dict(zip(["GRSTCTL","IN2_CTL","OUT2_CTL","IN2_INT","OUT2_INT","GAHBCFG","DCTL"],values[3:10])),
                     error=error,resource_refs="NOT_MODELED: no endpoint ledger exists in HAL",silicon="NOT_TESTED"))
    print(case,"PASS" if passed else "FAIL",name,values[:3],flush=True)
report=dict(scope="Unchanged HAL/LL characterization, not P1 implementation acceptance",cases=rows,
            summary=dict(total=len(rows),passed=sum(x["passed"] for x in rows),failed=sum(not x["passed"] for x in rows),assertions=sum(x["assertion_count"] for x in rows)),
            test_only_macro="HAL_USB_TIMEOUT=4U; production timeout unchanged; no physical timing claim",
            sources={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},commands=commands)
(REPORT/"hal_contract.json").write_bytes(json.dumps(report,ensure_ascii=False,indent=2).encode("utf-8"))
assert all(x["passed"] for x in rows)
