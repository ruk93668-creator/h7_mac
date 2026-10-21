"""Read an exported hardware frame; preserve bytes and render Y8/RGB565/YUYV PNG.

Standard library only. No image generation, alignment repair, normalization,
or interpolation is applied to the original 160x120 output.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib

WIDTH = 160
HEIGHT = 120
BASE = 0xD0000400
FRAME_SIZE = WIDTH * HEIGHT
HERE = Path(__file__).resolve().parent
PIXEL_FORMATS = {"y8": 1, "rgb565": 2, "yuv422": 2, "yuyv": 2}


def read_ihex(path, base=BASE, size=FRAME_SIZE):
    """Validate record checksums and require every requested byte; never pad."""
    memory = {}
    upper = 0
    eof = False
    records = 0
    for number, text in enumerate(path.read_text(encoding='ascii').splitlines(), 1):
        text = text.strip()
        if not text:
            continue
        if eof:
            raise ValueError(f'HEX line {number}: data after EOF')
        if not text.startswith(':'):
            raise ValueError(f'HEX line {number}: missing colon')
        try:
            record = bytes.fromhex(text[1:])
        except ValueError as exc:
            raise ValueError(f'HEX line {number}: invalid hexadecimal') from exc
        if len(record) < 5 or len(record) != record[0] + 5:
            raise ValueError(f'HEX line {number}: invalid record length')
        if sum(record) & 255:
            raise ValueError(f'HEX line {number}: checksum mismatch')
        length, address, kind = record[0], int.from_bytes(record[1:3], 'big'), record[3]
        payload = record[4:-1]
        records += 1
        if kind == 0:
            for offset, value in enumerate(payload):
                absolute = upper + address + offset
                if absolute > 0xFFFFFFFF:
                    raise ValueError('HEX address exceeds 32 bits')
                if absolute in memory and memory[absolute] != value:
                    raise ValueError(f'HEX conflicting bytes at 0x{absolute:08X}')
                memory[absolute] = value
        elif kind == 1:
            if length != 0 or address != 0:
                raise ValueError('Invalid HEX EOF record')
            eof = True
        elif kind in (2, 4):
            if length != 2 or address != 0:
                raise ValueError('Invalid HEX extended address record')
            upper = int.from_bytes(payload, 'big') << (4 if kind == 2 else 16)
        elif kind in (3, 5):
            if length != 4 or address != 0:
                raise ValueError('Invalid HEX start address record')
        else:
            raise ValueError(f'Unsupported HEX record type {kind}')
    if not eof:
        raise ValueError('HEX missing EOF; export may be incomplete')
    missing = [address for address in range(base, base + size) if address not in memory]
    if missing:
        raise ValueError(f'Missing {len(missing)} bytes; first gap at 0x{missing[0]:08X}; no padding performed')
    return bytes(memory[address] for address in range(base, base + size)), {
        'format': 'Intel HEX', 'records': records, 'exported_unique_bytes': len(memory),
        'selected_start': f'0x{base:08X}', 'selected_end': f'0x{base + size - 1:08X}',
        'missing_bytes': 0,
    }


def read_dump(path, base=BASE, size=FRAME_SIZE):
    if path.suffix.lower() in ('.hex', '.ihex'):
        return read_ihex(path, base, size)
    data = path.read_bytes()
    if len(data) != size:
        raise ValueError(f'BIN must contain exactly {size} bytes; got {len(data)}. '
                         'Keil SAVE produces HEX; changing its extension does not create BIN.')
    return data, {'format': 'raw BIN', 'missing_bytes': 0}


def decode_pixels(data, pixel_format='y8', width=WIDTH, height=HEIGHT, yuv_range='limited'):
    """RGB565 LE or packed Y0 U0 Y1 V0; no byte-order guessing or repair."""
    if pixel_format not in PIXEL_FORMATS:
        raise ValueError('Unsupported pixel format: ' + pixel_format)
    if width <= 0 or height <= 0:
        raise ValueError('Dimensions must be positive')
    if pixel_format in ('yuv422', 'yuyv') and width % 2:
        raise ValueError('YUYV requires even width')
    if yuv_range not in ('limited', 'full'):
        raise ValueError('YUV range must be limited or full')
    if len(data) != width * height * PIXEL_FORMATS[pixel_format]:
        raise ValueError('Frame byte count does not match selected pixel format')
    if pixel_format == 'y8':
        return data, 1, data
    rgb = bytearray()
    if pixel_format == 'rgb565':
        for offset in range(0, len(data), 2):
            value = data[offset] | (data[offset + 1] << 8)
            r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
            rgb.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        luma = bytes((77 * rgb[i] + 150 * rgb[i + 1] + 29 * rgb[i + 2] + 128) >> 8
                     for i in range(0, len(rgb), 3))
    else:
        def clip(value):
            return max(0, min(255, value))
        for offset in range(0, len(data), 4):
            y0, u, y1, v = data[offset:offset + 4]
            d, e = u - 128, v - 128
            for y in (y0, y1):
                if yuv_range == 'limited':
                    c = y - 16
                    rgb.extend((clip((298 * c + 409 * e + 128) >> 8),
                                clip((298 * c - 100 * d - 208 * e + 128) >> 8),
                                clip((298 * c + 516 * d + 128) >> 8)))
                else:
                    rgb.extend((clip(y + ((359 * e + 128) >> 8)),
                                clip(y - ((88 * d + 183 * e + 128) >> 8)),
                                clip(y + ((454 * d + 128) >> 8))))
        luma = data[0::2]
    return bytes(rgb), 3, luma


def write_png(path, pixels, width, height, channels=1):
    """Write lossless 8-bit grayscale or RGB PNG using the standard library."""
    if channels not in (1, 3) or len(pixels) != width * height * channels:
        raise ValueError('PNG pixel count does not match dimensions')
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    stride = width * channels
    scanlines = b''.join(b'\x00' + pixels[y * stride:(y + 1) * stride] for y in range(height))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0 if channels == 1 else 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b''))
    path.write_bytes(png)


def enlarge_nearest(data, width, height, scale=4, channels=1):
    if channels not in (1, 3) or scale < 1 or len(data) != width * height * channels:
        raise ValueError('Invalid preview dimensions or channels')
    result = []
    for y in range(height):
        start = y * width * channels
        row = b''.join(data[start + x * channels:start + (x + 1) * channels] * scale
                       for x in range(width))
        result.extend([row] * scale)
    return b''.join(result)


def analyze(data, width=WIDTH, height=HEIGHT):
    rows = [data[y * width:(y + 1) * width] for y in range(height)]
    reference = rows[height // 2]
    shifts = [0] + [s for n in range(1, 9) for s in (-n, n)]
    row_checks = []
    suspicious = []
    for y, row in enumerate(rows):
        costs = {s: sum(abs(row[x] - reference[(x + s) % width]) for x in range(width)) for s in shifts}
        best = min(shifts, key=lambda s: costs[s])
        if best != 0 and costs[0] >= width * 2 and costs[best] < costs[0] * 0.5:
            suspicious.append(y)
        row_checks.append({'row': y, 'crc32': f'{zlib.crc32(row):08X}', 'mean': sum(row) / width,
                           'best_small_shift_vs_middle': best, 'unshifted_mae': costs[0] / width})
    period = next((p for p in range(1, width // 2 + 1) if width % p == 0 and
                   all(row == row[:p] * (width // p) for row in rows)), None)
    constant_tail = 0
    for row in reversed(rows):
        if len(set(row)) != 1:
            break
        constant_tail += 1
    columns = [sum(row[x] for row in rows) / height for x in range(width)]
    return {
        'size_bytes': len(data), 'width': width, 'height': height, 'stride_bytes': width,
        'pixel_min': min(data), 'pixel_max': max(data), 'pixel_sum': sum(data),
        'crc32': f'0x{zlib.crc32(data):08X}', 'sha256': hashlib.sha256(data).hexdigest(),
        'unique_rows': len(set(rows)),
        'identical_adjacent_rows': sum(rows[y] == rows[y - 1] for y in range(1, height)),
        'possible_small_row_shifts': suspicious,
        'exact_horizontal_period_under_full_width': period,
        'trailing_constant_rows': constant_tail,
        'column_mean': columns, 'rows': row_checks,
        'row_repeat_note': 'Vertical sensor colorbars may legitimately have identical rows. '
                           'Row repetition alone is not proof of DMA duplication.',
    }


def convert(path, out_root, expected_crc=None, pixel_format="y8", yuv_range="limited"):
    path = path.resolve()
    if pixel_format not in PIXEL_FORMATS:
        raise ValueError('Unsupported pixel format: ' + pixel_format)
    data, source_info = read_dump(path, size=FRAME_SIZE * PIXEL_FORMATS[pixel_format])
    pixels, channels, luma = decode_pixels(data, pixel_format, yuv_range=yuv_range)
    metrics = analyze(luma)
    metrics.update(size_bytes=len(data), stride_bytes=WIDTH * PIXEL_FORMATS[pixel_format],
                   crc32=f'0x{zlib.crc32(data):08X}', sha256=hashlib.sha256(data).hexdigest())
    expected_crc = None if expected_crc is None else expected_crc & 0xFFFFFFFF
    match = None if expected_crc is None else zlib.crc32(data) == expected_crc
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d_%H%M%S_%f')
    output = out_root / stamp
    output.mkdir(parents=True, exist_ok=False)
    preserved = output / ('source' + path.suffix.lower())
    shutil.copy2(path, preserved)
    (output / 'frame.bin').write_bytes(data)
    write_png(output / 'frame_160x120.png', pixels, WIDTH, HEIGHT, channels)
    write_png(output / 'frame_preview_4x.png', enlarge_nearest(pixels, WIDTH, HEIGHT, channels=channels), WIDTH * 4, HEIGHT * 4, channels)
    record = {
        'source_path': str(path), 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'source_declaration': 'User-provided hardware debugger export; script does not read the probe itself.',
        'source_info': source_info, 'metrics': metrics,
        'pixel_format': 'yuv422' if pixel_format == 'yuyv' else pixel_format,
        'byte_order': {'y8': 'Y', 'rgb565': 'RGB565 little-endian',
                       'yuv422': 'Y0 U0 Y1 V0', 'yuyv': 'Y0 U0 Y1 V0'}[pixel_format],
        'png_mode': 'L' if channels == 1 else 'RGB',
        'yuv_conversion': ('BT.601 ' + yuv_range) if pixel_format in ('yuv422', 'yuyv') else None,
        'analysis_domain': 'Y channel' if pixel_format in ('yuv422', 'yuyv') else ('derived RGB luma' if channels == 3 else 'Y8'),
        'analysis_note': 'CRC/SHA256/size/stride describe original raw bytes; pixel and row diagnostics describe luminance. YUV matrix/range are explicit rendering assumptions, not sensor readback.',
        'expected_watch_crc32': None if expected_crc is None else f'0x{expected_crc:08X}',
        'watch_crc_matches': match,
        'visual_review_status': 'PENDING',
        'visual_checks': {'row_alignment': 'PENDING', 'unexpected_repetition': 'PENDING',
                          'visible_truncation': 'PENDING', 'colorbar_layout': 'PENDING'},
        'output_path': str(output.resolve()),
    }
    (output / 'analysis.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
    text = f'''# 实板导出帧初检

- 来源：{path}
- 来源文件SHA256：{record['source_sha256']}
- 格式：{record["pixel_format"]}；排列：{record["byte_order"]}；PNG模式：{record["png_mode"]}。
- 输出：原始BIN、160×120 PNG、4倍最近邻预览；未翻转、平移、补齐或归一化。
- YUV转换：{record["yuv_conversion"]}；仅为显式显示假设，未回读传感器矩阵或量程。
- 帧长度：{len(data)}字节，每行{metrics["stride_bytes"]}字节，共120行。
- 亮度min/max/sum：{metrics['pixel_min']} / {metrics['pixel_max']} / {metrics['pixel_sum']}。
- CRC32：{metrics['crc32']}；与指定Watch CRC比对：{match}（None表示未提供）。
- 不同图像行数量：{metrics['unique_rows']}；完全相同相邻行：{metrics['identical_adjacent_rows']}。
- 可能存在小幅行偏移的行号：{metrics['possible_small_row_shifts']}。
- 检出的短横向精确重复周期：{metrics['exact_horizontal_period_under_full_width']}。
- 末尾连续单色行：{metrics['trailing_constant_rows']}。

状态：待目视验收。垂直彩条的各行相同可能是正常现象，不能仅凭重复行判定DMA错误。
尚需检查：行错位、异常重复、明显截断、彩条排列。脚本不自动判定图案正确。
'''
    (output / '验收记录.md').write_text(text, encoding='utf-8')
    return record


def main():
    parser = argparse.ArgumentParser(description='Convert a 160x120 Y8, RGB565 LE or YUYV debugger dump into PNG.')
    default_input = HERE / 'camera_frame.hex'
    if not default_input.exists():
        default_input = HERE.parent.parent / 'MDK-ARM/camera_frame.hex'
    parser.add_argument('input', type=Path, nargs='?', default=default_input)
    parser.add_argument('--format', choices=PIXEL_FORMATS, default='y8', help='Explicit sensor format; default: y8')
    parser.add_argument('--yuv-range', choices=('limited', 'full'), default='limited', help='BT.601 rendering range, not auto-detected')
    parser.add_argument('--out', type=Path, default=HERE / 'captures')
    parser.add_argument('--expected-crc', type=lambda value: int(value, 0))
    args = parser.parse_args()
    try:
        record = convert(args.input, args.out, args.expected_crc, args.format, args.yuv_range)
    except (OSError, ValueError, UnicodeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print('Output:', record['output_path'])
    metrics = record['metrics']
    print('Format:', record['pixel_format'], 'PNG:', record['png_mode'], 'YUV:', record['yuv_conversion'])
    print('Bytes:', metrics['size_bytes'], 'min/max/sum:', metrics['pixel_min'], metrics['pixel_max'], metrics['pixel_sum'])
    print('CRC32:', metrics['crc32'], 'Watch match:', record['watch_crc_matches'])
    print('Visual review remains required; no automatic image repair was applied.')
    return 2 if record['watch_crc_matches'] is False else 0


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    raise SystemExit(main())
