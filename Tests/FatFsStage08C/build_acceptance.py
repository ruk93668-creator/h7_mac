"""Build a Tests-only acceptance image using the original project, restoring its exact bytes."""
from pathlib import Path
import subprocess,xml.etree.ElementTree as E,json,hashlib,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/FatFsStage08C/hardware';OUT.mkdir(parents=True,exist_ok=True)
project=ROOT/'MDK-ARM/h7_mac.uvprojx';original=project.read_bytes();(OUT/'project_before.uvprojx').write_bytes(original)
sct=(ROOT/'MDK-ARM/h7_mac_CM7_custom.sct').read_bytes().decode('gbk')
insert='    RW_FATFS_ACCEPT 0x24010000 UNINIT 0x00050000\n    {\n        .ANY1 (.fatfs_acceptance)\n    }\n\n'
sct=sct.replace('    RW_IRAM1',insert+'    RW_IRAM1');scatter=OUT/'acceptance.sct';scatter.write_bytes(sct.encode('gbk'))
tree=E.fromstring(original);t=next(x for x in tree.findall('./Targets/Target') if x.findtext('TargetName')=='h7_mac_CM7')
for f in t.findall('./Groups/Group/Files/File'):
    if f.findtext('FileName')=='app_tasks.c':
        f.find('FileName').text='acceptance_tasks.c'
        f.find('FilePath').text='../Tests/FatFsStage08C/acceptance_tasks.c'
# Avoid Keil cross-target file-group synchronization in the temporary image.
for other in list(tree.find('Targets')):
    if other is not t:tree.find('Targets').remove(other)
t.find('.//TargetCommonOption/OutputDirectory').text='..\\output\\FatFsStage08C\\hardware\\'
t.find('.//TargetCommonOption/ListingPath').text='..\\output\\FatFsStage08C\\hardware\\'
t.find('.//LDads/ScatterFile').text=str(scatter)
log=HERE/'hardware_build.log';cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t','h7_mac_CM7','-o',str(log)]
try:
    project.write_bytes(E.tostring(tree,encoding='utf-8',xml_declaration=True))
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    r=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
finally:
    project.write_bytes(original)
text=log.read_text(errors='replace');matches=list(re.finditer(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text));last=matches[-1] if matches else None
result=dict(command=cmd,exit_code=r.returncode,errors=int(last[1]) if last else None,warnings=int(last[2]) if last else None,project_restored=project.read_bytes()==original,project_sha256=hashlib.sha256(original).hexdigest(),image=str(OUT/'h7_mac_CM7.axf'),hardware_verified=False)
(HERE/'hardware_build.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
raise SystemExit(not (r.returncode==0 and last and last[1]=='0' and last[2]=='0' and result['project_restored']))
