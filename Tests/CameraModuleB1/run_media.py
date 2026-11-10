"""Run real CameraTask/App_Camera/Bridge/Dispatcher/Storage/JPEG/AVI with medium stubs."""
import run_tests as b
import json
ROOT=b.ROOT;HERE=b.HERE;OUT=b.OUT
data=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes()
(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[] = {'+','.join(map(str,data))+'};\n')
obj=OUT/'media.o';axf=OUT/'media.axf'
defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']
b.run(b.cc+['-I',OUT]+defs+['-c',HERE/'media_test.c','-o',obj])
b.run(['D:/32/ARM/ARMCC/Bin/armlink.exe',obj,'--entry','B1_MediaRun','--scatter',OUT/'test.sct','-o',axf])
with axf.open('rb') as f:
 e=b.ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['YUY2 held frame STOP','BSP stop error retry','JPEG held frame STOP','JPEG FAULT DRAINING','JPEG FAULT without loan','BSP BUSY retry']
rows=[]
for id,name in enumerate(names):
 u=b.Uc(b.UC_ARCH_ARM,b.UC_MODE_THUMB|b.UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x600000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(b.UC_ARM_REG_SP,0x2003fff0);u.reg_write(b.UC_ARM_REG_LR,0x01000001);u.reg_write(b.UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(syms['B1_MediaRun']|1,0x01000000,count=10000000)
 except Exception as ex:error=str(ex)
 line=u.reg_read(b.UC_ARM_REG_R0)
 passed=not error and line==0 and u.reg_read(b.UC_ARM_REG_PC)==0x01000000
 rows.append({'id':id,'name':name,'pass':passed,'failure_line':line,'error':error})
 print(id,name,'PASS' if passed else 'FAIL',line,error,flush=True)
result={'summary':{'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)},'cases':rows,'hardware_verified':False,'real_sources':['app_camera.c','camera_task.c','camera_capture.c'],'mocked':['Device','Bridge','BSP hardware','JPEG FIFO loan boundary','message transport']}
(HERE/'media_results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(result['summary'])
raise SystemExit(bool(result['summary']['failed']))
