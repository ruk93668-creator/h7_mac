"""Offline decoder checks against real ARM mock exports, plus corrupt input."""
import argparse
import json
from pathlib import Path
import unittest

from verify_report import h1_reference, read_export, verify
from h1_backup import to_ihex


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('mock_output', type=Path)
    args = parser.parse_args()
    layout = json.loads(args.manifest.read_bytes())['images'][0]['report_layout']
    root = args.mock_output
    data = (root / 'synthetic_report_0.bin').read_bytes()
    before = (root / 'synthetic_before.bin').read_bytes()
    after = (root / 'synthetic_after.bin').read_bytes()

    class ReportTest(unittest.TestCase):
        def test_success_export(self):
            result = verify(data, layout, before, after, [before[:4096], before[4096:]])
            self.assertEqual(result['status'], 'PASS')
            self.assertTrue(all(s['before_after_match'] and s['h1_reference_match'] for s in result['sectors']))

        def test_recover_timeout_evidence(self):
            result = verify((root / 'synthetic_report_4.bin').read_bytes(), layout)
            self.assertEqual(result['status'], 'FAIL')
            self.assertEqual(result['header']['recover'], -8)
            self.assertEqual(result['stages']['RECOVER']['fault']['latched'], 1)

        def test_another_image_or_corrupted_report_rejected(self):
            with self.assertRaises(ValueError):
                verify(data, [0, *layout[1:]], before, after)
            with self.assertRaises(ValueError):
                verify(bytes(1)+data[1:], layout, before, after)

        def test_corrupt_sector_and_wrong_h1_reference_rejected(self):
            wrong = bytes([after[0] ^ 1]) + after[1:]
            with self.assertRaises(ValueError):
                verify(data, layout, before, wrong)
            with self.assertRaises(ValueError):
                verify(data, layout, before, after, [wrong[:4096], wrong[4096:]])

        def test_exact_ihex_export_and_h1_archive_hash(self):
            location = {'address': 0x2000FFF0, 'size': len(data)}
            path = root / 'synthetic_report.hex'
            path.write_text(to_ihex(location['address'], data), encoding='ascii')
            self.assertEqual(read_export(path, location), data)
            with self.assertRaises(ValueError):
                read_export(path, {'address': location['address'] + 1, 'size': location['size']})
            directory = root / 'synthetic_h1'
            directory.mkdir(exist_ok=True)
            (directory / 'backup.json').write_text(json.dumps({
                'neighbor': {'address': '0xFFE000', 'bytes': 4096, 'sha256': 'tampered'},
            }), encoding='utf-8')
            (directory / 'neighbor.bin').write_bytes(before[:4096])
            with self.assertRaises(ValueError):
                h1_reference(directory)

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ReportTest)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == '__main__':
    raise SystemExit(main())
