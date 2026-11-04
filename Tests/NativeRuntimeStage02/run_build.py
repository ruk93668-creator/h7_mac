"""Full ARMCC5 target rebuilds using a temporary clean project; preserve local IDE edits."""
from pathlib import Path
import subprocess,sys,re,json,shutil,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'tmp/native_runtime_stage02';OUT.mkdir(parents=True,exist_ok=True)
project=subprocess.check_output(['git','show','HEAD:MDK-ARM/h7_mac.uvprojx'],cwd=ROOT).decode('utf-8').replace('\r\n','\n')
if '<FileName>app_messages.c</FileName>' not in project:
    addition='''            <File><FileName>app_messages.c</FileName><FileType>1</FileType><FilePath>../CM7/Application/app_messages.c</FilePath></File>
            <File><FileName>app_messages.h</FileName><FileType>5</FileType><FilePath>../CM7/Application/app_messages.h</FilePath></File>
'''
    needle='            <File><FileName>app_tasks.c</FileName>'
    assert project.count(needle)==2
    project=project.replace(needle,addition+needle)
uvc=ROOT/'MDK-ARM/h7_mac_native_stage02_check.uvprojx';test=ROOT/'MDK-ARM/h7_mac_native_stage02_test.uvprojx'
uvc.write_bytes(project.replace('\n','\r\n').encode('utf-8'))
a=project.index('<TargetName>h7_mac_CM7</TargetName>');b=project.index('</Target>',a)
part,n=re.subn(r'<Define>(.*?)</Define>',lambda m:'<Define>'+m.group(1)+',APP_VIDEO_TEST_MODE=1</Define>',project[a:b],count=1,flags=re.S)
assert n==1
test.write_bytes((project[:a]+part+project[b:]).replace('\n','\r\n').encode('utf-8'))
rows=[]
for variant,proj,target in [('cm7_test',test,'h7_mac_CM7'),('cm7_uvc',uvc,'h7_mac_CM7'),('cm4',uvc,'h7_mac_CM4')]:
    log=HERE/(variant+'_rebuild.log')
    command=['D:/32/UV4/UV4.exe','-j0','-r',str(proj),'-t',target,'-o',str(log)]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    p=subprocess.run(command,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
    text=log.read_text(errors='replace') if log.exists() else ''
    match=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
    clean=bool(match) and match.groups()==('0','0')
    row={'variant':variant,'command':command,'exit':p.returncode,'errors':int(match[1]) if match else None,'warnings':int(match[2]) if match else None,'pass':clean}
    rows.append(row)
    (HERE/'build_results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    if not clean:print(text[-4000:]);raise SystemExit(1)
    for suffix in ['.axf','.map']:
        src=ROOT/'MDK-ARM'/target/(target+suffix)
        if src.exists():shutil.copy2(src,OUT/(variant+suffix))
    print(variant,'0 Error, 0 Warning',flush=True)
