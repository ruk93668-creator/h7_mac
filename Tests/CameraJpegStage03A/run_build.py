"""Compatibility entry: use the one fixed Stage03B Keil project."""
from pathlib import Path
import subprocess,sys,argparse
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prepare-only',action='store_true')
p.add_argument('--variant',choices=['cm7_test','cm7_jpeg','cm4','cm7_uvc'])
p.add_argument('--retest',action='store_true',help='Compatibility flag; no extra project is generated')
p.add_argument('--isolated',action='store_true',help='Outputs always use output/build_stage03b')
a=p.parse_args()
runner=Path(__file__).resolve().parents[1]/'CameraMjpegStage03B/run_build.py'
cmd=[sys.executable,str(runner)]
if a.prepare_only:cmd+=['--prepare-only']
if a.variant:cmd+=['--targets',{'cm7_test':'CM7_TEST','cm7_jpeg':'CM7_JPEG_SINGLE','cm4':'CM4','cm7_uvc':'CM7_YUY2'}[a.variant]]
raise SystemExit(subprocess.call(cmd,cwd=Path(__file__).resolve().parents[2]))
