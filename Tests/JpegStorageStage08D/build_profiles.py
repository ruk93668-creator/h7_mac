from pathlib import Path
import subprocess,re,json,hashlib
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent;O=R/'output/Stage08D/profiles';O.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');rows=[]
project=R/'MDK-ARM/h7_mac.uvprojx';before=hashlib.sha256(project.read_bytes()).hexdigest()
source=(R/'CM7/Application/video_config.c').read_bytes();lnp=(R/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.lnp').read_text()
for name,defines,none in [('mjpeg_usb',['APP_VIDEO_MJPEG_MODE=1'],False),('usb_test',['APP_VIDEO_TEST_MODE=1'],False),('jpeg_sd_only',['APP_VIDEO_MJPEG_MODE=1'],True)]:
 out=O/name;out.mkdir(exist_ok=True);src=source
 if none:
  old=b'.output = APP_VIDEO_TEST_MODE ? VIDEO_OUTPUT_USB_TEST : VIDEO_OUTPUT_USB_UVC,'
  assert old in src;src=src.replace(old,b'.output = VIDEO_OUTPUT_NONE,')
 path=out/'video_config.c';path.write_bytes(src);obj=out/'video_config.o';axf=out/'h7_mac_CM7.axf';mp=out/'h7_mac_CM7.map'
 args=[str(cc),'--cpu','Cortex-M7.fp.dp','--c99','--gnu','-O0','--split_sections','-g']
 for d in ['CM7/Application','CM7/Platform','Common/Inc']:args+=['-I',str(R/d)]
 for d in defines:args+=['-D'+d]
 args+=['-c',str(path),'-o',str(obj)]
 compile_run=subprocess.run(args,capture_output=True)
 text=(compile_run.stdout+compile_run.stderr).decode(errors='replace');assert compile_run.returncode==0 and 'Warning:' not in text,text
 cmd=re.sub(r'"h7_mac_cm7\\video_config\.o"','"'+str(obj).replace('\\','/')+'"',lnp,flags=re.I)
 assert cmd!=lnp
 cmd=re.sub(r'--list\s+"[^"]+"','--list "'+str(mp).replace('\\','/')+'"',cmd)
 cmd=re.sub(r'-o\s+[^\r\n]+','-o "'+str(axf).replace('\\','/')+'"',cmd)
 response=out/'link.lnp';response.write_text(cmd)
 linkargs=[str(cc.with_name('armlink.exe')),'--via',str(response)]
 linked=subprocess.run(linkargs,cwd=R/'MDK-ARM',capture_output=True);text+=(linked.stdout+linked.stderr).decode(errors='replace')
 (H/(name+'_build.log')).write_text(text,encoding='utf-8')
 row=dict(profile=name,compiled_config_defines=defines,configuration_only_change='output=NONE' if none else 'none',compile_command=args,link_command=linkargs,exit_code=linked.returncode,warnings=len(re.findall('Warning:',text)),errors=len(re.findall('Error:',text)),reused_fully_built_objects=True,axf=str(axf.relative_to(R)),sha256=hashlib.sha256(axf.read_bytes()).hexdigest() if axf.exists() else None,project_unchanged=hashlib.sha256(project.read_bytes()).hexdigest()==before)
 rows.append(row);assert linked.returncode==0 and row['errors']==0 and row['warnings']==0 and row['project_unchanged'],text
(H/'profile_builds.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8');print(rows)
