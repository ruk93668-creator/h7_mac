"""One fixed Keil project with reusable targets; outputs stay outside MDK-ARM."""
from pathlib import Path
import subprocess,copy,xml.etree.ElementTree as ET,json,re,sys,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
# Use current project entries, including uncommitted source-file additions.
xml=(ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()
tree=ET.fromstring(xml);targets=tree.find('Targets');cm7=next(x for x in targets if x.findtext('TargetName')=='h7_mac_CM7');cm4=next(x for x in targets if x.findtext('TargetName')=='h7_mac_CM4')
for x in list(targets):targets.remove(x)
variants=[('CM7_MJPEG',cm7,'APP_VIDEO_MJPEG_MODE=1'),('CM7_YUY2',cm7,''),('CM7_TEST',cm7,'APP_VIDEO_TEST_MODE=1'),('CM7_JPEG_SINGLE',cm7,'APP_VIDEO_TEST_MODE=1,APP_CAMERA_JPEG_TEST_ENABLED=1'),('CM4',cm4,'')]
for name,base,defines in variants:
 t=copy.deepcopy(base);t.find('TargetName').text=name
 t.find('.//TargetCommonOption/OutputDirectory').text='..\\output\\build_stage03b\\'+name+'\\'
 if name.startswith('CM7_'):
  # Cold-boot variants must not inherit a user-selected MJPEG/TEST/single-frame mode.
  d=t.find('.//Cads/VariousControls/Define')
  switches={'APP_VIDEO_MJPEG_MODE','APP_VIDEO_TEST_MODE','APP_CAMERA_JPEG_TEST_ENABLED'}
  values=[v.strip() for v in (d.text or '').split(',') if v.strip() and v.strip().split('=',1)[0] not in switches]
  if defines:values.extend(defines.split(','))
  d.text=','.join(values)
  media=next(g for g in t.findall('./Groups/Group') if g.findtext('GroupName')=='CM7\\Media')
  if not any(f.findtext('FileName')=='camera_mjpeg_fifo.c' for f in media.findall('./Files/File')):
   raise RuntimeError('Formal CM7 Media group is missing camera_mjpeg_fifo.c; refusing to generate an incomplete target')
 targets.append(t)
project=ROOT/'MDK-ARM/h7_mac_stage03b.uvprojx'
ET.ElementTree(tree).write(project,encoding='utf-8',xml_declaration=True)
if '--prepare-only' in sys.argv:print(project);raise SystemExit(0)
selected=set(sys.argv[sys.argv.index('--targets')+1].split(',')) if '--targets' in sys.argv else None
rows=json.loads((HERE/'build_results.json').read_text(encoding='utf-8')) if selected and (HERE/'build_results.json').exists() else []
def source_hashes():
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
        for folder in ['CM7','Common','CM4'] for p in (ROOT/folder).rglob('*')
        if p.is_file() and p.suffix in ('.c','.h')}
for name,base,defines in variants:
 if selected and name not in selected:continue
 before=source_hashes()
 log=HERE/(name+'_rebuild.log');cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t',name,'-o',str(log)]
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 result=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
 text=log.read_text(errors='replace') if log.exists() else ''
 match=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
 row={'target':name,'command':cmd,'exit':result.returncode,'errors':int(match[1]) if match else None,'warnings':int(match[2]) if match else None}
 row['source_hashes_before']=before
 row['source_hashes_after']=source_hashes()
 row['source_stable']=row['source_hashes_before']==row['source_hashes_after']
 row['pass']=bool(match) and row['errors']==row['warnings']==0 and row['source_stable']
 rows=[x for x in rows if x['target']!=name];rows.append(row);(HERE/'build_results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
 print(name,{k:v for k,v in row.items() if not k.startswith('source_hashes')},flush=True)
 if not row['pass']:print(text[-6000:]);raise SystemExit(1)
