"""Check actual linked profiles: optional CPU buffer and isolated AXI IDMA memory."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,sys
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
rows=[]
modes=['current'] if '--current' in sys.argv else ['normal','read_only','write_compile_only','read_stress']
for mode in modes:
    path=ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf' if mode=='current' else ROOT/'output/SdmmcStage08B/firmware'/mode/'h7_mac_CM7.axf'
    with path.open('rb') as f:
        elf=ELFFile(f)
        symbols={x.name:(x['st_value'],x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()}
        dma=symbols.get('g_sdmmc_dma_buffer')
        stress=symbols.get('s_stress_buffer')
        end=max((x['sh_addr']+x['sh_size'] for x in elf.iter_sections() if 0x20000000<=x['sh_addr']<0x20020000),default=0)
        checks=dict(axi_window=dma==(0x24000000,32768),dtcm_boundary=end<=0x20020000,stress_presence=(stress is not None)==(mode in ['read_stress','current']),single_existing_task=('s_test_stack' in symbols)==(mode!='normal'))
        if stress:
            checks['cpu_buffer_boundary']=stress[1]==33288 and stress[0]==0x24010000 and stress[0]%32==0 and stress[0]+stress[1]<=0x24020000
        row=dict(profile=mode,checks=checks,passed=all(checks.values()),dtcm_end=hex(end),symbols={k:dict(address=hex(v[0]),size=v[1]) for k,v in symbols.items() if k in ['s_stress_buffer','s_test_stack','g_sdmmc_dma_buffer','g_sd_block_test']})
        rows.append(row)
report=dict(summary=dict(total=sum(len(x['checks']) for x in rows),passed=sum(sum(x['checks'].values()) for x in rows)),profiles=rows)
(HERE/('stress_axi_current_audit.json' if '--current' in sys.argv else 'stress_memory_audit.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
print(report['summary'])
raise SystemExit(not all(x['passed'] for x in rows))
