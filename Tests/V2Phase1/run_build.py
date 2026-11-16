from pathlib import Path
import subprocess, shutil, datetime, json, hashlib, re
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/V2Phase1'/('build_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
OUT.mkdir(parents=True)
report={'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip(),'output':str(OUT),'targets':[]}
report['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'CM7/ApplicationV2').rglob('*')) if p.is_file()}
report['project_sha256']=hashlib.sha256((ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()).hexdigest()
report['toolchain']=subprocess.run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'],capture_output=True).stdout.decode(errors='replace')
for target in ['h7_mac_CM7','h7_mac_CM4']:
    folder=ROOT/'MDK-ARM'/target
    if folder.exists(): shutil.copytree(folder,OUT/'previous_output'/target)
    log=OUT/(target+'.log')
    cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(ROOT/'MDK-ARM/h7_mac.uvprojx'),'-t',target,'-o',str(log)]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
    p=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
    text=log.read_bytes().decode('utf-8',errors='replace') if log.exists() else ''
    files=[]
    for f in sorted(folder.rglob('*')):
        if f.is_file() and f.suffix.lower() in ['.axf','.map','.htm','.hex','.bin']:
            dest=OUT/'artifacts'/target/f.relative_to(folder);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,dest)
            files.append({'file':str(dest.relative_to(OUT)),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'bytes':dest.stat().st_size})
    row={'target':target,'command':cmd,'exit':p.returncode,'zero_errors_warnings':bool(re.search(r'0 Error\(s\), 0 Warning\(s\)',text)),'program_size':re.findall(r'Program Size:.*',text),'artifacts':files}
    report['targets'].append(row)
    (OUT/'build_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(target,p.returncode,row['zero_errors_warnings'],row['program_size'],flush=True)
print(OUT,flush=True)
raise SystemExit(any(r['exit'] or not r['zero_errors_warnings'] for r in report['targets']))
