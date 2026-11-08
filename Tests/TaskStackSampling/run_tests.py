"""Execute the actual stack-rate guards extracted from current source with ARMCC/Unicorn."""
from pathlib import Path
import re,json,subprocess,sys,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_R0
OUT=ROOT/'output/TaskStackSampling';OUT.mkdir(parents=True,exist_ok=True)
rows=[];commands=[]
for name in ['usb','camera','control','ipc','sd']:
    path=ROOT/(f'CM7/Application/Tasks/{name}_task.c' if name!='sd' else 'Tests/SdmmcStage08B/sd_block_probe.c')
    source=path.read_bytes().decode('gbk')
    if name!='sd':
        pattern=r'stack_now = Platform_TimeGetMs\(\);\s*if \(\(uint32_t\)\(stack_now - stack_tick\) >= (\w+)\)\s*\{\s*stack_tick = stack_now;\s*g_'+name+r'_task_debug.stack_free_words = uxTaskGetStackHighWaterMark\(NULL\);\s*\}'
        match=re.search(pattern,source);assert match,path
        macro=match[1];value=re.search(r'#define\s+'+macro+r'\s+(\d+)U',source)[1];assert value=='1000'
        body=match[0].replace(macro,value+'U');body=re.sub(r'g_\w+_task_debug.stack_free_words','g_free',body)
        body='static void step(void) { uint32_t stack_now; '+body+' }'
        timer='stack_tick'
    else:
        match=re.search(r'static void SdBlockTest_SampleStack\(void\)\s*\{.*?\n\}',source,re.S);assert match
        body=match[0].replace('SdBlockTest_SampleStack','step').replace('g_sd_block_test.stack_free_words','g_free');timer='s_stack_sample_tick'
        assert source.count('SdBlockTest_SampleStack();')==2
    for opt in ['O0','O2']:
        text='''typedef unsigned int uint32_t;
#define NULL ((void *)0)
static uint32_t mocked_now, scans, g_free;
static uint32_t TIMER;
static uint32_t Platform_TimeGetMs(void) { return mocked_now; }
static uint32_t uxTaskGetStackHighWaterMark(void *p) { (void)p; scans++; return 321U; }
BODY
uint32_t run(void) {
    uint32_t i;
    uint32_t times[]={1099U,1100U,1100U,1101U,2099U,2100U,9100U,9100U};
    uint32_t expected[]={0U,1U,1U,1U,1U,2U,3U,3U};
    TIMER=100U;
    for(i=0U;i<8U;i++) { mocked_now=times[i];step();if(scans!=expected[i])return i+1U; }
    TIMER=0xffffff00U;mocked_now=0x2e7U;step();if(scans!=3U)return 9U;
    mocked_now=0x2e8U;step();if(scans!=4U)return 10U;
    mocked_now=0x6d0U;step();if(scans!=5U || g_free!=321U)return 11U;
    return 0U;
}
'''.replace('TIMER',timer).replace('BODY',body)
        c=OUT/f'{name}_{opt}.c';o=c.with_suffix('.o');axf=c.with_suffix('.axf');c.write_text(text)
        for cmd in [['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-'+opt,'-c',str(c),'-o',str(o)],['D:/32/ARM/ARMCC/Bin/armlink.exe','--entry','run','--ro_base','0x08000000','--rw_base','0x20000000',str(o),'-o',str(axf)]]:
            p=subprocess.run(cmd,capture_output=True);log=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(command=subprocess.list2cmdline(cmd),exit_code=p.returncode,output=log));assert p.returncode==0,log
        with axf.open('rb') as f:
            elf=ELFFile(f);entry=next(x['st_value'] for x in elf.get_section_by_name('.symtab').iter_symbols() if x.name=='run')
            u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.mem_map(0x08000000,0x10000);u.mem_map(0x20000000,0x20000);u.mem_map(0x01000000,0x1000)
            for seg in elf.iter_segments():
                if seg['p_type']=='PT_LOAD' and seg['p_filesz']:u.mem_write(seg['p_vaddr'],seg.data())
        u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.emu_start(entry|1,0x01000000,count=200000)
        ok=u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
        rows.append(dict(module=name,optimization=opt,passed=ok,failed_check=u.reg_read(UC_ARM_REG_R0),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
report=dict(total=len(rows),passed=sum(x['passed'] for x in rows),cases=rows,coverage=['999/1000ms boundary','repeat timestamp','no catch-up after delay','unsigned tick wrap','result preserved','SD two call sites share one limiter'],hardware_fps_verified=False)
(HERE/'results.json').write_text(json.dumps(report,indent=2)+'\n');(HERE/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(report['total'],report['passed']);assert all(x['passed'] for x in rows),rows
