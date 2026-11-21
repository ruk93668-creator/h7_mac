"""Compile an isolated two-core read-only candidate; contains no download command."""
from pathlib import Path
import subprocess, tarfile, io, json, re, hashlib, sys, copy, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
BASE = 'd35c6464a9392d97f04f103bc828f59800d2c751'
label = sys.argv[1] if len(sys.argv) > 1 else 'hardware_candidate'
assert re.fullmatch(r'[A-Za-z0-9_-]+', label)
OUT = ROOT / 'output/QspiFlashQ1' / label
assert not OUT.exists(), 'Preserve evidence: do not overwrite existing candidate.'
OUT.mkdir(parents=True)
(OUT.parent / '.gitignore').write_text('*\n')
snapshot = OUT / 'source'
snapshot.mkdir()
data = subprocess.check_output(['git', 'archive', '--format=tar', BASE], cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(data)) as tf:
    tf.extractall(snapshot)
testdir = snapshot / 'Tests/QspiFlashQ1/hardware'
testdir.mkdir()
for name in ['cm7_main.c', 'cm4_main.c']:
    (testdir / name).write_bytes((HERE / name).read_bytes())
main = (snapshot / 'CM7/Core/Src/main.c').read_bytes()
start = main.index(b'void SystemClock_Config(void)\r\n{')
end = main.index(b'\r\n/* USER CODE BEGIN 4 */', start)
clock = main[start:end]
(testdir / 'clock_config.c').write_bytes(b'#include "main.h"\r\n' + clock)
project = snapshot / 'MDK-ARM/qspi_readonly.uvprojx'
tree = ET.parse(snapshot / 'MDK-ARM/h7_mac.uvprojx')
for target in tree.findall('./Targets/Target'):
    core = 'CM7' if target.findtext('TargetName') == 'h7_mac_CM7' else 'CM4'
    for group in target.findall('./Groups/Group'):
        files = group.find('Files')
        if files is None:
            continue
        for f in list(files):
            p = f.findtext('FilePath', '').replace('\\', '/')
            name = f.findtext('FileName', '')
            keep = p.startswith('../Drivers/') or p.startswith('../Common/Src/system_stm32')
            keep |= p.startswith('../' + core + '/Core/')
            keep |= name == 'startup_stm32h747xx_' + core + '.s'
            if core == 'CM7':
                keep |= p.startswith('../Middlewares/Third_Party/FreeRTOS/')
                keep |= name in ['bsp_qspi.c', 'bsp_qspi.h', 'platform_rtos.c', 'platform_hal_timebase.c']
            if not keep:
                files.remove(f)
            elif name == 'main.c':
                f.find('FileName').text = core.lower() + '_main.c'
                f.find('FilePath').text = '../Tests/QspiFlashQ1/hardware/' + core.lower() + '_main.c'
    if core == 'CM7':
        files = target.find('./Groups/Group/Files')
        f = ET.SubElement(files, 'File')
        for key, value in [('FileName', 'clock_config.c'), ('FileType', '1'),
                           ('FilePath', '../Tests/QspiFlashQ1/hardware/clock_config.c')]:
            ET.SubElement(f, key).text = value
projects = {}
for core in ['CM7', 'CM4']:
    #uVision 多 target 共享文件树；使用单 target 工程，避免缺失的排除项被合并回来。
    root = copy.deepcopy(tree.getroot())
    targets = root.find('Targets')
    for target in list(targets):
        if target.findtext('TargetName') != 'h7_mac_' + core:
            targets.remove(target)
    for infos in root.findall('.//targetInfos'):
        for info in list(infos):
            if info.get('name') != 'h7_mac_' + core:
                infos.remove(info)
    for name in ['UpdateFlashBeforeDebugging', 'LoadApplicationAtStartup', 'RunToMain']:
        for node in root.iter(name):
            node.text = '0'
    #测试不包含 Initcall/IPC/SDMMC；使用测试专用 Scatter，不改生产 Scatter 或压制警告。
    scatter_name = 'qspi_test_' + core + '.sct'
    (snapshot / 'MDK-ARM' / scatter_name).write_bytes((HERE / (core.lower() + '_test.sct')).read_bytes())
    ld = root.find('./Targets/Target/TargetOption/TargetArmAds/LDads')
    ld.find('ScatterFile').text = './' + scatter_name
    ld.find('Misc').text = ''
    path = snapshot / 'MDK-ARM' / ('qspi_readonly_' + core + '.uvprojx')
    ET.ElementTree(root).write(path, encoding='utf-8', xml_declaration=True)
    projects[core] = path

results = []
for core in ['CM7', 'CM4']:
    target = 'h7_mac_' + core
    log = OUT / (core + '_build.log')
    cmd = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(projects[core]), '-t', target, '-o', str(log)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.run(cmd, cwd=snapshot, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    raw = log.read_bytes() if log.exists() else b''
    text = raw.decode('utf-8', errors='replace')
    counts = re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)', text)
    row = dict(core=core, exit=process.returncode, counts=list(map(int, counts.groups())) if counts else None)
    row['pass'] = row['exit'] == 0 and row['counts'] == [0, 0]
    print(row, flush=True)
    results.append(row)
    if not row['pass']:
        print(text[-2500:], flush=True)
(OUT / 'build_results.json').write_text(json.dumps(dict(baseline=BASE, rows=results,
    production_clock_function_sha256=hashlib.sha256(clock).hexdigest()), indent=2))
assert all(r['pass'] for r in results)

# 独立检查 Intel HEX 每条数据记录范围与校验和，禁止外部 QSPI 地址。
manifest = []
for core, lo in [('CM7', 0x08000000), ('CM4', 0x08100000)]:
    name = 'h7_mac_' + core
    image = snapshot / 'MDK-ARM' / name / (name + '.hex')
    if not image.exists():
        subprocess.run(['D:/32/ARM/ARMCC/Bin/fromelf.exe', '--i32combined', '--output', str(image),
                        str(image.with_suffix('.axf'))], check=True)
    segments, upper = [], 0
    for line in image.read_text().splitlines():
        rec = bytes.fromhex(line[1:])
        assert sum(rec) % 256 == 0 and len(rec) == rec[0] + 5
        count, kind = rec[0], rec[3]
        offset = int.from_bytes(rec[1:3], 'big')
        if kind == 4:
            upper = int.from_bytes(rec[4:6], 'big') << 16
        elif kind == 2:
            upper = int.from_bytes(rec[4:6], 'big') << 4
        elif kind == 0 and count:
            addr = upper + offset
            assert lo <= addr < addr + count <= lo + 0x100000
            segments.append((addr, addr + count))
        else:
            assert kind in [1, 3, 5]
    assert segments
    first, last = min(a for a, _ in segments), max(b for _, b in segments)
    assert last <= lo + 0x20000, 'Re-audit sector proposal for larger image.'
    manifest.append(dict(core=core, hex_path=str(image.relative_to(ROOT)),
        sha256=hashlib.sha256(image.read_bytes()).hexdigest(), start=hex(first), end_exclusive=hex(last),
        internal_flash_bank_start=hex(lo), bytes=sum(b-a for a,b in segments),
        proposed_sector_start=hex(lo), proposed_sector_end_exclusive=hex(lo + 0x20000)))
(OUT / 'download_scope.json').write_text(json.dumps(dict(execution_authorized=False, images=manifest), indent=2))
print(json.dumps(manifest, indent=2))
