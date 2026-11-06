"""Full ARMCC5 target rebuilds using a temporary clean project; preserve local IDE edits."""
from pathlib import Path
import subprocess,sys,re,json,shutil,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'tmp/jpeg_stage03a/firmware';OUT.mkdir(parents=True,exist_ok=True)
project=subprocess.check_output(['git','show','HEAD:MDK-ARM/h7_mac.uvprojx'],cwd=ROOT).decode('utf-8').replace('\r\n','\n')
a=project.index('<TargetName>h7_mac_CM7</TargetName>');b=project.index('</Target>',a)
part=project[a:b]
if '<FileName>app_camera_jpeg.c</FileName>' not in part:
    at=part.index('<File>',part.index('<FileName>app_camera.c</FileName>'))
    part=part[:at]+"<File><FileName>app_camera_jpeg.c</FileName><FileType>1</FileType><FilePath>../CM7/Application/app_camera_jpeg.c</FilePath></File>\n"+part[at:]
project=project[:a]+part+project[b:]
uvc=ROOT/'MDK-ARM/h7_mac_jpeg_stage03a_uvc.uvprojx';test=ROOT/'MDK-ARM/h7_mac_jpeg_stage03a_test.uvprojx'
uvc.write_bytes(project.replace('\n','\r\n').encode('utf-8'))
a=project.index('<TargetName>h7_mac_CM7</TargetName>');b=project.index('</Target>',a)
part,n=re.subn(r'<Define>(.*?)</Define>',lambda m:'<Define>'+m.group(1)+',APP_VIDEO_TEST_MODE=1</Define>',project[a:b],count=1,flags=re.S)
assert n==1
test.write_bytes((project[:a]+part+project[b:]).replace('\n','\r\n').encode('utf-8'))
jpeg=ROOT/'MDK-ARM/h7_mac_jpeg_stage03a_acceptance.uvprojx'
jpeg.write_bytes(test.read_bytes().replace(b'APP_VIDEO_TEST_MODE=1',b'APP_VIDEO_TEST_MODE=1,APP_CAMERA_JPEG_TEST_ENABLED=1').replace(b'<OutputDirectory>h7_mac_CM7'+bytes([92])+b'</OutputDirectory>',b'<OutputDirectory>h7_mac_CM7_JPEG'+bytes([92])+b'</OutputDirectory>'))
if '--prepare-only' in sys.argv:
    print(jpeg);raise SystemExit(0)
if '--retest' in sys.argv:
    retest=ROOT/'MDK-ARM/h7_mac_jpeg_stage03a_retest.uvprojx'
    retest.write_bytes(jpeg.read_bytes().replace(b'h7_mac_CM7_JPEG',b'h7_mac_CM7_JPEG_RETEST'))
    jpeg=retest
rows=[]
for variant,proj,target in [('cm7_test',test,'h7_mac_CM7'),('cm7_jpeg',jpeg,'h7_mac_CM7'),('cm4',uvc,'h7_mac_CM4'),('cm7_uvc',uvc,'h7_mac_CM7')]:
    if '--variant' in sys.argv and variant != sys.argv[sys.argv.index('--variant')+1]:
        continue
    output_folder = ('h7_mac_CM7_JPEG_RETEST' if '--retest' in sys.argv else 'h7_mac_CM7_JPEG') if variant=='cm7_jpeg' else target
    if '--isolated' in sys.argv:
        xml=proj.read_text(encoding='utf-8')
        first=xml.index('<TargetName>'+target+'</TargetName>');last=xml.index('</Target>',first)
        block=xml[first:last]
        output_folder='h7_mac_STAGE03A_VERIFY_'+variant
        block=re.sub(r'<OutputDirectory>.*?</OutputDirectory>',lambda m:'<OutputDirectory>'+output_folder+chr(92)+'</OutputDirectory>',block,count=1)
        proj=ROOT/('MDK-ARM/h7_mac_jpeg_stage03a_'+variant+'_verify.uvprojx')
        proj.write_bytes((xml[:first]+block+xml[last:]).replace('\r\n','\n').replace('\n','\r\n').encode('utf-8'))
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
    for suffix in ['.axf','.map','.hex']:
        src=ROOT/'MDK-ARM'/output_folder/(target+suffix)
        if src.exists():shutil.copy2(src,OUT/(variant+suffix))
    print(variant,'0 Error, 0 Warning',flush=True)
