"""Bounded READ-only loop for user-controlled USB data disconnect; no write API."""
from windows_read import ReadSession,OUT
import time,json,datetime
session=None;report=dict(started=datetime.datetime.now(datetime.timezone.utc).isoformat(),completed_reads=0,expected_operation='disconnect USB data while READ is active',writes_sent=0)
try:
 session=ReadSession();deadline=time.monotonic()+180
 while time.monotonic()<deadline:
  (OUT/'active_read_progress.json').write_text(json.dumps(dict(command_active=True,completed_reads=report['completed_reads'],at=datetime.datetime.now(datetime.timezone.utc).isoformat())),encoding='utf-8')
  data,row=session.read(2048+(report['completed_reads']%16)*128,128)
  if row['scsi_status']!=0 or len(data)!=65536:raise RuntimeError('READ did not complete normally')
  report['completed_reads']+=1
 report['ended_by']='bounded time limit without disconnect'
except Exception as error:report['ended_by']='read error/disconnect';report['error']=repr(error)
finally:
 if session:report['last_commands']=session.trace[-4:];session.close()
 report['ended']=datetime.datetime.now(datetime.timezone.utc).isoformat();(OUT/'active_disconnect_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 (OUT/'active_read_progress.json').write_text(json.dumps(dict(command_active=False,completed_reads=report['completed_reads'],ended_by=report.get('ended_by'))),encoding='utf-8')
