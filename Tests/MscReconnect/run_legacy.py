"""Compare historical UVC assertions against the task baseline without editing them."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
original=ROOT/'Tests/MscJoint/run_legacy.py'
(ROOT/'output/MSC41/legacy').mkdir(parents=True,exist_ok=True)
for baseline in (True,False):
    source=original.read_text(encoding='utf-8')
    source=source.replace('14f44ca678647421d3519f192d441724178a7882','8691497062f1459592decd7ea154c4fc6151c1c9')
    source=source.replace('output/MSC24','output/MSC41/legacy')
    source=source.replace('Tests/MscJoint/','Tests/MscReconnect/')
    sys.argv=[str(original)]+(['--baseline'] if baseline else [])
    try:
        exec(compile(source,str(original),'exec'),{'__file__':str(original),'__name__':'__main__'})
    except SystemExit as error:
        print('BASELINE' if baseline else 'CURRENT','exit',error.code,flush=True)
