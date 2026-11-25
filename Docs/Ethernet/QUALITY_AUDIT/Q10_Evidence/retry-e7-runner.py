from pathlib import Path
import os,sys,subprocess,json,hashlib,re,xml.etree.ElementTree as ET
root=Path.cwd()
out=root/'output/Ethernet/q10'
out.mkdir(parents=True,exist_ok=True)
git=['git','-c','safe.directory='+root.as_posix()]
mdk=root/'MDK-ARM'
original=mdk/'h7_mac.uvprojx'
tmp=mdk/'_q10_uv.uvprojx'
assert not tmp.exists(),tmp
rte={}
for core in ('CM7','CM4'):
    p=mdk/f'RTE/_h7_mac_{core}/RTE_Components.h'
    rte[p]=p.read_bytes() if p.exists() else None
mode=sys.argv[1]
results=json.loads((out/(mode+'_results.json')).read_text())
def publish():
    (out/(mode+'_results.json')).write_text(json.dumps(results,indent=2))
def restore():
    for p,value in rte.items():
        if value is not None:
            if not p.exists() or p.read_bytes()!=value:p.write_bytes(value)
        elif p.exists():
            p.unlink()
def formal(key,core,raw,scatter_override=None):
    tree=ET.fromstring(raw)
    targets=tree.find('Targets')
    target=next(t for t in targets if t.findtext('TargetName')==core)
    for t in list(targets):
        if t is not target:targets.remove(t)
    build=out/key
    build.mkdir(exist_ok=True)
    for field in ('OutputDirectory','ListingPath'):
        target.find('.//TargetCommonOption/'+field).text=str(build)+'\\'
    if scatter_override:
        target.find('.//LDads/ScatterFile').text=str(scatter_override)
    ET.ElementTree(tree).write(tmp,encoding='utf-8',xml_declaration=True)
    log=build/'build.log'
    si=subprocess.STARTUPINFO()
    si.dwFlags|=subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow=0
    try:
        p=subprocess.run(['D:/32/UV4/UV4.exe','-j0','-r',str(tmp),'-t',core,'-o',str(log)],
                         cwd=mdk,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW,timeout=240)
    finally:
        restore()
        tmp.unlink(missing_ok=True)
    log_text=log.read_text(errors='replace')
    match=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',log_text)
    size=re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)',log_text)
    artifact=build/(core+'.axf')
    results[key]={'exit':p.returncode,'errors':int(match[1]) if match else None,
        'warnings':int(match[2]) if match else None,
        'sizes':dict(zip(('code','ro','rw','zi'),map(int,size.groups()))) if size else None,
        'firmware_sha256':hashlib.sha256(artifact.read_bytes()).hexdigest() if artifact.exists() else None}
    publish()
    print(key,results[key],flush=True)
    if not match or int(match[1]):print(log_text[-4500:],flush=True)
def isolated(flag):
    code=(root/'Tests/Ethernet/build_firmware.py').read_text(encoding='utf-8')
    code=code.replace("OUT=ROOT/'output/Ethernet'", "OUT=ROOT/'output/Ethernet/q10/isolated'")
    start=code.index("\nproject=ROOT/'MDK-ARM'/")
    end=code.index('\n',start+1)
    code=code[:start]+"\nproject=ROOT/'MDK-ARM'/'_q10_uv.uvprojx'"+code[end:]
    code=code.replace("['rev-parse','HEAD'],cwd=ROOT", "['rev-parse','HEAD'],cwd='D:/32/STM32H7/h7_mac_wk'").replace("'safe.directory='+ROOT.as_posix()", "'safe.directory=D:/32/STM32H7/h7_mac_wk'")
    env=os.environ.copy()
    env['PYTHONPATH']=str(root/'Tests/Ethernet')
    script=out/'isolated_build_worker.py'
    script.write_text("from pathlib import Path\n__file__=str(Path.cwd()/'Tests/Ethernet/build_firmware.py')\n"+code,encoding='utf-8')
    key=flag[2:].replace('-','_')
    try:
        with (out/(key+'_driver.log')).open('wb') as log:
            p=subprocess.run([sys.executable,'-B',str(script),flag],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=240)
    finally:
        restore()
        tmp.unlink(missing_ok=True)
    file=out/'isolated'/(key+'_result.json')
    info=json.loads(file.read_text()) if file.exists() else {}
    results[key]={'exit':p.returncode,'errors':info.get('errors'),'warnings':info.get('warnings'),
                 'layout_ok':info.get('e4_cpu_link_valid'),'stable':info.get('source_stable')}
    publish()
    print(key,results[key],flush=True)
    if p.returncode:print((out/(key+'_driver.log')).read_text(errors='replace')[-1400:],flush=True)
for flag in ('--e7','--e7-v1','--e7-v1-axi','--e7-v1-qvga'):
    isolated(flag)
raise SystemExit(any(v['exit'] or v['errors'] or v['warnings'] for v in results.values()))
