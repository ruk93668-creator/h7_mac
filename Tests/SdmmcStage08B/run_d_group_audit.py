"""Verify D-group task suppression flag and retained C-group code/data in actual AXFs."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,struct
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def inspect(path):
    with path.open('rb') as f:
        elf=ELFFile(f)
        symbols={x.name:dict(address=x['st_value'],size=x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()}
        address=symbols['g_sd_block_d_group']['address']
        value=None
        for section in elf.iter_sections():
            if section['sh_type']=='SHT_PROGBITS' and section['sh_addr']<=address and address+4<=section['sh_addr']+section['sh_size']:
                value=struct.unpack_from('<I',section.data(),address-section['sh_addr'])[0]
                break
        names=[n for n in symbols if n.startswith('SdBlockTest_') or n in ['s_test_tcb','s_test_stack','s_test_data','s_stress_buffer','g_sd_block_d_group']]
        return dict(flag=value,symbols={n:symbols[n] for n in names})

current=inspect(ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf')
# 正式AXF启用了RW压缩，不能把压缩区文件字节当作运行期初值。
# 使用该次链接的真实目标文件读取初值，并在正式AXF核对保留符号。
object_path=ROOT/'MDK-ARM/h7_mac_CM7/sd_block_probe.o'
with object_path.open('rb') as f:
    elf=ELFFile(f)
    symbol=next(x for x in elf.get_section_by_name('.symtab').iter_symbols() if x.name=='g_sd_block_d_group')
    section=elf.get_section(symbol['st_shndx'])
    object_flag=struct.unpack_from('<I',section.data(),symbol['st_value']-section['sh_addr'])[0]
current.pop('flag')
current['flag_from_linked_object']=object_flag
current['flag_source']='MDK-ARM/h7_mac_CM7/sd_block_probe.o: .data'

required=['SdBlockTest_Task','SdBlockTest_RunRequest','SdBlockTest_Stress','SdBlockTest_StressFinish','SdBlockTest_Read','SdBlockTest_Crc','s_test_tcb','s_test_stack','s_test_data','s_stress_buffer']
checks=dict(d_group_flag_in_compiled_object=object_flag==1 and 'g_sd_block_d_group' in current['symbols'],all_pressure_symbols=all(x in current['symbols'] for x in required),axi_buffer=current['symbols']['s_stress_buffer']==dict(address=0x24010000,size=33288),test_stack_retained=current['symbols']['s_test_stack']['size']==4096)
comparisons=[]
for opt in ['O0','O2']:
    c=inspect(ROOT/'output/SdmmcStage08B'/('dgroup0_'+opt+'.axf'))
    d=inspect(ROOT/'output/SdmmcStage08B'/('dgroup1_'+opt+'.axf'))
    common=[n for n in c['symbols'] if n.startswith('SdBlockTest_')]
    same_functions=all(n in d['symbols'] and c['symbols'][n]['size']==d['symbols'][n]['size'] for n in common)
    resources=['s_test_tcb','s_test_stack','s_test_data','s_stress_buffer']
    same_resources=all(c['symbols'][n]==d['symbols'][n] for n in resources)
    checks[opt+'_retained_functions']=same_functions
    checks[opt+'_retained_resources']=same_resources
    comparisons.append(dict(optimization=opt,c=c,d=d))
report=dict(summary=dict(total=len(checks),passed=sum(checks.values())),checks=checks,current=current,comparisons=comparisons,hardware_usb_fps_verified=False)
(HERE/'d_group_link_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(report['summary'])
raise SystemExit(not all(checks.values()))
