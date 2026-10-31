from pathlib import Path
import subprocess, json, hashlib, difflib
ROOT=Path.cwd(); FORMAL=Path(r'D:\32\STM32H7\h7_mac'); OLD=FORMAL/'tmp/verify_0395fa8'; PRE=FORMAL/'tmp/verify_0395fa8_before_remaining19'
OUT=ROOT/'Tests/UsbStage4Migration'; OUT.mkdir(parents=True,exist_ok=True)
def git(*a,cwd=FORMAL): return subprocess.check_output(['git',*a],cwd=cwd)
def sha(b): return hashlib.sha256(b).hexdigest()
def decode(b):
    try: return b.decode('utf-8-sig').replace('\r\n','\n')
    except UnicodeDecodeError: return b.decode('gbk').replace('\r\n','\n')
base=git('rev-parse','origin/main').decode().strip(); assert base==git('rev-parse','HEAD',cwd=ROOT).decode().strip()
paths=git('ls-files').decode().splitlines()
protection={'head':git('rev-parse','HEAD').decode().strip(),'tracked_status':git('status','--porcelain','--untracked-files=no').decode(),'index_sha':sha((FORMAL/'.git/index').read_bytes()),'files':{p:sha((FORMAL/p).read_bytes()) if (FORMAL/p).is_file() else None for p in paths}}
(OUT/'formal_before.json').write_text(json.dumps(protection,indent=2),encoding='utf-8')
files=['CM7/BSP/bsp_usb.c','CM7/BSP/bsp_usb.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','CM7/Platform/usb_mode.c','CM7/Service/usb_test_device.c']
map_path=lambda p:p.replace('CM7/Platform/usb_mode.c','CM7/Service/usb_mode.c')
common=git('merge-base',base,'0395fa8').decode().strip(); assert common.startswith('949180c')
phase0={p:decode(git('show',common+':'+p)) for p in files}
phase1={p:decode(git('show','0395fa8:'+p)) for p in files}
phase2={p:decode((PRE/p).read_bytes()) if (PRE/p).is_file() else phase1[p] for p in files}
phase3={p:decode((OLD/p).read_bytes()) for p in files}
phase3['CM7/BSP/bsp_usb.c']=decode((OLD/'Tests/Ep0IrqRace/bsp_usb.c.before').read_bytes())
phase3['CM7/Platform/usb_device.c']=decode((OLD/'Tests/Ep0IrqRace/usb_device.c.before').read_bytes())
phase4={p:decode((OLD/p).read_bytes()) for p in files}
phases=[phase0,phase1,phase2,phase3,phase4]
names=['stage3_architecture','ep0_P0_and_missing_tests','remaining19','ep0_old_XFRC']
current={p:decode((ROOT/map_path(p)).read_bytes()) for p in files}; layers=[]
for n,(before,after) in enumerate(zip(phases,phases[1:])):
    item={'name':names[n],'files':[],'conflicts':[]}; patch=[]
    for p in files:
        if before[p]==after[p]: continue
        target=map_path(p); item['files'].append(target)
        for name,data in [('ours',current[p]),('base',before[p]),('theirs',after[p])]: (OUT/(name+'.txt')).write_bytes(data.encode('utf-8'))
        m=subprocess.run(['git','merge-file','-p',str(OUT/'ours.txt'),str(OUT/'base.txt'),str(OUT/'theirs.txt')],capture_output=True)
        if m.returncode: raise RuntimeError('merge conflict: '+target+' '+m.stdout.decode('utf-8'))
        result=m.stdout.decode('utf-8'); patch.extend(difflib.unified_diff(current[p].splitlines(True),result.splitlines(True),fromfile='a/'+target,tofile='b/'+target))
        current[p]=result
    (OUT/(names[n]+'.patch')).write_text(''.join(patch),encoding='utf-8',newline='\n'); layers.append(item)
for p,text in current.items():
    assert text==phase4[p],p
    if decode((ROOT/map_path(p)).read_bytes())!=text: (ROOT/map_path(p)).write_bytes(text.replace('\n','\r\n').encode('utf-8'))
# Main already has the complete Core decoupling and Service relocation; retain its project/header/path setup.
for p in ['core_test.c','endpoint_lifecycle_test.c','legacy_test.c','run_tests.py','stage3_acceptance.md']:
    data=decode((OLD/'Tests/UsbCoreDecoupling'/p).read_bytes())
    data=data.replace('CM7/Platform/usb_mode','CM7/Service/usb_mode')
    if p=='run_tests.py': data=data.replace('"CM7/Platform", "CM7/Framework"','"CM7/Platform", "CM7/Service", "CM7/Framework"')
    (ROOT/'Tests/UsbCoreDecoupling'/p).write_bytes(data.replace('\n','\r\n').encode('utf-8'))
irq=ROOT/'Tests/Ep0IrqRace'; irq.mkdir(exist_ok=True)
for p in ['race_test.c','run_race.py']:
    data=decode((OLD/'Tests/Ep0IrqRace'/p).read_bytes()).replace('CM7/Platform/usb_mode','CM7/Service/usb_mode')
    if p=='run_race.py': data=data.replace('"CM7/Platform", "CM7/Framework"','"CM7/Platform", "CM7/Service", "CM7/Framework"')
    (irq/p).write_bytes(data.replace('\n','\r\n').encode('utf-8'))
manifest={'baseline':base,'common_base':common,'tested_branch':'0395fa8785830bf0d9571ea9da0cea61f91ad027','layers':layers,'path_resolution':'Retain main Service Mode Manager, header, Keil project and existing move tests','production':{map_path(p):{'reference':p,'sha256':sha((ROOT/map_path(p)).read_bytes()),'canonical_equal_to_accepted':current[p]==phase4[p]} for p in files}}
(OUT/'migration_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
for name in ['ours.txt','base.txt','theirs.txt']: (OUT/name).unlink()
print(json.dumps(manifest,ensure_ascii=False,indent=2))