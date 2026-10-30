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
             '--keep=USB_UVC_GetDeviceDescriptor', '--scatter', scatter, '--no_scanlib', '-o', image])
        with image.open('rb') as f:
            elf = ELFFile(f)
            symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                        if s['sh_flags'] & 2 and s['sh_type'] == 'SHT_PROGBITS' and s['sh_size']]
    return symbols, sections


class DescriptorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.symbols, sections = compile_module()
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
        self.assertEqual(self.config_length, 179)
        self.assertEqual(struct.unpack_from('<H', self.config, 2)[0], 179)

    def test_02_all_descriptor_boundaries(self):
        self.assertEqual([d[0] for _, _, _, d in self.entries], [9, 8, 9, 13, 18, 11, 9, 9, 14, 27, 30, 6, 9, 7])
        self.assertEqual(sum(d[0] for _, _, _, d in self.entries), 179)

    def test_03_iad(self):
        self.assertEqual(self.config[9:17], bytes.fromhex('08 0B 00 02 0E 03 00 00'))

    def test_04_interfaces_and_alternates(self):
        entries = [d for _, _, _, d in self.entries if d[1] == 4]
        self.assertEqual([(d[2], d[3], d[4], d[5], d[6]) for d in entries], [(0, 0, 0, 14, 1), (1, 0, 0, 14, 2), (1, 1, 1, 14, 2)])
        self.assertEqual(self.config[4], len({d[2] for d in entries}))

    def test_05_vc_length_and_version(self):
        header = self.cs(0, 1)
        self.assertEqual(header[3:7], bytes.fromhex('00 01 33 00'))
        self.assertEqual(sum(len(d) for _, i, _, d in self.entries if i == 0 and d[1] == 0x24), 51)
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
        self.assertEqual(self.cs(0, 5), bytes.fromhex('0B 24 05 02 01 00 00 02 00 00 00'))

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
        self.assertEqual(self.config_get(), (self.config_address, 179))
        self.assertEqual(self.device_get(1), (self.device_address, 18))

    def test_19_alignment_and_pio_padding(self):
        self.assertEqual(self.config_address % 4, 0)
        self.assertEqual(self.device_address % 4, 0)
        self.assertEqual(bytes(self.cpu.mem_read(self.config_address + 179, 1)), b'\x00')
        self.assertEqual(bytes(self.cpu.mem_read(self.device_address + 18, 2)), bytes(2))

    def test_20_initializer_count(self):
        source = (ROOT / 'CM7/Service/usb_uvc_desc.c').read_bytes().decode('gbk')
        body = source.split('s_uvc_config_storage', 1)[1].split('};', 1)[0]
        body = re.sub(r'//[^\r\n]*', '', body)
        literal = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)U', body))
        self.assertEqual(len(literal), 179)
        self.assertEqual(literal, self.config)


if __name__ == '__main__':
    capture = io.StringIO()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DescriptorTests)
    result = unittest.TextTestRunner(stream=capture, verbosity=2).run(suite)
    output = capture.getvalue()
    print(output)
    summary = dict(total=result.testsRun, passed=result.testsRun - len(result.failures) - len(result.errors),
                   failures=len(result.failures), errors=len(result.errors))
    sources = ['CM7/Service/usb_uvc_desc.c', 'CM7/Service/usb_uvc_desc.h']
    record = dict(summary=summary, source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}, output=output)
    (HERE / 'results.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    sys.exit(not result.wasSuccessful())
