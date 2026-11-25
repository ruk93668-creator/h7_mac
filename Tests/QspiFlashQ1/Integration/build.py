"""Build frozen main and the whitelisted candidate, never download a probe."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
BASE = '62b42d7c0b7df9eadafa00953ee8ce1229da9826'
QSPI = '3a5c942dfd1420788102b11cc5271b188f319bf2'
FILES = ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h',
         'CM7/Core/Inc/stm32h7xx_hal_conf.h', 'MDK-ARM/h7_mac.uvprojx']
sha = lambda b: hashlib.sha256(b).hexdigest()


def archive(revision, destination, paths=()):
    data = subprocess.check_output(['git', 'archive', revision, *paths], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(destination, filter='data')


def configure(project, jpeg, test=False):
    raw = project.read_bytes()
    a = raw.index(b'<TargetName>h7_mac_CM7</TargetName>')
    b = raw.index(b'</Target>', a)
    target = raw[a:b]
    # Only the CM7 C compiler's product mode changes in disposable projects.
    c = target.index(b'<Cads>')
    d = target.index(b'</Cads>', c)
    compiler = target[c:d]
    start = compiler.index(b'<Define>') + len(b'<Define>')
    end = compiler.index(b'</Define>', start)
    defines = compiler[start:end]
    defines = re.sub(rb',?APP_VIDEO_MJPEG_MODE=[01]', b'', defines)
    defines += b',APP_VIDEO_MJPEG_MODE=' + (b'1' if jpeg else b'0')
    compiler = compiler[:start] + defines + compiler[end:]
    target = target[:c] + compiler + target[d:]
    if test:
        i = target.index(b'</Groups>')
        group = b'''<Group><GroupName>Isolated QSPI coexistence</GroupName><Files>
<File><FileName>coexistence.c</FileName><FileType>1</FileType>
<FilePath>..\\Tests\\QspiFlashQ1\\Integration\\coexistence.c</FilePath></File>
</Files></Group>'''
        target = target[:i] + group + target[i:]
    project.write_bytes(raw[:a] + target + raw[b:])


def build(tree, target, result_dir):
    from elftools.elf.elffile import ELFFile
    result_dir.mkdir(parents=True, exist_ok=False)
    log = result_dir / 'rebuild.log'
    project = tree / 'MDK-ARM/h7_mac.uvprojx'
    command = ['D:/32/UV4/UV4.exe', '-j2', '-r', str(project), '-t', target, '-o', str(log)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    code = subprocess.run(command, cwd=tree, startupinfo=startup,
                          creationflags=subprocess.CREATE_NO_WINDOW).returncode
    raw = log.read_bytes()
    status = re.findall(rb'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)
    if code != 0 or status[-1:] != [(b'0', b'0')]:
        raise RuntimeError(str(log) + '\n' + raw.decode(errors='replace')[-5000:])
    sizes = list(map(int, re.search(rb'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', raw).groups()))
    artifacts = tree / 'MDK-ARM' / target
    for suffix in ('axf', 'hex', 'map', 'htm'):
        source = artifacts / (target + '.' + suffix)
        if source.exists():
            shutil.copyfile(source, result_dir / source.name)
    axf = result_dir / (target + '.axf')
    with axf.open('rb') as stream:
        elf = ELFFile(stream)
        names = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        segments = [(s['p_paddr'], s.data()) for s in elf.iter_segments()
                    if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
    image_hash = sha(b''.join(address.to_bytes(4, 'little') + data for address, data in segments))
    stack = re.search(r'Maximum Stack Usage =\s*(\d+)', (result_dir / (target + '.htm')).read_text(errors='replace'))
    result = dict(target=target, sizes=sizes, rom=sizes[0] + sizes[1] + sizes[2],
                  ram=sizes[2] + sizes[3], maximum_static_stack=int(stack[1]), errors=0, warnings=0,
                  load_image_sha256=image_hash,
                  loads=[[a, a + len(d)] for a, d in segments],
                  symbols={k: v for k, v in names.items() if k.startswith(('BSP_QSPI_', 'g_qspi_coexist', 'Coexist_'))},
                  artifacts={p.name: sha(p.read_bytes()) for p in result_dir.iterdir() if p.is_file()})
    (result_dir / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(directory=str(result_dir), **result)), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = {}
    for variant in ('baseline', 'candidate'):
        tree = output / (variant + '_tree')
        tree.mkdir()
        archive(BASE, tree)
        if variant == 'candidate':
            for path in FILES + ['Tests/QspiFlashQ1/Integration/coexistence.c']:
                dst = tree / path
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / path, dst)
        project = tree / 'MDK-ARM/h7_mac.uvprojx'
        original = project.read_bytes()
        configure(project, True)
        results[variant + '_jpeg'] = build(tree, 'h7_mac_CM7', output / (variant + '_jpeg'))
        results[variant + '_cm4'] = build(tree, 'h7_mac_CM4', output / (variant + '_cm4'))
        project.write_bytes(original)
        configure(project, False)
        results[variant + '_raw'] = build(tree, 'h7_mac_CM7', output / (variant + '_raw'))
        if variant == 'candidate':
            project.write_bytes(original)
            configure(project, True, test=True)
            results['isolated_test'] = build(tree, 'h7_mac_CM7', output / 'isolated_test')
    checks = {}
    for mode in ('jpeg', 'raw', 'cm4'):
        checks[mode + '_image_equal'] = results['baseline_' + mode]['load_image_sha256'] == results['candidate_' + mode]['load_image_sha256']
        assert not results['candidate_' + mode]['symbols'], mode
    assert 'g_qspi_coexist_arm' in results['isolated_test']['symbols']
    assert 'BSP_QSPI_Read' in results['isolated_test']['symbols']
    report = dict(baseline=BASE, qspi_input=QSPI, production_sha256={f: sha((ROOT/f).read_bytes()) for f in FILES},
                  checks=checks, results=results, hardware='NOT_RUN')
    (output / 'build_report.json').write_text(json.dumps(report, indent=2) + '\n')
    if not all(checks.values()):
        raise RuntimeError('Unexpected product load-image changes; investigate before acceptance')


if __name__ == '__main__':
    main()
