"""Finite protocol checks; WRITE negative is locked behind compiled RO, live WP and hardware guard evidence."""
from pathlib import Path
import sys,json,re,hashlib,ctypes as C,datetime
from windows_read import ReadSession,PACKET,SPTD,kernel,W,OUT
ROOT=Path(__file__).resolve().parents[2]
def guard():
 build=json.loads((ROOT/'Tests/MscSdReadOnly/build_results.json').read_text())['builds'][0]
 if build['preprocessed_values']!={'BOOT':'1','SD':'1','WRITE':'0'} or build['adapter_write_call_present']:raise RuntimeError('Compile-time RO proof missing')
 if hashlib.sha256(Path(build['artifact']).read_bytes()).hexdigest()!=build['sha256']:raise RuntimeError('Audited image changed')
 log=ROOT/'MDK-ARM/msc4_backend_trace_20261118.txt'
 if not log.exists():raise RuntimeError('Hardware guard log missing; no negative WRITE issued')
 text=log.read_text(errors='replace')
 if re.search(r'\*\*\* error|cannot|could not|failed to set|UNEXPECTED_SD_WRITE',text,re.I):raise RuntimeError('Debugger probe error/fault; negative WRITE blocked')
 addresses=json.loads((OUT/'probe_breakpoints.json').read_text())
 for address in addresses.values():
  if address[2:].lower() not in text.lower():raise RuntimeError('Breakpoint list does not confirm all probe addresses')
 return log
mode=sys.argv[1] if len(sys.argv)>1 else 'trace_read';report=dict(mode=mode,started=datetime.datetime.now(datetime.timezone.utc).isoformat());session=None
try:
 session=ReadSession()
 if mode=='trace_read':
  data,row=session.read(1024,64);report['result']=row;report['expected_backend_requests']=32
  if row['scsi_status']!=0 or len(data)!=32768:raise RuntimeError('Trace read failed')
 elif mode=='write_protect':
  log=guard();data,row=session.command(bytes([0x1A,8,0x3F,0,192,0]),192)
  if row['scsi_status'] or len(data)<4 or not data[2]&0x80:raise RuntimeError('Live WP bit not confirmed')
  memory=kernel.VirtualAlloc(None,4096,0x3000,4)
  if not memory:raise C.WinError(C.get_last_error())
  try:
   packet=PACKET();s=packet.spt;s.Length=C.sizeof(SPTD);s.CdbLength=10;s.SenseInfoLength=32;s.DataIn=0;s.DataTransferLength=512;s.TimeOutValue=15;s.DataBuffer=memory;s.SenseInfoOffset=PACKET.sense.offset
   fixed_cdb=bytes.fromhex('2a000000000000000100')
   for i,b in enumerate(fixed_cdb):s.Cdb[i]=b
   returned=W.DWORD();ok=kernel.DeviceIoControl(session.handle,0x4D014,C.byref(packet),C.sizeof(packet),C.byref(packet),C.sizeof(packet),C.byref(returned),None)
   sense=bytes(packet.sense);report['result']=dict(cdb=fixed_cdb.hex(),ioctl_ok=bool(ok),winerror=C.get_last_error() if not ok else 0,scsi_status=s.ScsiStatus,sense=sense.hex(),transport='Requires capture correlation; OS may reject before USB')
   report['data_protect_observed']=s.ScsiStatus!=0 and sense[2]&15==7 and sense[12]==0x27
   if 'UNEXPECTED_SD_WRITE' in log.read_text(errors='replace'):raise RuntimeError('Unexpected SD write entry trapped; keep target halted')
  finally:kernel.VirtualFree(memory,0,0x8000)
 elif mode=='phase_error':
  # Deliberately H_i < D_i for READ only, to observe USBSTOR recovery; never send SD writes.
  data,row=session.command(bytes.fromhex('28000000000000000100'),256);report['result']=row
 elif mode=='commands':
  for cdb,length in [(bytes([0x12,0,0,0,36,0]),36),(bytes(6),0),(bytes([3,0,0,0,18,0]),18)]:
   data,row=session.command(cdb,length)
  report['commands']=session.trace
 else:raise ValueError('Unknown finite test mode')
 report['completed']=True
except Exception as error:report['error']=repr(error);raise
finally:
 if session:report['commands']=session.trace;session.close()
 (OUT/('protocol_'+mode+'_results.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
