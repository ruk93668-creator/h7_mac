"""Build baseline/current CM7; keep production project and original CM4 target bytes."""
from pathlib import Path
import subprocess,json,re,hashlib
ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
rte=ROOT/'MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h'; rte_before=rte.read_bytes()
proj=ROOT/'MDK-ARM/h7_mac.uvprojx';text=proj.read_bytes().decode('utf-8')
temp=ROOT/'MDK-ARM/h7_mac_ble_b1_baseline.uvprojx'
assert not temp.exists()
baseline=re.sub(r'\s*<File><FileName>bsp_bt_uart\.[ch]</FileName>.*?</File>','',text)
baseline=baseline.replace('\n--keep=bsp_bt_uart.o(*)','')
a=baseline.index('<TargetName>h7_mac_CM7</TargetName>');b=baseline.index('</Target>',a)
part=re.sub(r'<OutputDirectory>.*?</OutputDirectory>',r'<OutputDirectory>..\\Tests\\BleB1\\build\\baseline\\</OutputDirectory>',baseline[a:b])
part=re.sub(r'<ListingPath>.*?</ListingPath>',r'<ListingPath>..\\Tests\\BleB1\\build\\baseline\\</ListingPath>',part)
baseline=baseline[:a]+part+baseline[b:]
rows=[]
try:
 temp.write_bytes(baseline.encode('utf-8'))
 for name,project,target,mapfile in [('current_cm7',proj,'h7_mac_CM7',ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.map'),('baseline_cm7',temp,'h7_mac_CM7',HERE/'build/baseline/h7_mac_CM7.map')]:
  log=HERE/(name+'_build.log');cmd=['D:/32/UV4/UV4.exe','-j0','-b',str(project),'-t',target,'-o',str(log)]
  p=subprocess.run(cmd,cwd=ROOT,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW)
  data=log.read_text(errors='replace');match=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',data)
  ok=p.returncode==0 and match and match.groups()==('0','0')
  mapping=mapfile.read_text(errors='replace') if mapfile.exists() else ''
  regions={n:{'size':int(size,16),'limit':int(limit,16)} for n,size,limit in re.findall(r'Execution Region (\w+) \(.*?Size: (0x[0-9a-fA-F]+), Max: (0x[0-9a-fA-F]+)',mapping)}
  api={n:bool(re.search(r'\b'+n+r'\s+0x[0-9a-fA-F]+',mapping)) for n in ['BSP_BT_UART_Init','BSP_BT_UART_Read','BSP_BT_UART_Write','BSP_BT_UART_Recover','BSP_BT_UART_DeInit','BSP_BT_UART_GetDiagnostics','USART2_IRQHandler']}
  row={'name':name,'command':cmd,'exit':p.returncode,'pass':bool(ok),'regions':regions,'api_symbols':api,'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()};rows.append(row)
  (HERE/'build_results.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
  print(name,'PASS' if ok else 'FAIL',regions,api,flush=True)
  if not ok: print(data[-4000:]);raise SystemExit(1)
  if name=='current_cm7': assert all(api.values()), 'debug API removed'
finally:
 pattern=rb"(Project: ')[^']*(')"
 if re.sub(pattern,rb'\1PROJECT\2',rte.read_bytes())==re.sub(pattern,rb'\1PROJECT\2',rte_before): rte.write_bytes(rte_before)
 else: print('RTE changed beyond generated project comment; preserved for review')
 for suffix in ['.uvprojx','.uvoptx']:
  f=temp.with_suffix(suffix)
  if f.exists():f.unlink()
