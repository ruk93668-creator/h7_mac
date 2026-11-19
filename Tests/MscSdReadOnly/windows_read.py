"""Controlled Windows SCSI reads only. No arbitrary CDB or disk write API."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json,struct,hashlib,datetime
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/MSC4-readonly'
ALLOWED={0x00,0x03,0x12,0x25,0x1A,0x28}
class SPTD(C.Structure):
 _fields_=[('Length',W.USHORT),('ScsiStatus',W.BYTE),('PathId',W.BYTE),('TargetId',W.BYTE),('Lun',W.BYTE),('CdbLength',W.BYTE),('SenseInfoLength',W.BYTE),('DataIn',W.BYTE),('DataTransferLength',W.ULONG),('TimeOutValue',W.ULONG),('DataBuffer',C.c_void_p),('SenseInfoOffset',W.ULONG),('Cdb',W.BYTE*16)]
class PACKET(C.Structure):_fields_=[('spt',SPTD),('sense',W.BYTE*32)]
kernel=C.WinDLL('kernel32',use_last_error=True)
kernel.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,C.c_void_p];kernel.CreateFileW.restype=W.HANDLE
kernel.DeviceIoControl.argtypes=[W.HANDLE,W.DWORD,C.c_void_p,W.DWORD,C.c_void_p,W.DWORD,C.POINTER(W.DWORD),C.c_void_p];kernel.DeviceIoControl.restype=W.BOOL
kernel.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,W.DWORD,W.DWORD];kernel.VirtualAlloc.restype=C.c_void_p
kernel.VirtualFree.argtypes=[C.c_void_p,C.c_size_t,W.DWORD];kernel.CloseHandle.argtypes=[W.HANDLE]
class ReadSession:
 def __init__(self):
  init=json.loads((OUT/'diagnostics/init_results.json').read_text(encoding='utf-8-sig'));
  if not init['initialization_passed']:raise RuntimeError('Initialization gate failed')
  identity=json.loads((OUT/'windows_identity.json').read_text(encoding='utf-8-sig'));
  if len(identity['targets'])!=1:raise RuntimeError('Unique VID/PID parent mapping required')
  self.target=identity['targets'][0];self.blocks=init['diagnostics']['usb_msc_sd.c:s_sd_info']['logical_block_count'];
  if self.blocks<=16:raise RuntimeError('RAM LUN or invalid SD geometry')
  # Windows pass-through IOCTL requires an access-capable handle; only DATA_IN allowlisted CDBs are exposed.
  self.handle=kernel.CreateFileW(self.target['path'],0xC0000000,3,None,3,0,None)
  if self.handle==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
  self.trace=[]
 def close(self):kernel.CloseHandle(self.handle)
 def command(self,cdb,length):
  if not cdb or cdb[0] not in ALLOWED or not 1<=len(cdb)<=16 or not 0<=length<=65536:raise ValueError('Only bounded DATA_IN / no-data read commands allowed')
  memory=kernel.VirtualAlloc(None,max(length,4096),0x3000,4)
  if not memory:raise C.WinError(C.get_last_error())
  try:
   packet=PACKET();s=packet.spt;s.Length=C.sizeof(SPTD);s.CdbLength=len(cdb);s.SenseInfoLength=32;s.DataIn=1 if length else 2;s.DataTransferLength=length;s.TimeOutValue=15;s.DataBuffer=memory;s.SenseInfoOffset=PACKET.sense.offset
   for i,b in enumerate(cdb):s.Cdb[i]=b
   count=W.DWORD();ok=kernel.DeviceIoControl(self.handle,0x4D014,C.byref(packet),C.sizeof(packet),C.byref(packet),C.sizeof(packet),C.byref(count),None)
   error=C.get_last_error() if not ok else 0
   data=C.string_at(memory,min(s.DataTransferLength,length));row=dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(),cdb=cdb.hex(),requested_bytes=length,returned_bytes=s.DataTransferLength,ioctl_ok=bool(ok),winerror=error,scsi_status=s.ScsiStatus,sense=bytes(packet.sense).hex(),sha256=hashlib.sha256(data).hexdigest())
   self.trace.append(row)
   if not ok:raise OSError(error,'SCSI read IOCTL failed')
   return data,row
  finally:kernel.VirtualFree(memory,0,0x8000)
 def read(self,lba,count):
  if not 0<=lba<=0xFFFFFFFF or not 1<=count<=128:raise ValueError('Invalid bounded READ(10)')
  return self.command(bytes([0x28,0])+struct.pack('>I',lba)+bytes([0])+struct.pack('>H',count)+bytes([0]),count*512)
def main():
 session=ReadSession();report=dict(target=session.target,sd_blocks=session.blocks,source='actual Windows SCSI class-driver pass-through',writes_sent=0,tests=[])
 try:
  capacity,row=session.command(bytes([0x25])+bytes(9),8);assert row['scsi_status']==0 and len(capacity)==8
  last,sector=struct.unpack('>II',capacity);assert last+1==session.blocks and sector==512,'Hardware/USB geometry mismatch'
  mode,row=session.command(bytes([0x1A,8,0x3F,0,192,0]),192);assert row['scsi_status']==0 and len(mode)>=4 and mode[2]&0x80,'MODE SENSE WP absent'
  plan=[('lba0',0,1),('ordinary',1,1),('double',2,2),('three_blocks',0,3),('continuous_32KiB',0,64),('cross_32KiB',63,4),('tail_pair',last-1,2),('last_lba',last,1)]
  for name,lba,count in plan:
   data,row=session.read(lba,count);assert row['scsi_status']==0 and len(data)==count*512
   if lba==0 and name=='lba0':(OUT/'lba0.bin').write_bytes(data)
   report['tests'].append(dict(name=name,lba=lba,blocks=count,passed=True,expected_backend_requests=(count+1)//2,sha256=row['sha256']));print(name,'PASS',flush=True)
  if last<0xFFFFFFFF:
   data,row=session.read(last+1,1);sense=bytes.fromhex(row['sense']);assert row['scsi_status']!=0 and len(sense)>12 and sense[2]&15==5 and sense[12]==0x21
   report['tests'].append(dict(name='out_of_bounds',lba=last+1,passed=True,scsi_status=row['scsi_status'],sense=row['sense']))
 finally:
  report['commands']=session.trace;(OUT/'sector_read_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');session.close()
if __name__=='__main__':
 try:
  main()
  (OUT/'read_process_done.json').write_text(json.dumps(dict(passed=True)),encoding='utf-8')
 except Exception as error:
  import traceback
  (OUT/'read_process_done.json').write_text(json.dumps(dict(passed=False,error=repr(error),traceback=traceback.format_exc()),indent=2),encoding='utf-8')
  raise
