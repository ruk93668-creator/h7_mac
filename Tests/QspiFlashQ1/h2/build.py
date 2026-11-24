"""Build and audit an isolated H2 pair. This program only builds local files."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import xml.etree.ElementTree as ET

from elftools.elf.elffile import ELFFile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = 'a897daaca069e1bccad6f1b91aabf09b35a4eb65'
H2 = 'Tests/QspiFlashQ1/h2'
LEGACY = 'Tests/QspiFlashQ1/hardware'
BSP = 'CM7/BSP/bsp_qspi.c'
SCATTER = {'CM7': (0x08000000, 0x20000000, 0x20020000), 'CM4': (0x08100000, 0x10000000, 0x10048000)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hex_ranges(path, first, limit):
    """Validate Intel HEX framing, checksums, address types, and unique data bytes."""
    upper, eof = 0, False
    ranges = []
    for line in path.read_text(encoding='ascii').splitlines():
        if eof or not re.fullmatch(r':[0-9a-fA-F]+', line):
            raise ValueError('Invalid Intel HEX framing or record after EOF')
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError('Invalid Intel HEX length or checksum')
        length, address, kind = record[0], int.from_bytes(record[1:3], 'big'), record[3]
        if kind == 0:
            start = upper + address
            if not length or address + length > 0x10000 or not first <= start < start + length <= limit:
                raise ValueError('Data lies outside the permitted internal Flash bank')
            ranges.append((start, start + length))
        elif kind in (2, 4) and length == 2 and address == 0:
            upper = int.from_bytes(record[4:6], 'big') << (4 if kind == 2 else 16)
        elif kind in (3, 5) and length == 4 and address == 0:
            continue
        elif kind == 1 and length == 0 and address == 0:
            eof = True
        else:
            raise ValueError('Unsupported or malformed Intel HEX record')
    if not eof or not ranges:
        raise ValueError('Missing data or EOF')
    merged = []
    for start, stop in sorted(ranges):
        if merged and start < merged[-1][1]:
            raise ValueError('Overlapping Intel HEX data')
        if merged and start == merged[-1][1]:
            merged[-1][1] = stop
        else:
            merged.append([start, stop])
    return merged


def project_for(snapshot, core):
    project = snapshot / 'MDK-ARM/h7_mac.uvprojx'
    tree = ET.parse(project)
    root = tree.getroot()
    targets = root.find('Targets')
    name = 'h7_mac_' + core
    for target in list(targets):
        if target.findtext('TargetName') != name:
            targets.remove(target)
    target = targets.find('Target')
    for infos in root.findall('.//targetInfos'):
        for info in list(infos):
            if info.get('name') != name:
                infos.remove(info)
    for group in target.findall('./Groups/Group'):
        files = group.find('Files')
        if files is None:
            continue
        for file in list(files):
            path = file.findtext('FilePath', '').replace('\\', '/')
            file_name = file.findtext('FileName', '')
            keep = (path.startswith(('../Drivers/', '../Common/Src/system_stm32', f'../{core}/Core/')) or
                    file_name == f'startup_stm32h747xx_{core}.s')
            if core == 'CM7':
                keep |= path.startswith('../Middlewares/Third_Party/FreeRTOS/') or file_name in (
                    'platform_rtos.c', 'platform_hal_timebase.c')
            if not keep:
                files.remove(file)
            elif file_name == 'main.c':
                directory = H2 if core == 'CM7' else LEGACY
                file.find('FileName').text = f'{core.lower()}_main.c'
                file.find('FilePath').text = f'../{directory}/{core.lower()}_main.c'
    if core == 'CM7':
        group = ET.SubElement(target.find('Groups'), 'Group')
        ET.SubElement(group, 'GroupName').text = 'H2 test'
        files = ET.SubElement(group, 'Files')
        for file_name in ('h2.c', 'bsp_qspi_h2.c', 'clock_config.c'):
            file = ET.SubElement(files, 'File')
            for key, value in [('FileName', file_name), ('FileType', '1'),
                               ('FilePath', f'../{H2}/{file_name}')]:
                ET.SubElement(file, key).text = value
    for name in ('UpdateFlashBeforeDebugging', 'LoadApplicationAtStartup', 'RunToMain'):
        for node in root.iter(name):
            node.text = '0'
    scatter = f'qspi_h2_{core}.sct'
    shutil.copyfile(snapshot / LEGACY / f'{core.lower()}_test.sct', snapshot / 'MDK-ARM' / scatter)
    linker = target.find('./TargetOption/TargetArmAds/LDads')
    linker.find('ScatterFile').text = './' + scatter
    linker.find('Misc').text = '--xref --callgraph'
    path = snapshot / 'MDK-ARM' / f'qspi_h2_{core}.uvprojx'
    tree.write(path, encoding='utf-8', xml_declaration=True)
    return path


def audit(core, snapshot):
    name = 'h7_mac_' + core
    image = snapshot / 'MDK-ARM' / name / name
    axf, ihex, map_file = (image.with_suffix(suffix) for suffix in ('.axf', '.hex', '.map'))
    first, ram_first, ram_end = SCATTER[core]
    subprocess.run(['D:/32/ARM/ARMCC/Bin/fromelf.exe', '--i32combined', '--output', str(ihex), str(axf)], check=True)
    ranges = hex_ranges(ihex, first, first + 0x100000)
    sector_bytes = 0x20000
    sectors = sorted({sector for start, stop in ranges for sector in
                      range((start-first)//sector_bytes, (stop-1-first)//sector_bytes + 1)})
    info = dict(core=core, flash_ranges=ranges, data_bytes=sum(stop-start for start, stop in ranges),
                bank_if_unswapped=1 if core == 'CM7' else 2,
                erase_sectors=[dict(index=i, start=first+i*sector_bytes, end=first+(i+1)*sector_bytes) for i in sectors],
                artifacts={path.suffix[1:]: dict(path=str(path.relative_to(ROOT)), sha256=sha(path))
                           for path in (axf, ihex, map_file)})
    with axf.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()
                   if s['st_shndx'] != 'SHN_UNDEF' and s['st_value']}
        loads = [[s['p_paddr'], s['p_paddr'] + s['p_filesz']] for s in elf.iter_segments()
                 if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
        assert loads and all(first <= a < b <= first + 0x100000 for a, b in loads)
        assert elf.header['e_entry'] & ~1 in range(first, first + 0x100000)
        assert 'main' in symbols and 'Reset_Handler' in symbols
        forbidden = ('BSP_QSPI_Program', 'BSP_QSPI_EraseSector', 'Qspi_ProgramWithPolicy',
                     'Qspi_EraseWithPolicy', 'HAL_QSPI_Transmit', 'HAL_QSPI_MemoryMapped', 'HAL_QSPI_AutoPolling')
        assert not any(s in symbols for s in forbidden)
        assert not any(s.startswith('HAL_QSPI_') and
                       (s.endswith(('_DMA', '_IT')) or s == 'HAL_QSPI_IRQHandler') for s in symbols)
        info.update(entry=elf.header['e_entry'], main=symbols['main']['st_value'], axf_loads=loads)
        if core == 'CM7':
            required = ('H2_Step', 'H2_Command', 'H2_Receive', 'H2_DeInit', 'HAL_QSPI_Command',
                        'HAL_QSPI_Receive', 'HAL_QSPI_Abort', 'HAL_QSPI_DeInit', 'BSP_QSPI_Recover',
                        'TIM6_DAC_IRQHandler', 'SysTick_Handler', 'PendSV_Handler', 'SVC_Handler')
            assert all(s in symbols for s in required), set(required) - symbols.keys()
            names = ('g_qspi_h2_arm', 'g_qspi_h2_report', 'g_qspi_h2_before', 'g_qspi_h2_after', 's_test_stack')
            info['ram_symbols'] = {n: dict(address=symbols[n]['st_value'], size=symbols[n]['st_size']) for n in names}
            assert all(ram_first <= s['address'] < s['address'] + s['size'] <= ram_end
                       for s in info['ram_symbols'].values())
            layout = symbols['g_qspi_h2_layout']
            section = elf.get_section(layout['st_shndx'])
            offset = layout['st_value'] - section['sh_addr']
            info['report_layout'] = list(struct.unpack_from('<12I', section.data(), offset))
            assert info['report_layout'][2] == info['ram_symbols']['g_qspi_h2_report']['size']
        else:
            assert not any(s.startswith(('BSP_QSPI_', 'HAL_QSPI_', 'g_qspi_h2_')) for s in symbols)
    if core == 'CM7':
        refs = {}
        text = map_file.read_text(errors='replace')
        for func in ('HAL_QSPI_Command', 'HAL_QSPI_Receive', 'HAL_QSPI_DeInit'):
            refs[func] = [line.strip() for line in text.splitlines() if re.search(r'\bfor ' + func + r'\s*$', line)]
            assert len(refs[func]) == 1 and 'h2.o(i.H2_' in refs[func][0], refs
        info['hal_references'] = refs
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('label', help='Fresh directory name under output/QspiFlashQ1/h2')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.label):
        parser.error('Use letters, digits, underscores or hyphens for the label')
    output = ROOT / 'output/QspiFlashQ1/h2' / args.label
    output.mkdir(parents=True)
    snapshot = output / 'source'
    snapshot.mkdir()
    archive = subprocess.check_output(['git', 'archive', BASE], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(snapshot, filter='data')
    testdir = snapshot / H2
    testdir.mkdir()
    for name in ('h2.h', 'h2.c', 'bsp_qspi_h2.c', 'cm7_main.c'):
        shutil.copyfile(HERE / name, testdir / name)
    source = (snapshot / 'CM7/Core/Src/main.c').read_bytes()
    clock = re.search(rb'(?ms)^void SystemClock_Config\(void\)\r?\n\{.*?^\}', source)
    assert clock
    (testdir / 'clock_config.c').write_bytes(b'#include "main.h"\n' + clock[0] + b'\n')
    projects = {core: project_for(snapshot, core) for core in SCATTER}
    images = []
    for core, project in projects.items():
        log = output / (core + '_build.log')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(project), '-t', f'h7_mac_{core}', '-o', str(log)]
        result = subprocess.run(command, cwd=snapshot, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        raw = log.read_text(errors='replace')
        print(f'{core} exit {result.returncode}\n{raw[-800:]}', flush=True)
        assert result.returncode == 0 and re.findall(r'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)[-1:] == [('0', '0')]
        size = re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', raw)
        assert size
        info = audit(core, snapshot)
        name = 'h7_mac_' + core
        html = (snapshot / 'MDK-ARM' / name / (name + '.htm')).read_text(errors='replace')
        stack = re.search(r'Maximum Stack Usage\s*=\s*(\d+)', html)
        assert stack
        info.update(program_size=list(map(int, size.groups())), max_static_stack=int(stack[1]),
                    errors=0, warnings=0, command=command, build_log_sha256=sha(log))
        images.append(info)
    manifest = dict(baseline=BASE, execution_authorized=False, bsp_sha256=sha(snapshot / BSP),
                    clock_sha256=sha(testdir / 'clock_config.c'), images=images)
    (output / 'download_scope.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest, indent=2), flush=True)


if __name__ == '__main__':
    main()
