from pathlib import Path
import subprocess, re, json
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/MSC24/build'; OUT.mkdir(parents=True,exist_ok=True)
project=ROOT/'MDK-ARM/h7_mac.uvprojx'
original=project.read_bytes(); rows=[]
rte=ROOT/'MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h';rte_original=rte.read_bytes()
for mode,defines in [('msc','USB_MODE_BOOT_MSC=1'),('uvc','USB_MODE_BOOT_MSC=0'),('sd_readonly','USB_MODE_BOOT_MSC=1,USB_MSC_SD_TEST=1')]:
    temp=ROOT/('MDK-ARM/msc24_'+mode+'.uvprojx')
    assert not temp.exists()
    text=original.decode('utf-8'); start=text.index('<TargetName>h7_mac_CM7</TargetName>')
    before=text[:start]; tail=text[start:]
    tail=re.sub(r'<OutputDirectory>.*?</OutputDirectory>',lambda m:'<OutputDirectory>../output/MSC24/build/'+mode+'/</OutputDirectory>',tail,count=1)
    tail=re.sub(r'<OutputName>.*?</OutputName>','<OutputName>h7_mac_CM7_'+mode+'</OutputName>',tail,count=1)
    at=tail.index('</Define>',tail.index('<Define>'));tail=tail[:at]+','+defines+tail[at:]
    temp.write_bytes((before+tail).encode('utf-8'))
    try:
        log=OUT/(mode+'.log');startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        result=subprocess.run(['D:/32/UV4/UV4.exe','-j0','-r',str(temp),'-t','h7_mac_CM7','-o',str(log)],cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
        text=log.read_text(errors='replace') if log.exists() else ''
        match=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
        row=dict(mode=mode,exit=result.returncode,summary=match.group(0) if match else None,passed=result.returncode==0 and match is not None and match.groups()==('0','0'))
        artifact=OUT/mode/'.axf'
        if artifact.exists():
            import shutil
            shutil.copy2(artifact,OUT/('h7_mac_CM7_'+mode+'.axf'))
        rows.append(row);print(row,flush=True)
        if not row['passed']:print(text[-6000:],flush=True)
    finally:
        temp.unlink();rte.write_bytes(rte_original);assert project.read_bytes()==original
(OUT/'results.json').write_text(json.dumps(rows,indent=2))
(ROOT/'Tests/MscJoint/build_results.json').write_text(json.dumps(rows,indent=2))
raise SystemExit(not all(r['passed'] for r in rows))
