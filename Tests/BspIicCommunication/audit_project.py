"""Verify formal source integrity and actual linked CM4 vector/callback binding."""
from pathlib import Path
import hashlib
import json
import re
import struct
from elftools.elf.elffile import ELFFile

here=Path(__file__).resolve().parent
root=here.parent.parent
baseline=json.loads((here/'source_before.json').read_text())
changed=[p for p,h in baseline.items() if not (root/p).exists() or hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
report={'hashed_formal_files':len(baseline),'changed_files':changed,'scope':'CM4/CM7/Common/Board/Drivers sources, MDK sources/scatters/uvprojx and h7_mac.ioc; generated build output excluded'}
definitions={}
names=['I2C2_EV_IRQHandler','I2C2_ER_IRQHandler','HAL_I2C_MasterTxCpltCallback','HAL_I2C_AbortCpltCallback','HAL_I2C_ErrorCallback']
for name in names:
    definitions[name]=[str(p.relative_to(root)) for p in (root/'CM4').rglob('*.c')
                       if re.search(rb'void\s+'+name.encode()+rb'\s*\([^;]*?\)\s*\{',p.read_bytes())]
    assert definitions[name]==[str(Path('CM4/BSP/bsp_iic.c'))],definitions[name]
report['cm4_strong_definitions']=definitions
startup=(root/'MDK-ARM/startup_stm32h747xx_CM4.s').read_text()
with (root/'MDK-ARM/h7_mac_CM4/h7_mac_CM4.axf').open('rb') as f:
    elf=ELFFile(f);symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    def read(address,size):
        for section in elf.iter_sections():
            base=section['sh_addr']
            if section['sh_type']=='SHT_PROGBITS' and base<=address and address+size<=base+section['sh_size']:
                return section.data()[address-base:address-base+size]
        raise ValueError(hex(address))
    base=symbols['__Vectors']['st_value'];vectors={}
    for name,irq in [('I2C2_EV_IRQHandler',33),('I2C2_ER_IRQHandler',34)]:
        assert 'DCD     '+name in startup
        vector=struct.unpack('<I',read(base+4*(16+irq),4))[0]
        symbol=symbols[name]
        assert vector==symbol['st_value']|1
        assert symbol['st_info']['bind']=='STB_GLOBAL'
        vectors[name]={'external_irq':irq,'vector_index':16+irq,'vector':hex(vector),'binding':symbol['st_info']['bind']}
    report['vectors']=vectors
    report['callbacks_in_formal_axf']={name:symbols[name]['st_info']['bind'] if name in symbols else 'not retained (unreferenced async path)' for name in names[2:]}
    report['async_apis_in_formal_axf']={name:name in symbols for name in ['BSP_IIC_WriteAsync','BSP_IIC_TakeResult']}
    assert symbols['hi2c2']['st_info']['bind']=='STB_LOCAL'
    report['hi2c2_binding']='STB_LOCAL'
    entry=symbols['g_framework_init_BSP_IIC_Init']['st_value']
    _,level,fn=struct.unpack('<III',read(entry,12))
    assert level==1 and fn==symbols['BSP_IIC_Init']['st_value']|1
    report['initcall']={'level':level,'address':hex(entry),'function':hex(fn)}
with (root/'MDK-ARM/h7_mac_CM4/bsp_iic.o').open('rb') as f:
    obj=ELFFile(f);symbols={s.name:s for s in obj.get_section_by_name('.symtab').iter_symbols()}
    report['callbacks_in_bsp_object']={name:symbols[name]['st_info']['bind'] for name in names[2:]}
    assert all(b=='STB_GLOBAL' for b in report['callbacks_in_bsp_object'].values())
assert not changed,changed
report['result']='PASS'
(here/'project_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
