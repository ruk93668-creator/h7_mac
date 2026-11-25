"""Run original suites in disposable trees; never patch V3 product sources."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

from build import archive, FILES, ROOT, QSPI


def run(command, cwd, log, environment):
    result = subprocess.run(command, cwd=cwd, env=environment, capture_output=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_bytes(result.stdout + result.stderr)
    print(json.dumps(dict(command=list(map(str, command)), code=result.returncode, log=str(log))), flush=True)
    return dict(command=list(map(str, command)), code=result.returncode, log=str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--group', choices=('bsp', 'v3'), required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    tree = output/'tree'
    tree.mkdir()
    archive('62b42d7c0b7df9eadafa00953ee8ce1229da9826', tree)
    for f in FILES:
        destination=tree/f
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/f, destination)
    env = dict(os.environ)
    rows=[]
    if args.group == 'bsp':
        archive(QSPI, tree, ['Tests/QspiFlashQ1'])
        commands=[['run_tests.py'], ['run_read_tests.py'], ['run_write_tests.py'],
                  ['run_q14_tests.py'], ['run_q14_r1_tests.py','--r2'],
                  ['run_hal_link.py'], ['run_q13_hal.py'], ['run_q14_hal.py'],
                  ['h2/run_mock.py'], ['h2/run_hal.py'],
                  ['h2/test_verify_report.py', str(tree/'Tests/QspiFlashQ1/h2/evidence/download_scope.json'),
                   str(tree/'output/QspiFlashQ1/h2/mock')],
                  ['test_h1_backup.py']]
        for i, command in enumerate(commands):
            rows.append(run(['python', str(tree/'Tests/QspiFlashQ1'/command[0]), *command[1:]],
                            tree, output/f'core_{i:02d}.log',env))
        # Historical 30 MHz/H1 modes require their old compile-time entry seams.
        # Reconstitute only the removed prologue/epilogue around the exact candidate body.
        candidate=(tree/'CM7/BSP/bsp_qspi.c').read_text(encoding='utf-8')
        old=subprocess.check_output(['git','show',QSPI+':CM7/BSP/bsp_qspi.c'],cwd=ROOT).decode('utf-8')
        start=old.index('#if defined(QSPI_H1_BACKUP)')
        end=old.index('//首次 HAL Init')
        tail=old[old.rindex('#if defined(QSPI_H1_BACKUP)'):]
        first=candidate.index('//产品固定')
        last=candidate.index('//首次 HAL Init')
        overlay=candidate[:first]+old[start:end]+candidate[last:]+tail
        (tree/'CM7/BSP/bsp_qspi.c').write_text(overlay,encoding='utf-8')
        commands=[['run_sfdp_tests.py'], ['run_quad_id_tests.py','30'], ['run_quad_id_tests.py','120'],
                  ['run_h1_tests.py','backup'], ['run_h1_tests.py','backup120'], ['run_h1_tests.py','write']]
        for i, command in enumerate(commands):
            rows.append(run(['python',str(tree/'Tests/QspiFlashQ1'/command[0]),*command[1:]],
                            tree,output/f'diagnostic_overlay_{i:02d}.log',env))
    else:
        # Real product translation units: do not apply the historical board patch here.
        commands=[['Tests/ApplicationV3/run_tests.py','--production'],
                  ['Tests/ApplicationV3/run_tests.py','--production','--raw-uvc'],
                  ['Tests/ApplicationV3/MediaLease/run_tests.py'],
                  ['Tests/ApplicationV3/FrameDispatcher/run_tests.py'],
                  ['Tests/ApplicationV3/UvcReader/run_tests.py'],
                  ['Tests/ApplicationV3/StorageFs/run_tests.py']]
        for i, command in enumerate(commands):
            rows.append(run(['python',command[0],str(output/f'product_{i:02d}'),*command[1:]],
                            tree,output/f'product_{i:02d}.log',env))
        # Historical startup/smoke/joint seams are confined to another disposable tree.
        env['GIT_CEILING_DIRECTORIES']=str(output)
        spec=importlib.util.spec_from_file_location('profiles',tree/'Tests/ApplicationV3/BoardProfiles/prepare.py')
        profiles=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(profiles)
        profiles.ROOT=tree
        historical=output/'historical_tree'
        profiles.prepare(historical)
        rows.append(run(['python','Tests/ApplicationV3/run_usb_platform_trace.py',str(output/'usb_trace_historical')],
                        historical,output/'usb_trace_historical.log',env))
        options=[[],['--raw-uvc'],['--joint-board'],['--record-board']]
        for module in ('camera','usb','storage'):
            options.append(['--board-smoke',module])
            options.append(['--board-smoke',module,'--board-stress','repeat'])
            if module!='camera':options.append(['--board-smoke',module,'--board-stress','recovery'])
        for i, option in enumerate(options):
            rows.append(run(['python','Tests/ApplicationV3/run_tests.py',str(output/f'historical_{i:02d}'),*option],
                            historical,output/f'historical_{i:02d}.log',env))
    (output/'runs.json').write_text(json.dumps(rows,indent=2)+'\n')
    raise SystemExit(any(r['code'] for r in rows))


if __name__=='__main__':
    main()
