"""Tool integrity checks using the supplied real dump; not simulated image acceptance."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zlib

from PIL import Image
import view_frame as tool

HERE = Path(__file__).resolve().parent
CAPTURE = HERE / 'captures/20261020_160039_979921'
SOURCE = CAPTURE / 'source.hex'


class RealDumpChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data, cls.info = tool.read_ihex(SOURCE)

    def test_export_range_and_watch(self):
        self.assertEqual(len(self.data), 19200)
        self.assertEqual(self.info['missing_bytes'], 0)
        self.assertEqual((min(self.data), max(self.data), sum(self.data)), (4, 255, 2491680))
        self.assertEqual(zlib.crc32(self.data), 0x35A2952B)

    def test_preserved_source_and_bin(self):
        record = json.loads((CAPTURE / 'analysis.json').read_text(encoding='utf-8'))
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), record['source_sha256'])
        self.assertEqual(self.data, (CAPTURE / 'frame.bin').read_bytes())

    def test_original_png_roundtrip(self):
        with Image.open(CAPTURE / 'frame_160x120.png') as image:
            self.assertEqual(image.mode, 'L')
            self.assertEqual(image.size, (160, 120))
            self.assertEqual(image.tobytes(), self.data)

    def test_preview_nearest_roundtrip(self):
        with Image.open(CAPTURE / 'frame_preview_4x.png') as image:
            self.assertEqual(image.size, (640, 480))
            self.assertEqual(bytes(image.getpixel((x * 4, y * 4)) for y in range(120) for x in range(160)), self.data)

    def test_vertical_bars_not_false_duplicate_fault(self):
        metrics = tool.analyze(self.data)
        self.assertEqual(metrics['unique_rows'], 1)
        self.assertEqual(metrics['identical_adjacent_rows'], 119)
        self.assertEqual(metrics['possible_small_row_shifts'], [])
        self.assertIsNone(metrics['exact_horizontal_period_under_full_width'])
        self.assertEqual(metrics['trailing_constant_rows'], 0)

    def test_raw_bin_read(self):
        data, info = tool.read_dump(CAPTURE / 'frame.bin')
        self.assertEqual(data, self.data)
        self.assertEqual(info['format'], 'raw BIN')

    def test_bad_bin_length_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'bad.bin'
            for content in [self.data[:-1], self.data + b'\x00']:
                path.write_bytes(content)
                with self.assertRaisesRegex(ValueError, 'exactly 19200'):
                    tool.read_dump(path)

    def bad_hex(self, change, expected):
        lines = SOURCE.read_text(encoding='ascii').splitlines()
        change(lines)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'bad.hex'
            path.write_text('\n'.join(lines) + '\n', encoding='ascii')
            with self.assertRaisesRegex(ValueError, expected):
                tool.read_ihex(path)

    def test_checksum_rejected(self):
        def change(lines):
            value = int(lines[1][-2:], 16) ^ 1
            lines[1] = lines[1][:-2] + f'{value:02X}'
        self.bad_hex(change, 'checksum mismatch')

    def test_missing_data_not_padded(self):
        self.bad_hex(lambda lines: lines.pop(1), 'Missing 16 bytes')

    def test_missing_eof_rejected(self):
        self.bad_hex(lambda lines: lines.pop(), 'missing EOF')

    def test_conflicting_duplicate_rejected(self):
        def change(lines):
            record = bytearray.fromhex(lines[1][1:])
            record[4] ^= 1
            record[-1] = (-sum(record[:-1])) & 255
            lines.insert(2, ':' + record.hex().upper())
        self.bad_hex(change, 'conflicting bytes')


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RealDumpChecks)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (HERE / 'tool_checks.json').write_text(json.dumps({
        'tests': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
        'all_passed': result.wasSuccessful(), 'source': str(SOURCE),
        'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'note': 'Real exported frame used for PNG roundtrip; malformed HEX/BIN copies only test rejection and are temporary.'
    }, indent=2), encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
