"""Compile the actual descriptor module; execute getters as ARM code, then audit bytes."""
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

COMMANDS = []
TOOL = Path('D:/32/ARM/ARMCC/Bin')


def run(command):
    command = list(map(str, command))
    result = subprocess.run(command, cwd=ROOT, capture_output=True)
    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
    COMMANDS.append(dict(command=subprocess.list2cmdline(command), returncode=result.returncode, output=output))
    (HERE / 'commands.json').write_text(json.dumps(COMMANDS, indent=2), encoding='utf-8')
    if result.returncode or 'warning' in output.lower():
        raise RuntimeError(output)


def compile_module():
    run([TOOL / 'armcc.exe', '--vsn'])
    with tempfile.TemporaryDirectory(prefix='UsbUvcDescriptors_') as temp:
        temp = Path(temp)
        scatter = temp / 'test.sct'
        scatter.write_text('LR_TEST 0x08000000 0x100000 {\n ER_TEST 0x08000000 { .ANY (+RO) }\n RW_TEST 0x20000000 0x40000 { .ANY (+RW +ZI) }\n}\n')
        args = [TOOL / 'armcc.exe', '--cpu', 'Cortex-M7', '--c99', '-O0',
                '-DSTM32H747xx', '-DCORE_CM7']
        for include in ['CM7/Service', 'CM7/Platform', 'Drivers/CMSIS/Include',
                        'Drivers/CMSIS/Device/ST/STM32H7xx/Include',
                        'Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
            args += ['-I', ROOT / include]
        obj, image = temp / 'descriptor.o', temp / 'descriptor.axf'
        run(args + ['-c', ROOT / 'CM7/Service/usb_uvc_desc.c', '-o', obj])
        run([TOOL / 'armlink.exe', obj, '--entry', 'USB_UVC_GetFSConfigDescriptor',
             '--keep=USB_UVC_GetDeviceDescriptor', '--keep=USB_UVC_GetLangIDStrDescriptor',
             '--keep=USB_UVC_GetInterfaceStrDescriptor', '--scatter', scatter, '--no_scanlib', '-o', image])
        with image.open('rb') as f:
            elf = ELFFile(f)
            symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            sizes = {s.name: s['st_size'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                        if s['sh_flags'] & 2 and s['sh_type'] == 'SHT_PROGBITS' and s['sh_size']]
    return symbols, sizes, sections


class DescriptorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.symbols, cls.sizes, sections = compile_module()
        cls.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [(0x08000000, 0x100000), (0x20000000, 0x40000), (0x01000000, 0x1000)]:
            cls.cpu.mem_map(address, size)
        for address, data in sections:
            cls.cpu.mem_write(address, data)
        cls.length_address = 0x2003F000
        cls.config_address, cls.config_length = cls.config_get()
        cls.config = bytes(cls.cpu.mem_read(cls.config_address, cls.config_length))
        cls.device_address, cls.device_length = cls.device_get(1)  # USBD_SPEED_FULL
        cls.device = bytes(cls.cpu.mem_read(cls.device_address, cls.device_length))
        cls.entries = []
        offset, interface, alt = 0, None, None
        while offset < len(cls.config):
            length = cls.config[offset]
            if length < 2 or offset + length > len(cls.config):
                raise AssertionError(f'invalid bLength at {offset}')
            desc = cls.config[offset:offset + length]
            if desc[1] == 4:
                interface, alt = desc[2], desc[3]
            cls.entries.append((offset, interface, alt, desc))
            offset += length

    @classmethod
    def invoke(cls, name, r0=0, r1=0):
        cls.cpu.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
        cls.cpu.reg_write(UC_ARM_REG_LR, 0x01000001)
        cls.cpu.reg_write(UC_ARM_REG_R0, r0)
        cls.cpu.reg_write(UC_ARM_REG_R1, r1)
        cls.cpu.emu_start(cls.symbols[name] | 1, 0x01000000, count=100000, timeout=1000000)
        assert cls.cpu.reg_read(UC_ARM_REG_PC) == 0x01000000
        return cls.cpu.reg_read(UC_ARM_REG_R0)

    @classmethod
    def config_get(cls):
        cls.cpu.mem_write(cls.length_address, b'\xFF\xFF')
        pointer = cls.invoke('USB_UVC_GetFSConfigDescriptor', cls.length_address)
        return pointer, struct.unpack('<H', cls.cpu.mem_read(cls.length_address, 2))[0]

    @classmethod
    def device_get(cls, speed):
        cls.cpu.mem_write(cls.length_address, b'\xFF\xFF')
        pointer = cls.invoke('USB_UVC_GetDeviceDescriptor', speed, cls.length_address)
        return pointer, struct.unpack('<H', cls.cpu.mem_read(cls.length_address, 2))[0]

    def cs(self, interface, subtype):
        found = [d for _, i, a, d in self.entries if i == interface and a == 0 and d[1:3] == bytes([0x24, subtype])]
        self.assertEqual(len(found), 1)
        return found[0]

    def test_01_configuration_length(self):
        self.assertEqual(self.config_length, 180)
        self.assertEqual(struct.unpack_from('<H', self.config, 2)[0], 180)

    def test_02_all_descriptor_boundaries(self):
        self.assertEqual([d[0] for _, _, _, d in self.entries], [9, 8, 9, 13, 18, 12, 9, 9, 14, 27, 30, 6, 9, 7])
        self.assertEqual(sum(d[0] for _, _, _, d in self.entries), 180)

    def test_03_iad(self):
        self.assertEqual(self.config[9:17], bytes.fromhex('08 0B 00 02 0E 03 00 05'))

    def test_04_interfaces_and_alternates(self):
        entries = [d for _, _, _, d in self.entries if d[1] == 4]
        self.assertEqual([(d[2], d[3], d[4], d[5], d[6]) for d in entries], [(0, 0, 0, 14, 1), (1, 0, 0, 14, 2), (1, 1, 1, 14, 2)])
        self.assertEqual(self.config[4], len({d[2] for d in entries}))

    def test_05_vc_length_and_version(self):
        header = self.cs(0, 1)
        self.assertEqual(header[3:7], bytes.fromhex('10 01 34 00'))
        self.assertEqual(sum(len(d) for _, i, _, d in self.entries if i == 0 and d[1] == 0x24), 52)
        self.assertEqual(header[11:13], b'\x01\x01')

    def test_06_terminal_topology(self):
        camera, processing, output = self.cs(0, 2), self.cs(0, 5), self.cs(0, 3)
        self.assertEqual((camera[3], processing[3], output[3]), (1, 2, 3))
        self.assertEqual((processing[4], output[7]), (1, 2))
        self.assertEqual(camera[4:6], b'\x01\x02')
        self.assertEqual(output[4:6], b'\x01\x01')

    def test_07_camera_controls(self):
        camera = self.cs(0, 2)
        self.assertEqual(camera[8:14], bytes(6))
        self.assertEqual(camera[14:], b'\x03\x00\x00\x00')

    def test_08_processing_controls(self):
        self.assertEqual(self.cs(0, 5), bytes.fromhex('0C 24 05 02 01 00 00 02 00 00 00 00'))

    def test_09_vs_length_and_link(self):
        header = self.cs(1, 1)
        self.assertEqual(header[3:9], bytes.fromhex('01 4D 00 81 00 03'))
        self.assertEqual(sum(len(d) for _, i, _, d in self.entries if i == 1 and d[1] == 0x24), 77)
        self.assertEqual(header[9:], bytes.fromhex('00 00 00 01 00'))

    def test_10_yuy2_format(self):
        fmt = self.cs(1, 4)
        self.assertEqual(fmt[3:5], b'\x01\x01')
        self.assertEqual(fmt[5:21], uuid.UUID('32595559-0000-0010-8000-00aa00389b71').bytes_le)
        self.assertEqual(fmt[21:], bytes([16, 1, 0, 0, 0, 0]))

    def test_11_frame_dimensions(self):
        frame = self.cs(1, 5)
        self.assertEqual(frame[3:5], b'\x01\x00')
        self.assertEqual(struct.unpack_from('<HH', frame, 5), (160, 120))

    def test_12_frame_rate_size_bitrate(self):
        frame = self.cs(1, 5)
        self.assertEqual(struct.unpack_from('<IIII', frame, 9), (160 * 120 * 16 * 10, 3072000, 38400, 1000000))
        self.assertEqual(frame[25], 1)
        self.assertEqual(struct.unpack_from('<I', frame, 26)[0], 10000000 // 10)

    def test_13_color_matching(self):
        self.assertEqual(self.cs(1, 13), bytes.fromhex('06 24 0D 00 00 00'))

    def test_14_endpoint(self):
        endpoints = [(i, a, d) for _, i, a, d in self.entries if d[1] == 5]
        self.assertEqual(endpoints, [(1, 1, bytes.fromhex('07 05 81 05 00 02 01'))])
        self.assertEqual(endpoints[0][2][2], self.cs(1, 1)[6])

    def test_15_iad_device_identity(self):
        self.assertEqual(self.device_length, 18)
        self.assertEqual(self.device[:8], bytes.fromhex('12 01 00 02 EF 02 01 40'))
        self.assertEqual(self.device[8:14], bytes.fromhex('09 12 01 00 00 01'))
        self.assertEqual(self.device[14:18], bytes.fromhex('00 00 00 01'))

    def test_16_null_length(self):
        self.assertEqual(self.invoke('USB_UVC_GetFSConfigDescriptor', 0), 0)
        self.assertEqual(self.invoke('USB_UVC_GetDeviceDescriptor', 1, 0), 0)

    def test_17_unsupported_speeds(self):
        for speed in [0, 2, 255]:
            self.assertEqual(self.device_get(speed), (0, 0))

    def test_18_static_pointers(self):
        self.assertEqual(self.config_get(), (self.config_address, 180))
        self.assertEqual(self.device_get(1), (self.device_address, 18))

    def test_19_alignment_and_pio_padding(self):
        self.assertEqual(self.config_address % 4, 0)
        self.assertEqual(self.device_address % 4, 0)
        self.assertEqual(bytes(self.cpu.mem_read(self.config_address + 180, 1)), b'\x00')
        self.assertEqual(bytes(self.cpu.mem_read(self.device_address + 18, 2)), bytes(2))

    def test_20_initializer_count(self):
        source = (ROOT / 'CM7/Service/usb_uvc_desc.c').read_bytes().decode('gbk')
        body = source.split('s_uvc_config_storage', 1)[1].split('};', 1)[0]
        body = re.sub(r'//[^\r\n]*', '', body)
        literal = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)U', body))
        self.assertEqual(len(literal), 180)
        self.assertEqual(literal, self.config)

    def test_21_deprecated_clock_bytes_preserved(self):
        header = self.cs(0, 1)
        self.assertEqual(len(header), 13)
        self.assertEqual(header[7:11], bytes.fromhex('00 6C DC 02'))
        self.assertEqual(header[11:13], bytes.fromhex('01 01'))

    def test_22_processing_unit_field_offsets(self):
        unit = self.cs(0, 5)
        self.assertEqual(len(unit), 12)
        fields = [(0, 1, b'\x0c'), (1, 1, b'\x24'), (2, 1, b'\x05'),
                  (3, 1, b'\x02'), (4, 1, b'\x01'), (5, 2, bytes(2)),
                  (7, 1, b'\x02'), (8, 2, bytes(2)), (10, 1, b'\x00'),
                  (11, 1, b'\x00')]
        for offset, size, expected in fields:
            with self.subTest(offset=offset):
                self.assertEqual(unit[offset:offset + size], expected)

    def test_23_compiled_storage_and_last_pio_word(self):
        self.assertEqual(self.symbols['s_uvc_config_storage'], self.config_address)
        self.assertEqual(self.sizes['s_uvc_config_storage'], 184)
        self.assertEqual(self.config_length % 4, 0)
        last_word_offset = ((self.config_length - 1) // 4) * 4
        self.assertEqual(last_word_offset, 176)
        self.assertLessEqual(last_word_offset + 4, self.sizes['s_uvc_config_storage'])
        self.assertEqual(bytes(self.cpu.mem_read(self.config_address + 176, 4)), self.config[176:180])
        self.assertEqual(bytes(self.cpu.mem_read(self.config_address + 180, 4)), bytes(4))

    def test_24_only_intended_wire_changes(self):
        # Reconstruct the compiled UVC 1.0 baseline; all other wire bytes must match.
        wire = bytearray(self.config)
        self.assertEqual((wire[16], wire[25]), (5, 5))
        wire[16] = wire[25] = 0
        pu_offset = next(offset for offset, i, _, d in self.entries if i == 0 and d[1:3] == b'\x24\x05')
        self.assertEqual(pu_offset, 57)
        del wire[pu_offset + 11]
        wire[pu_offset] = 11
        wire[2] = 0xB3
        wire[29] = 0x00
        wire[31] = 0x33
        self.assertEqual(hashlib.sha256(wire).hexdigest(),
                         '51e1059633a0a727cb4d828c595f6a39c47c5aa50671f018392fd3393ad08a1e')


    @classmethod
    def string_get(cls, name, speed=1):
        cls.cpu.mem_write(cls.length_address, b'\xff\xff')
        pointer = cls.invoke(name, speed, cls.length_address)
        length = struct.unpack('<H', cls.cpu.mem_read(cls.length_address, 2))[0]
        return pointer, length, bytes(cls.cpu.mem_read(pointer, length)) if pointer else b''

    def test_25_langid_descriptor(self):
        pointer, length, data = self.string_get('USB_UVC_GetLangIDStrDescriptor')
        self.assertNotEqual(pointer, 0)
        self.assertEqual(length, 4)
        self.assertEqual(data, bytes.fromhex('04 03 09 04'))

    def test_26_function_name_utf16le(self):
        _, length, data = self.string_get('USB_UVC_GetInterfaceStrDescriptor')
        self.assertEqual(length, 22)
        self.assertEqual(data, bytes([22, 3]) + 'UVC Camera'.encode('utf-16le'))
        self.assertEqual(data[2:].decode('utf-16le'), 'UVC Camera')
        self.assertNotIn('\x00', data[2:].decode('utf-16le'))

    def test_27_function_and_interface_indices(self):
        iad = next(d for _, _, _, d in self.entries if d[1] == 0x0B)
        vc = next(d for _, _, _, d in self.entries if d[1] == 4 and d[2] == 0)
        vs = [d for _, _, _, d in self.entries if d[1] == 4 and d[2] == 1]
        self.assertEqual(iad[7], 5)
        self.assertEqual(vc[8], 5)
        self.assertEqual(iad[7], vc[8])
        self.assertEqual([d[8] for d in vs], [0, 0])

    def test_28_static_string_pointers(self):
        for name in ['USB_UVC_GetLangIDStrDescriptor', 'USB_UVC_GetInterfaceStrDescriptor']:
            first = self.string_get(name)
            self.assertEqual(self.string_get(name), first)

    def test_29_string_storage_alignment_and_pio_tail(self):
        for name, symbol, size in [('USB_UVC_GetLangIDStrDescriptor', 's_uvc_langid_desc', 4),
                                    ('USB_UVC_GetInterfaceStrDescriptor', 's_uvc_function_desc', 24)]:
            pointer, length, data = self.string_get(name)
            self.assertEqual(pointer % 4, 0)
            self.assertEqual(pointer, self.symbols[symbol])
            self.assertEqual(self.sizes[symbol], size)
            self.assertLessEqual(((length + 3) // 4) * 4, size)
            self.assertEqual(bytes(self.cpu.mem_read(pointer + length, size - length)), bytes(size - length))

    def test_30_string_null_length_and_unsupported_speed(self):
        for name in ['USB_UVC_GetLangIDStrDescriptor', 'USB_UVC_GetInterfaceStrDescriptor']:
            self.assertEqual(self.invoke(name, 1, 0), 0)
            for speed in [0, 2, 255]:
                self.assertEqual(self.string_get(name, speed), (0, 0, b''))

    def test_31_optional_identity_and_entity_strings_remain_zero(self):
        self.assertEqual(self.device[14:17], bytes(3))
        self.assertEqual(self.config[6], 0)
        self.assertEqual(self.cs(0, 2)[7], 0)
        self.assertEqual(self.cs(0, 5)[10], 0)
        self.assertEqual(self.cs(0, 3)[8], 0)

    def test_32_only_two_configuration_string_indices_changed(self):
        wire = bytearray(self.config)
        self.assertEqual((wire[16], wire[25]), (5, 5))
        wire[16] = wire[25] = 0
        self.assertEqual(hashlib.sha256(wire).hexdigest(),
                         '46d26e057a5505aeed5b2bac3f8d1f740548f6419586f66590babb89e1561813')


if __name__ == '__main__':
    capture = io.StringIO()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DescriptorTests)
    result = unittest.TextTestRunner(stream=capture, verbosity=2).run(suite)
    output = capture.getvalue()
    print(output)
    # Execute real Core string requests as part of this same acceptance command.
    core_path = HERE / 'string_results.json'
    core_path.unlink(missing_ok=True)
    core_run = subprocess.run([sys.executable, str(HERE / 'run_string_tests.py')], cwd=ROOT, capture_output=True)
    core_output = (core_run.stdout + core_run.stderr).decode('utf-8', errors='replace')
    print(core_output)
    if (HERE / 'string_commands.json').exists():
        COMMANDS.extend(json.loads((HERE / 'string_commands.json').read_text(encoding='utf-8')))
        (HERE / 'commands.json').write_text(json.dumps(COMMANDS, indent=2), encoding='utf-8')
    core = json.loads(core_path.read_text(encoding='utf-8')) if core_path.exists() else {'summary': {'total': 8, 'passed': 0, 'failed': 0}, 'cases': []}
    core_cases = core.get('cases', [])
    summary = dict(total=result.testsRun + 8,
                   passed=result.testsRun - len(result.failures) - len(result.errors) + core['summary']['passed'],
                   failures=len(result.failures) + core['summary']['failed'],
                   errors=len(result.errors) + 8 - len(core_cases))
    sources = ['CM7/Service/usb_uvc_desc.c', 'CM7/Service/usb_uvc_desc.h', 'CM7/Service/usb_uvc_class.c']
    record = dict(summary=summary, original_descriptor_tests=24, new_native_string_tests=8, new_core_string_tests=8,
                  source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
                  core_returncode=core_run.returncode, core_cases=core_cases, output=output + core_output)
    (HERE / 'results.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    sys.exit(not result.wasSuccessful() or core_run.returncode != 0 or summary['failures'] != 0 or summary['errors'] != 0)
