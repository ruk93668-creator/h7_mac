"""Offline H1 capture verification and RAM package preparation. Never contacts a probe."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

TARGET = 0x00FFF000
NEIGHBOR = 0x00FFE000
SECTOR = 4096
OFFSET = 255
CAPTURE = struct.Struct('<7Ii8s')
INPUT = struct.Struct('<8I8s')
OPS = {'program': 1, 'erase': 2, 'restore': 3}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def crc(data):
    return zlib.crc32(data)


def now():
    return datetime.now(timezone.utc).isoformat()


def json_file(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def test_pattern():
    data = bytearray.fromhex('00 FF 55 AA 0F F0 01 02 04 08 10 20 40 80')
    state = 0x74701301
    while len(data) < 520:
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        data.append(state & 0xFF)
    require(crc(data) == 0x6AFF4F17, 'Test pattern CRC disagrees with Q1.3')
    return bytes(data)


def record(kind, address, data):
    raw = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ':' + (raw + bytes([-sum(raw) & 255])).hex().upper()


def to_ihex(address, data):
    lines = []
    while data:
        lines.append(record(4, 0, (address >> 16).to_bytes(2, 'big')))
        block = min(len(data), 0x10000 - (address & 0xFFFF))
        for index in range(0, block, 16):
            lines.append(record(0, (address + index) & 0xFFFF, data[index:min(index+16, block)]))
        address += block
        data = data[block:]
    return '\n'.join(lines + [record(1, 0, b'')]) + '\n'


def from_ihex(text, address, length):
    data = {}
    base = 0
    eof = False
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        require(line.startswith(':') and not eof, 'Invalid Intel HEX record order')
        raw = bytes.fromhex(line[1:])
        require(len(raw) >= 5 and len(raw) == raw[0] + 5 and sum(raw) % 256 == 0,
                'Invalid Intel HEX length or checksum')
        offset, kind, payload = int.from_bytes(raw[1:3], 'big'), raw[3], raw[4:-1]
        if kind == 0:
            require(offset + len(payload) <= 0x10000, 'Intel HEX line crosses record address range')
            for i, value in enumerate(payload, base + offset):
                require(address <= i < address + length and i not in data, 'Unexpected or overlapping byte')
                data[i] = value
        elif kind == 1:
            require(offset == 0 and not payload, 'Invalid EOF')
            eof = True
        elif kind in (2, 4):
            require(offset == 0 and len(payload) == 2, 'Invalid extended address')
            base = int.from_bytes(payload, 'big') << (4 if kind == 2 else 16)
        else:
            raise ValueError(f'Unexpected Intel HEX record type {kind}')
    require(eof and len(data) == length, 'Export must cover the whole capture without gaps')
    return bytes(data[i] for i in range(address, address + length))


def reviewed_build(path, profile, symbol, size):
    build = json.loads(path.read_text(encoding='utf-8'))
    require(build['profile'] == profile and build['errors'] == build['warnings'] == 0,
            'Wrong or incomplete H1 build')
    require(build['data_range'] == [TARGET, TARGET + SECTOR], 'Invalid H1 target in manifest')
    axf = (path.parent / build['axf']).read_bytes()
    require(sha(axf) == build['axf_sha256'], 'AXF SHA-256 differs from build manifest')
    loc = build['symbols'][symbol]
    address = loc['address']
    require(loc['size'] == size and address % 4 == 0 and
            0x20000000 <= address <= 0x20020000 - size, 'Buffer outside expected M7 DTCM range')
    # 对照 AXF 符号，避免使用由旧工程或手工改写的 RAM 地址。
    from elftools.elf.elffile import ELFFile
    from io import BytesIO
    elf = ELFFile(BytesIO(axf))
    symbols = elf.get_section_by_name('.symtab').get_symbol_by_name(symbol) or []
    require(any(s['st_value'] == address and s['st_size'] == size for s in symbols),
            'Build manifest symbol does not match AXF')
    loads = [[seg['p_paddr'], seg['p_paddr'] + seg['p_filesz']] for seg in elf.iter_segments()
             if seg['p_type'] == 'PT_LOAD' and seg['p_filesz']]
    require(loads == build['loads'] and loads and all(0x08000000 <= a < b <= 0x08040000 for a,b in loads),
            'Image exceeds approved-review M7 aperture')
    return build, address


def unpack_capture(raw):
    require(len(raw) == CAPTURE.size + 4 * SECTOR, 'Expected exactly 16,424 capture bytes')
    tag, abi, complete, address, size, jedec, frequency, result, uid = CAPTURE.unpack_from(raw)
    require((tag, abi, complete, address, size, jedec, frequency, result) ==
            (0x31424351, 1, 1, NEIGHBOR, 2 * SECTOR, 0xEF4018, 30000000, 0),
            'Capture status, clock, identity or range does not match H1')
    require(uid not in (b'\0' * 8, b'\xff' * 8), 'Invalid W25Q unique ID')
    standard, fast = raw[CAPTURE.size:CAPTURE.size + 2 * SECTOR], raw[CAPTURE.size + 2 * SECTOR:]
    require(standard == fast, '03h and 0Bh differ; capture rejected')
    return uid, standard[SECTOR:], standard[:SECTOR]


def import_capture(args):
    length = CAPTURE.size + 4 * SECTOR
    build, address = reviewed_build(args.build, 'backup', 'g_qspi_h1_capture', length)
    source = args.export.read_bytes()
    raw = from_ihex(source.decode('ascii'), address, length) if args.export.suffix.lower() in ('.hex', '.ihex') else source
    uid, target, neighbor = unpack_capture(raw)
    read_time = datetime.fromisoformat(args.read_time.replace('Z', '+00:00'))
    require(read_time.tzinfo is not None and args.board.strip(), 'Board ID and timezone-offset read time required')
    require(re.fullmatch(r'[0-9a-fA-F]{40}', args.firmware_sha) is not None, 'Full firmware Git SHA required')
    args.out.mkdir(parents=True)
    (args.out / 'capture.bin').write_bytes(raw)
    (args.out / 'target.bin').write_bytes(target)
    (args.out / 'neighbor.bin').write_bytes(neighbor)
    metadata = dict(kind=args.kind, board=args.board, read_time=read_time.isoformat(), imported_at=now(),
        uid=uid.hex(), jedec='EF4018', firmware_sha=args.firmware_sha.lower(),
        axf_sha256=build['axf_sha256'], input_export_sha256=sha(source), capture_sha256=sha(raw),
        target=dict(address=TARGET, bytes=SECTOR, sha256=sha(target), crc32=crc(target)),
        neighbor=dict(address=NEIGHBOR, bytes=SECTOR, sha256=sha(neighbor), crc32=crc(neighbor)))
    json_file(args.out / 'backup.json', metadata)
    print(json.dumps(metadata, indent=2, ensure_ascii=False))


def check_archive(directory):
    meta = json.loads((directory / 'backup.json').read_text(encoding='utf-8'))
    raw = (directory / 'capture.bin').read_bytes()
    uid, target, neighbor = unpack_capture(raw)
    require(meta['uid'] == uid.hex() and meta['capture_sha256'] == sha(raw), 'Backup identity mismatch')
    for name, address, data in [('target', TARGET, target), ('neighbor', NEIGHBOR, neighbor)]:
        require((directory / (name + '.bin')).read_bytes() == data, 'Backup file diverges from capture')
        require(meta[name] == dict(address=address, bytes=SECTOR, sha256=sha(data), crc32=crc(data)),
                'Backup metadata differs from the archived bytes')
    return meta, uid, target, neighbor


def build_package(op, uid, original, neighbor, current):
    pattern = test_pattern()
    require(original[OFFSET:OFFSET + len(pattern)] == b'\xff' * len(pattern),
            'Program range is not erased in the approved original; this trial cannot proceed')
    states = {'program': original,
              'erase': original[:OFFSET] + pattern + original[OFFSET + len(pattern):],
              'restore': b'\xff' * SECTOR}
    require(current == states[op], 'Current capture does not match the required operation state')
    return INPUT.pack(0x31495751, 1, OPS[op], TARGET, SECTOR, crc(original), crc(neighbor),
                      crc(current), uid) + original + neighbor + current


def prepare_package(args):
    length = INPUT.size + 3 * SECTOR
    build, address = reviewed_build(args.build, 'write', 'g_qspi_h1_input', length)
    require(args.execution_approval.strip(), 'Separate execution approval reference required')
    original, uid, data, neighbor = check_archive(args.original)
    current, current_uid, observed, observed_neighbor = check_archive(args.current)
    expected_kind = {'program': 'original', 'erase': 'programmed', 'restore': 'erased'}
    require(original['kind'] == 'original' and current['kind'] == expected_kind[args.operation],
            'Archive kind is incorrect for this operation')
    require(original['board'] == current['board'] and uid == current_uid and neighbor == observed_neighbor,
            'Board identity or neighbor contents differ')
    require(datetime.fromisoformat(current['read_time']) >= datetime.fromisoformat(original['read_time']),
            'Current capture predates the original')
    package = build_package(args.operation, uid, data, neighbor, observed)
    args.out.mkdir(parents=True)
    (args.out / 'ram_input.bin').write_bytes(package)
    (args.out / 'ram_input.hex').write_text(to_ihex(address, package), encoding='ascii')
    summary = dict(operation=args.operation, board=original['board'], uid=uid.hex(), prepared_at=now(),
        execution_approval_reference=args.execution_approval, write_axf_sha256=build['axf_sha256'],
        ram_address=address, ram_bytes=len(package), package_sha256=sha(package),
        original_sha256=sha(data), current_sha256=sha(observed), neighbor_sha256=sha(neighbor))
    json_file(args.out / 'package.json', summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='subcommand', required=True)
    capture = sub.add_parser('import-capture', help='Validate a read-only Keil RAM export and split backups')
    package = sub.add_parser('prepare-package', help='Generate an offline, operation-specific RAM package')
    for cmd in (capture, package):
        cmd.add_argument('--build', type=Path, required=True)
        cmd.add_argument('--out', type=Path, required=True)
    capture.add_argument('--export', type=Path, required=True)
    capture.add_argument('--board', required=True)
    capture.add_argument('--read-time', required=True)
    capture.add_argument('--firmware-sha', required=True)
    capture.add_argument('--kind', choices=('original', 'programmed', 'erased', 'restored'), required=True)
    package.add_argument('--original', type=Path, required=True)
    package.add_argument('--current', type=Path, required=True)
    package.add_argument('--operation', choices=tuple(OPS), required=True)
    package.add_argument('--execution-approval', required=True)
    args = parser.parse_args()
    try:
        (import_capture if args.subcommand == 'import-capture' else prepare_package)(args)
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.exit(1, f'H1: {exc}\n')


if __name__ == '__main__':
    main()
