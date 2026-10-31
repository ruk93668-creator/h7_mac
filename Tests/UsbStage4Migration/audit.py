from pathlib import Path
import subprocess,json,hashlib,re
ROOT=Path.cwd(); OUT=ROOT/'Tests/UsbStage4Migration'; FORMAL=Path(r'D:\32\STM32H7\h7_mac')
def git(*a,cwd=ROOT):return subprocess.check_output(['git',*a],cwd=cwd)
def sha(b):return hashlib.sha256(b).hexdigest()
manifest=json.loads((OUT/'migration_manifest.json').read_text(encoding='utf-8'))
before=json.loads((OUT/'formal_before.json').read_text(encoding='utf-8'))
paths=git('ls-files','-z').decode().split('\0');paths=[p for p in paths if p]
# Hash literal baseline blobs, not filtered/newline-normalized content.
entries=git('ls-tree','-rz','HEAD').split(b'\0'); blobs={}
for e in entries:
 if not e:continue
 hdr,p=e.split(b'\t'); blobs[p.decode()]=hdr.split()[2].decode()
ordered=list(blobs)
stream=git('cat-file','--batch',cwd=ROOT) if False else subprocess.check_output(['git','cat-file','--batch'],input=('\n'.join(blobs[p] for p in ordered)+'\n').encode(),cwd=ROOT)
offset=0; protected={}; mismatches=[]
for p in ordered:
 end=stream.index(b'\n',offset); size=int(stream[offset:end].split()[2]); payload=stream[end+1:end+1+size];offset=end+2+size
 if p in manifest['production'] or p.startswith('Tests/'): continue
 actual=(ROOT/p).read_bytes() if (ROOT/p).is_file() else b''
 row={'baseline_sha256':sha(payload),'actual_sha256':sha(actual),'equal':payload==actual};protected[p]=row
 if not row['equal']:mismatches.append(p)
assert not mismatches,mismatches
(OUT/'protected_sha256.json').write_text(json.dumps(protected,ensure_ascii=False,indent=2),encoding='utf-8')
formal_changed=[p for p,h in before['files'].items() if (sha((FORMAL/p).read_bytes()) if (FORMAL/p).is_file() else None)!=h]
assert not formal_changed,formal_changed
assert git('rev-parse','HEAD',cwd=FORMAL).decode().strip()==before['head']
assert git('status','--porcelain','--untracked-files=no',cwd=FORMAL).decode()==before['tracked_status']
assert sha((FORMAL/'.git/index').read_bytes())==before['index_sha']
changed=git('diff','--name-only','HEAD').decode().splitlines(); production=[p for p in changed if not p.startswith('Tests/')]
assert sorted(production)==sorted(manifest['production'])
assert not git('diff','--cached','--name-only').strip()
full=json.loads((ROOT/'Tests/UsbCoreDecoupling/results.json').read_text(encoding='utf-8'))
irq=json.loads((ROOT/'Tests/Ep0IrqRace/results.json').read_text(encoding='utf-8'))
assert full['summary']['total']==204 and full['summary']['passed']==204 and full['summary']['failed']==0 and not full['source_changed']
assert len(irq)==4 and all(r['pass'] for r in irq)
old=json.loads((FORMAL/'tmp/verify_0395fa8/Tests/UsbCoreDecoupling/results.json').read_text(encoding='utf-8'))
assert [r['id'] for r in full['cases']]==[r['id'] for r in old['cases']]
for p in manifest['production']:assert sha((ROOT/p).read_bytes())==manifest['production'][p]['sha256']
builds={}
for target in ['h7_mac_CM7','h7_mac_CM4']:
 text=(OUT/('build_'+target+'.log')).read_text(errors='replace')
 matches=re.findall(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
 assert matches and matches[-1]==('0','0'),(target,matches)
 builds[target]={'errors':0,'warnings':0,'program_size':re.search(r'Program Size:.*',text).group()}
whitespace=subprocess.run(['git','-c','core.whitespace=cr-at-eol','diff','--check'],cwd=ROOT,capture_output=True)
(OUT/'whitespace_check.log').write_bytes(whitespace.stdout+whitespace.stderr)
audit={'baseline':manifest['baseline'],'detached_HEAD':git('rev-parse','HEAD').decode().strip(),'production_changed':production,'protected_files':len(protected),'protected_mismatches':mismatches,'hal_ll_files':sum(p.startswith('Drivers/STM32H7xx_HAL_Driver/') for p in protected),'official_usb_core_files':sum(p.startswith('Middlewares/ST/STM32_USB_Device_Library/Core/') for p in protected),'formal_main_unchanged':True,'formal_index_unchanged':True,'formal_pending_files_unchanged':True,'keil_project_unchanged':(ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()==git('show','HEAD:MDK-ARM/h7_mac.uvprojx'),'service_path_preserved':not (ROOT/'CM7/Platform/usb_mode.c').exists(),'regression_204':full['summary'],'same_204_ids':True,'irq':{'total':len(irq),'passed':sum(r['pass'] for r in irq),'assertions':sum(r['assertions'] for r in irq)},'builds':builds,'commit_push_merge':'none','whitespace_check_exit':whitespace.returncode}
(OUT/'audit.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(audit,ensure_ascii=False,indent=2))