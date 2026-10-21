from pathlib import Path
import sys,json,re,struct
sys.path.insert(0,str(Path('Tests/OledAsyncAcceptance/python_deps').resolve()))
from elftools.elf.elffile import ELFFile
p=Path('Tests/DcmiInit');out={}
header=Path('Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h').read_text(encoding='utf-8')
with Path('MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf').open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
 def read(address,length):
  for sec in elf.iter_sections():
   if sec['sh_type']=='SHT_PROGBITS' and sec['sh_addr']<=address and address+length<=sec['sh_addr']+sec['sh_size']:
    offset=address-sec['sh_addr'];return sec.data()[offset:offset+length]
  raise RuntimeError(hex(address))
 for name,irqname in [('DMA1_Stream0_IRQHandler','DMA1_Stream0_IRQn'),('DCMI_IRQHandler','DCMI_IRQn')]:
  irq=int(re.search(r'\b'+irqname+r'\s*=\s*(\d+)',header).group(1));address=symbols[name]['st_value']
  vector=struct.unpack('<I',read(symbols['__Vectors']['st_value']+4*(16+irq),4))[0]
  assert symbols[name]['st_info']['bind']=='STB_GLOBAL' and vector&~1==address&~1
  out[name]={'irq':irq,'function':hex(address),'vector':hex(vector),'binding':'GLOBAL'}
 _,level,func=struct.unpack('<III',read(symbols['g_framework_init_BSP_DCMI_Init']['st_value'],12))
 assert level==1 and func&~1==symbols['BSP_DCMI_Init']['st_value']&~1
 out['initcall']={'level':level,'function':hex(func)}
 out['capture_start_linked']=[s for s in ['HAL_DCMI_Start_DMA','HAL_DMA_Start','HAL_DMA_Start_IT','g_sdram_check_state'] if s in symbols]
 assert not out['capture_start_linked']
 out['init_error_global']=hex(symbols['init_error']['st_value']);assert symbols['init_error']['st_info']['bind']=='STB_GLOBAL'
(p/'link_audit.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print('Final AXF audit PASS; IRQ vectors and BUS entry correct, init_error global, no capture starts.')
