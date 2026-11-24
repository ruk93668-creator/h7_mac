"""Validate exported H2 RAM evidence offline. No probe or download support."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from h1_backup import from_ihex

ROOT = Path(__file__).resolve().parents[3]
BASE = 'a897daaca069e1bccad6f1b91aabf09b35a4eb65'
LAYOUT = [0x51344832, 1, 4872, 312, 456, 16, 112, 300, 408, 432, 160, 188]
PHASES = ('BOOT', 'WAIT_FAULT', 'INIT', 'NORMAL', 'INJECT', 'FAULT',
          'WAIT_RECOVER', 'RECOVER', 'AFTER', 'DONE')
HEADER = ('signature version size phase complete result init id_result status_result fault_read '
          'blocked_init blocked_read recover repeat_recover recovered_id_result id recovered_id consumed '
          'injected denied completed blocked_completed rx_original rx_returned rx_error rx_size '
          'rx_remaining rx_state rx_tick cleanup_calls cleanup_hal cleanup_state cleanup_cr cleanup_sr '
          'recover_entry_tick recover_entry_tim_count restored before_valid after_valid quad_valid').split()
FAULT = ('origin stage latched primary cleanup last_cleanup cleanup_stage last_cleanup_stage '
         'cleanup_attempted cleanup_hal last_cleanup_hal address opcode hal_status hal_error hal_state '
         'unknown wren mutation attempts recovery_result recovery_stage recovery_elapsed controller_idle '
         'flash_idle confirmed_address confirmed_bytes confirmed_opcode').split()
RESOURCES = ('tick counter cr1 dier tim_sr irq masks gate mux gpio_gates qspi_cr qspi_sr owned').split()
SIGNED = {'result', 'init', 'id_result', 'status_result', 'fault_read', 'blocked_init', 'blocked_read',
          'recover', 'repeat_recover', 'recovered_id_result', 'primary', 'cleanup', 'last_cleanup', 'recovery_result'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def words(data, offset, count):
    return struct.unpack_from(f'<{count}I', data, offset)


def named(names, values):
    return {name: (value - 0x100000000 if name in SIGNED and value & 0x80000000 else value)
            for name, value in zip(names, values, strict=True)}


def fields(data, offset, names):
    return named(names, words(data, offset, len(names)))


def read_export(path, location):
    if path.suffix.lower() == '.hex':
        return from_ihex(path.read_text(encoding='ascii'), location['address'], location['size'])
    data = path.read_bytes()
    require(len(data) == location['size'], f'{path.name}: incorrect export size')
    return data


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode(report, layout):
    require(layout == LAYOUT and len(report) == layout[2], 'Unexpected H2 report ABI')
    header = fields(report, 0, HEADER)
    header['mismatch_address'] = words(report, layout[11]-4, 1)[0]
    require([header[k] for k in ('signature', 'version', 'size')] == layout[:3], 'Wrong capture identity')
    require(header['phase'] < len(PHASES) and header['complete'] in (0, 1), 'Malformed report phase')
    require((header['phase'] == 9) == bool(header['complete']), 'Report captured during publication')
    stages = {}
    for i, phase in enumerate(PHASES):
        base = layout[3] + i * layout[4]
        valid, result, diagnostics, fault_result = struct.unpack_from('<Iiii', report, base)
        require(valid in (0, 1), 'Invalid snapshot marker')
        if not valid:
            continue
        reg_start = base + layout[5]
        diag = words(report, base + layout[6], 47)
        stages[phase] = dict(
            result=result, diagnostics_result=diagnostics, fault_result=fault_result,
            resources=fields(report, reg_start, RESOURCES),
            pins=words(report, reg_start + len(RESOURCES)*4, 11),
            fault=named(FAULT, struct.unpack_from('<B3x5IBB2x13IB3x6I', report, base + layout[7])),
            ready=diag[1], fault_flag=diag[2], init_attempts=diag[3], serial_hz=diag[13],
            jedec=diag[21], valid_status=diag[36], busy=diag[43], wel=diag[44], sus=diag[46],
            commands=words(report, base + layout[8], 6),
            received=words(report, base + layout[9], 6),
        )
    return header, stages


def delta(new, old, name):
    return tuple(a-b for a, b in zip(new[name], old[name], strict=True))


def verify(report, layout, before=None, after=None, reference=None):
    header, stage = decode(report, layout)
    result = dict(phase=PHASES[header['phase']], report_sha256=sha(report), header=header, stages=stage,
                  status='PENDING' if not header['complete'] else 'FAIL')
    if not header['complete'] or header['result'] != 0:
        return result
    require(set(stage) == set(PHASES), 'Missing a successful-stage snapshot')
    require(all(stage[k]['result'] == 0 for k in PHASES if k != 'INJECT') and
            stage['INJECT']['result'] == -9, 'Stage result does not match the nominal scenario')
    require(stage['INIT']['ready'] == 1 and stage['INIT']['serial_hz'] == 120000000 and
            header['id'] == header['recovered_id'] == 0xEF4018, 'Unexpected clock or JEDEC')
    status = words(report, layout[11], 7)
    require(status[3] == status[4] == status[6] == 0, 'Initial status is unsafe')
    require(header['init'] == header['id_result'] == header['status_result'] == header['recover'] ==
            header['recovered_id_result'] == 0 and header['repeat_recover'] == -3, 'Unexpected API result')
    require((header['consumed'], header['injected'], header['denied']) == (3, 1, 0),
            'Unexpected gate or injection counters')
    require((header['rx_original'], header['rx_returned'], header['rx_error'], header['rx_size'],
             header['rx_remaining'], header['rx_state']) == (0, 1, 0, 16, 0, 1),
            'No proof of a completed original HAL receive and a single injected error')
    require((header['fault_read'], header['completed'], header['blocked_init'], header['blocked_read'],
             header['blocked_completed']) == (-9, 0, -4, -4, 0), 'Unexpected fault-path API result')
    fault, recovery, done = stage['FAULT'], stage['RECOVER'], stage['DONE']
    old = fault['fault']
    require((old['origin'], old['stage'], old['latched'], old['primary'], old['opcode'], old['hal_status']) ==
            (4, 3, 1, -9, 0x0B, 1) and fault['ready'] == 0 and fault['fault_flag'] == 1,
            'Expected read fault not locked')
    require((old['wren'], old['mutation'], old['unknown'], old['attempts']) == (0, 0, 0, 0),
            'Unexpected Flash write or recovery history')
    require((old['cleanup'], old['cleanup_stage'], old['controller_idle'], fault['resources']['gate']) ==
            (0, 7, 1, 0) and fault['resources']['owned'] == 0 and
            header['restored'] == 1 and header['cleanup_calls'] == 1 and
            header['cleanup_hal'] == header['cleanup_state'] == 0 and
            header['cleanup_cr'] & 0x1F0007 == 0 and header['cleanup_sr'] & 0x20 == 0,
            'MCU cleanup readback is incomplete')
    initial = layout[11] + 28
    require(words(report, initial+32, 1)[0] == fault['resources']['mux'] and
            words(report, initial+len(RESOURCES)*4, 11) == fault['pins'],
            'Owned pins or QSPI mux differ from baseline')
    require(stage['WAIT_FAULT']['commands'] == (0,)*6 and stage['WAIT_FAULT']['received'] == (0,)*6 and
            delta(fault, stage['INJECT'], 'commands') == (0,)*6, 'Unexpected commands before authorization or while locked')
    require(delta(recovery, fault, 'commands') == delta(recovery, fault, 'received') ==
            (1, 1, 1, 1, 0, 0), 'Recover did not complete JEDEC plus all three status transactions')
    require(recovery['ready'] == 1 and recovery['fault_flag'] == 0 and recovery['valid_status'] == 7 and
            recovery['busy'] == recovery['wel'] == recovery['sus'] == 0 and recovery['jedec'] == 0xEF4018 and
            recovery['resources']['gate'] == recovery['resources']['owned'] == 1 and
            recovery['init_attempts'] == stage['INIT']['init_attempts']+1,
            'READY or reinitialization proof incomplete')
    f = recovery['fault']
    require((f['origin'], f['primary'], f['latched'], f['attempts'], f['recovery_result'],
             f['recovery_stage'], f['controller_idle'], f['flash_idle']) == (4, -9, 0, 1, 0, 6, 1, 1),
            'Recovery state or retained primary failure differs')
    elapsed = (recovery['resources']['tick'] - header['recover_entry_tick']) & 0xFFFFFFFF
    require(elapsed > 0 and f['recovery_elapsed'] > 0 and recovery['resources']['cr1'] & 1 and
            recovery['resources']['dier'] & 1 and recovery['resources']['irq'] & 0x100,
            'Tick progression or TIM6 IRQ proof missing')
    require(delta(stage['AFTER'], recovery, 'received') == (0, 4, 4, 4, 32, 32) and
            delta(done, stage['AFTER'], 'commands') == (0,)*6, 'Readback or duplicate-Recover traffic differs')
    require(header['before_valid'] == header['after_valid'] == header['quad_valid'] == 3 and
            header['mismatch_address'] == 0xFFFFFFFF, 'Both sectors and all three comparisons are required')
    require(before is not None and after is not None and len(before) == len(after) == 8192,
            'Complete successful runs require before and after RAM exports')
    crc = words(report, layout[10], 6)
    sectors = []
    for i in range(2):
        lo = i*4096
        original, restored = before[lo:lo+4096], after[lo:lo+4096]
        require(original == restored and zlib.crc32(original) == crc[i] == crc[i+2] == crc[i+4],
                f'Sector {i} mismatch or incorrect CRC')
        comparison = None if reference is None else original == reference[i]
        require(comparison is not False, f'Sector {i} differs from the supplied H1 reference')
        sectors.append(dict(address=f'0x{0xFFE000+lo:08X}', before_after_match=True,
                            flash_quad_crc32=f'{crc[i+4]:08x}', sha256=sha(original),
                            all_ff=all(b == 255 for b in original), h1_reference_match=comparison))
    result.update(status='PASS', recover_tick_delta=elapsed, sectors=sectors)
    return result


def h1_reference(directory):
    metadata = json.loads((directory / 'backup.json').read_text(encoding='utf-8'))
    reference = []
    for name, address in (('neighbor', 0xFFE000), ('target', 0xFFF000)):
        data = (directory / f'{name}.bin').read_bytes()
        entry = metadata[name]
        saved_address = entry['address']
        if isinstance(saved_address, str):
            saved_address = int(saved_address, 0)
        require(saved_address == address and len(data) == entry['bytes'] == 4096 and
                sha(data) == entry['sha256'], f'Invalid H1 {name} archive')
        reference.append(data)
    return reference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--axf', type=Path, help='CM7 AXF if stored outside its original build path')
    parser.add_argument('--report', type=Path, required=True, help='Exact RAM export, BIN or Keil Intel HEX')
    parser.add_argument('--before', type=Path)
    parser.add_argument('--after', type=Path)
    parser.add_argument('--h1', type=Path, help='Optional H1 reference archive for the same board')
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_bytes())
        require(manifest['baseline'] == BASE, 'Wrong build baseline')
        image = next(row for row in manifest['images'] if row['core'] == 'CM7')
        axf = args.axf or ROOT / image['artifacts']['axf']['path']
        require(sha(axf.read_bytes()) == image['artifacts']['axf']['sha256'], 'CM7 AXF hash does not match')
        symbols = image['ram_symbols']
        read = lambda name, path: read_export(path, symbols['g_qspi_h2_' + name]) if path else None
        result = verify(read('report', args.report), image['report_layout'],
                        read('before', args.before), read('after', args.after),
                        h1_reference(args.h1) if args.h1 else None)
        result['cm7_axf_sha256'] = image['artifacts']['axf']['sha256']
        print(json.dumps(result, indent=2))
        return 0 if result['status'] == 'PASS' else (2 if result['status'] == 'PENDING' else 1)
    except (ValueError, OSError, KeyError, StopIteration, struct.error) as exc:
        print(f'H2 verification failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
