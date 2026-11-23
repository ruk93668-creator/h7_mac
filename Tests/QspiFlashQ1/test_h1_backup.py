"""Verify the offline data format with synthetic input; no debugger or device."""
from pathlib import Path
from tempfile import TemporaryDirectory
import os
import unittest

import h1_backup as h


class H1BackupTests(unittest.TestCase):
    uid = bytes.fromhex('11 32 12 56 78 9A BC DE')
    neighbor = bytes(range(256)) * 16
    original = b'\xFF' * h.SECTOR

    def capture(self, first=None, second=None, uid=None, complete=1):
        data = self.neighbor + self.original
        return h.CAPTURE.pack(0x31424351, 1, complete, h.NEIGHBOR, 8192, 0xEF4018, 30000000, 0,
                              self.uid if uid is None else uid) + (data if first is None else first) + (
                                  data if second is None else second)

    def test_exact_capture_splits_both_sectors(self):
        self.assertEqual(h.unpack_capture(self.capture()), (self.uid, self.original, self.neighbor))

    def test_120mhz_capture_requires_sfdp_and_matching_paths(self):
        data = self.neighbor + self.original
        header = h.CAPTURE.pack(0x31424351, 2, 1, h.NEIGHBOR, 8192, 0xEF4018, 120000000, 0, self.uid)
        sfdp = b'SFDP' + bytes(12)
        self.assertEqual(h.unpack_capture(header + data * 2 + sfdp),
                         (self.uid, self.original, self.neighbor))
        for raw in (header + data * 2, header + data * 2 + bytes(16),
                    header + data + bytes(8192) + sfdp):
            with self.assertRaises(ValueError):
                h.unpack_capture(raw)

    def test_bad_capture_length_status_identity_and_mismatch(self):
        for raw in (self.capture()[:-1], self.capture(complete=0),
                    self.capture(uid=b'\xff' * 8), self.capture(second=b'\xff' * 8192)):
            with self.subTest(length=len(raw)), self.assertRaises(ValueError):
                h.unpack_capture(raw)

    def test_ihex_roundtrips_across_address_boundary(self):
        for start, raw in ((0x20008D80, self.capture()), (0x2000FFFC, b'\xA5' * 41)):
            self.assertEqual(h.from_ihex(h.to_ihex(start, raw), start, len(raw)), raw)

    def test_ihex_rejects_corruption_and_wrong_range(self):
        start = 0x20008D80
        text = h.to_ihex(start, b'\x55' * 32)
        lines = text.splitlines()
        corrupt = lines.copy()
        corrupt[1] = corrupt[1][:-2] + ('00' if corrupt[1][-2:] != '00' else '01')
        for candidate, addr in (('\n'.join(corrupt), start), ('\n'.join(lines[0:1] + lines[2:]), start),
                                ('\n'.join(lines[:2] + lines[1:]), start), (text, start + 4)):
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                h.from_ihex(candidate, addr, 32)

    def test_three_distinct_packages_roundtrip(self):
        pattern = h.test_pattern()
        programmed = self.original[:h.OFFSET] + pattern + self.original[h.OFFSET + len(pattern):]
        for op, state in [('program', self.original), ('erase', programmed), ('restore', self.original)]:
            package = h.build_package(op, self.uid, self.original, self.neighbor, state)
            self.assertEqual(len(package), 12328)
            head = h.INPUT.unpack_from(package)
            self.assertEqual(head[2:5], (h.OPS[op], h.TARGET, 4096))
            self.assertEqual(head[8], self.uid)
            self.assertEqual(package[-h.SECTOR:], state)

    def test_reject_stale_or_ineligible_sector(self):
        for op, original, state in [
                ('erase', self.original, self.original),
                ('restore', self.original, self.neighbor),
                ('program', self.neighbor, self.neighbor)]:
            with self.subTest(op=op), self.assertRaises(ValueError):
                h.build_package(op, self.uid, original, self.neighbor, state)

    def test_archive_detects_split_file_tamper(self):
        raw = self.capture()
        with TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path/'capture.bin').write_bytes(raw)
            (path/'target.bin').write_bytes(self.original)
            (path/'neighbor.bin').write_bytes(self.neighbor)
            h.json_file(path/'backup.json', dict(uid=self.uid.hex(), capture_sha256=h.sha(raw),
                target=dict(address=h.TARGET, bytes=4096, sha256=h.sha(self.original), crc32=h.crc(self.original)),
                neighbor=dict(address=h.NEIGHBOR, bytes=4096, sha256=h.sha(self.neighbor), crc32=h.crc(self.neighbor))))
            self.assertEqual(h.check_archive(path)[1:], (self.uid, self.original, self.neighbor))
            (path/'target.bin').write_bytes(self.neighbor)
            with self.assertRaises(ValueError):
                h.check_archive(path)

    @unittest.skipUnless(os.environ.get('H1_BUILD_ROOT'), 'Set H1_BUILD_ROOT to a verified local output tree')
    def test_actual_axf_symbols_and_sram_only_roundtrip(self):
        root = Path(os.environ['H1_BUILD_ROOT'])
        _, backup_addr = h.reviewed_build(root/'backup'/'build.json', 'backup',
                                         'g_qspi_h1_capture', len(self.capture()))
        _, write_addr = h.reviewed_build(root/'write'/'build.json', 'write', 'g_qspi_h1_input', 12328)
        capture = h.from_ihex(h.to_ihex(backup_addr, self.capture()), backup_addr, len(self.capture()))
        uid, target, neighbor = h.unpack_capture(capture)
        packet = h.build_package('program', uid, target, neighbor, target)
        self.assertEqual(h.from_ihex(h.to_ihex(write_addr, packet), write_addr, len(packet)), packet)

    @unittest.skipUnless(os.environ.get('H1_BACKUP120_BUILD'), 'Set H1_BACKUP120_BUILD to the local manifest')
    def test_import_120mhz_with_actual_axf(self):
        from contextlib import redirect_stdout
        from io import StringIO
        import json
        from types import SimpleNamespace

        build = Path(os.environ['H1_BACKUP120_BUILD'])
        payload = self.neighbor + self.original
        header = h.CAPTURE.pack(0x31424351, 2, 1, h.NEIGHBOR, 8192, 0xEF4018, 120000000, 0, self.uid)
        raw = header + payload * 2 + b'SFDP' + bytes(12)
        manifest, address = h.reviewed_build(build, 'backup120', 'g_qspi_h1_capture', len(raw))
        with TemporaryDirectory() as tmp:
            folder = Path(tmp)
            export = folder/'synthetic.hex'
            export.write_text(h.to_ihex(address, raw), encoding='ascii')
            args = SimpleNamespace(build=build, out=folder/'capture', export=export,
                board='SYNTHETIC', read_time='2026-11-23T12:00:00+08:00',
                kind='original', firmware_sha='a'*40)
            with redirect_stdout(StringIO()):
                h.import_capture(args)
            meta, uid, backup, adjacent = h.check_archive(args.out)
            self.assertEqual((uid, backup, adjacent), (self.uid, self.original, self.neighbor))
            self.assertEqual(meta['axf_sha256'], manifest['axf_sha256'])
            self.assertEqual(meta['read_opcodes'], ['0B', '6B'])
            meta['capture_profile'] = 'backup'
            (args.out/'backup.json').write_text(json.dumps(meta), encoding='utf-8')
            with self.assertRaises(ValueError):
                h.check_archive(args.out)


if __name__ == '__main__':
    unittest.main(verbosity=2)
