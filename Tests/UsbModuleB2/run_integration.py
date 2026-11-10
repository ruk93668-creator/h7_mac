"""Execute current Task/App_USB with real Mode/Core/UVC/BSP/Bridge; HAL and media are stubs."""
import run_tests as b
import json,re
ROOT=b.ROOT;HERE=b.HERE;OUT=b.OUT
objects=[]
for src in [HERE/'integration_test.c',ROOT/'CM7/Service/usb_uvc_desc.c',ROOT/'CM7/Service/usb_uvc_payload.c']+list((ROOT/'Middlewares/ST/STM32_USB_Device_Library/Core/Src').glob('*.c')):
 obj=OUT/('integration_'+src.stem+'.o');b.run(b.cc+['-c',src,'-o',obj]);objects.append(obj)
pcd=ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c'
exports=re.findall(r'(?m)^(?:__weak\s+)?[\w *]+\b(HAL_PCD_\w+)\([^;{}]*\)\s*\{',pcd.read_text())
renames=['-D'+n+'=Stream_'+n for n in sorted(set(exports)) if not n.endswith('Callback') and n not in ['HAL_PCD_MspInit','HAL_PCD_MspDeInit']]
for src,extra in [(pcd,renames),(ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c',[])]:
 obj=OUT/('integration_'+src.stem+'.o');b.run(b.cc+extra+['-c',src,'-o',obj]);objects.append(obj)
axf=OUT/'integration.axf'
scatter=OUT/'integration.sct'
scatter.write_text('LR_TEST 0x08000000 0x100000 { ER_FW_INIT 0x08000000 { *(.framework_init) } ER_TEST +0 { .ANY (+RO) } RW_TEST 0x20000000 0x40000 { .ANY (+RW +ZI) } }\n')
b.run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','B2_IntegrationRun','--scatter',scatter,'--no_scanlib','-o',axf])
with axf.open('rb') as f:
 e=b.ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['STOP before enumeration','Configured without Commit/Alt1','Real ISO EOF and Bridge Return','STOP in flight waits for ACK','Abort failure retains frame and retries','Reset preserves business and returns frame','Disconnect preserves business and returns frame','Suspend Resume preserves loan','DataIn only confirms in Task','EP0 device descriptor','EP0 STALL next SETUP recovery','ISO Incomplete with failed Abort','Return ACK queue full','Hardware Stop failure retry','Repeated STOP','Real TEST enumeration','NONE no physical connection','Repeated START BUSY fault','Mode enumeration timeout']
rows=[]
for id,name in enumerate(names):
 u=b.Uc(b.UC_ARCH_ARM,b.UC_MODE_THUMB|b.UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x600000),(0x01000000,0x1000),(0x40000000,0x100000),(0x58000000,0x40000),(0xE000E000,0x2000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(b.UC_ARM_REG_SP,0x2003fff0);u.reg_write(b.UC_ARM_REG_LR,0x01000001);u.reg_write(b.UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(syms['B2_IntegrationRun']|1,0x01000000,count=20000000)
 except Exception as ex:error=str(ex)
 line=u.reg_read(b.UC_ARM_REG_R0)
 detail=b.struct.unpack('<6I',u.mem_read(syms['g_result'],24))
 passed=not error and line==0 and detail[0]==0 and u.reg_read(b.UC_ARM_REG_PC)==0x01000000
 rows.append({'id':id,'name':name,'pass':passed,'return':line,'failure_line':detail[0],'actual':detail[2],'expected':detail[3],'error':error})
 print(id,name,'PASS' if passed else 'FAIL',detail,error,flush=True)
result={'summary':{'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)},'cases':rows,'hardware_verified':False,'real_sources':['usb_task.c','app_usb.c','app_uvc_bridge.c','usb_mode.c','usb_device.c','usb_uvc_class.c','usb_uvc_desc.c','usb_uvc_payload.c','bsp_usb.c','ST USB Core'],'mocked':['HAL hardware','message transport','RTOS environment','Media frames','Dispatcher/Storage boundary']}
(HERE/'integration_results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(result['summary']);raise SystemExit(bool(result['summary']['failed']))
