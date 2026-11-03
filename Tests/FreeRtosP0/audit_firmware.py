"""Re-runnable local firmware/ownership audit; no historical HEAD or absolute worktree dependency."""
from pathlib import Path
import sys, struct, json, hashlib, re, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
rows=[]
def check(name, condition, actual=None):
    rows.append({'name':name,'pass':bool(condition),'actual':actual})
    if not condition: raise AssertionError((name,actual))
image=ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
with image.open('rb') as f:
    elf=ELFFile(f)
    symbols={s.name:(s['st_value'],s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
    segments=[(p['p_vaddr'],p.data()) for p in elf.iter_segments() if p['p_type']=='PT_LOAD']
def read(addr,count):
    for start,data in segments:
        if start<=addr and addr+count<=start+len(data):return data[addr-start:addr-start+count]
    raise AssertionError(hex(addr))
vector=symbols['__Vectors'][0]
irq_header=(ROOT/'Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h747xx.h').read_text()
tim6_irq=int(re.search(r'TIM6_DAC_IRQn\s*=\s*(\d+)',irq_header).group(1))
for index,name in [(11,'SVC_Handler'),(14,'PendSV_Handler'),(15,'SysTick_Handler'),(16+tim6_irq,'TIM6_DAC_IRQHandler')]:
    target=struct.unpack('<I',read(vector+index*4,4))[0]
    check(name+' vector',target==symbols[name][0]|1,hex(target))
check('TIM6 is HAL Tick, no HAL SysTick call','HAL_IncTick();' not in (ROOT/'CM7/Core/Src/stm32h7xx_it.c').read_text(encoding='gbk'))
check('MSP stack is 4KiB',symbols['Stack_Mem'][1]==4096,symbols['Stack_Mem'])
check('Legacy stack is 8KiB',symbols['s_legacy_stack'][1]==8192,symbols['s_legacy_stack'])
check('Idle stack is 1KiB',symbols['s_idle_stack'][1]==1024,symbols['s_idle_stack'])
for name in ['s_legacy_stack','s_idle_stack','s_legacy_tcb','s_idle_tcb']:
    address,size=symbols[name]
    check(name+' DTCM range',0x20000000<=address and address+size<=0x20020000,[hex(address),size])
initial_sp=struct.unpack('<I',read(vector,4))[0]
check('initial MSP is aligned and inside DTCM',initial_sp%8==0 and 0x20000000<initial_sp<=0x20020000,hex(initial_sp))
main=(ROOT/'CM7/Core/Src/main.c').read_text(encoding='gbk')
body=main.split('static void Main_Run(void)',1)[1].split('}',1)[0]
calls=re.findall(r'\b(Framework_Run|USB_Mode_Process|USB_UVC_Process|App_Run)\(\)',body)
check('Main_Run original call order',calls==['Framework_Run','USB_Mode_Process','USB_UVC_Process','App_Run'],calls)
check('no kernel heap symbols',not any(x in symbols for x in ['pvPortMalloc','ucHeap','vPortFree']))
manifest=json.loads((ROOT/'Middlewares/Third_Party/FreeRTOS/upstream.json').read_text())
for name,expected in manifest['files'].items():
    check('official '+name,hashlib.sha256((ROOT/'Middlewares/Third_Party/FreeRTOS/Source'/name).read_bytes()).hexdigest()==expected)
project=ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').getroot()
for target in project.findall('Targets/Target'):
    group=next(g for g in target.findall('Groups/Group') if g.findtext('GroupName')=='CM7 FreeRTOS P0')
    if target.findtext('TargetName')=='h7_mac_CM4':
        check('CM4 excludes CM7 kernel group',group.findtext('GroupOption/CommonProperty/IncludeInBuild')=='0')
config=(ROOT/'CM7/Application/video_config.c').read_text(encoding='gbk')
check('UVC 25 FPS cold config restored','.output = VIDEO_OUTPUT_USB_UVC' in config and '.output_fps = 25U' in config)
result={'summary':{'total':len(rows),'passed':len(rows),'failed':0},'firmware_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),'checks':rows,'hardware_verified':False}
(HERE/'firmware_audit.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(result['summary'])
