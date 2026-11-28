"""Observe one complete JPEG through production code in the Cortex-M host harness."""
import re
import struct
import zlib

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2


class JpegCrcTrace:
    def __init__(self, image, fixture):
        self.fixture = fixture.read_bytes()
        with image.open("rb") as stream:
            symbols = ELFFile(stream).get_section_by_name(".symtab")
            ring = symbols.get_symbol_by_name("s_connections")
            assert len(ring) == 1
            self.ring = (ring[0]["st_value"], ring[0]["st_size"])
        self.fifo = self.private = b""
        self.private_address = self.tail = 0
        self.tcp_ring = bytearray()
        self.wire = bytearray()
        self.wire_base = None

    @staticmethod
    def _first_frame(data):
        match = re.search(
            rb"--h7frame\r\nContent-Type: image/jpeg\r\nContent-Length: (\d+)\r\n"
            rb"X-Frame-Sequence: 1\r\nX-Frame-CRC32: ([0-9A-Fa-f]{8})\r\n\r\n", data)
        if match:
            n = int(match[1])
            end = match.end() + n
            if end + 2 <= len(data):
                assert data[end:end + 2] == b"\r\n", "inconsistent multipart trailer"
                return bytes(data[match.end():end]), int(match[2], 16)
        return None

    def attach(self, cpu, symbols):
        copy = symbols["eth_jpeg_copy"] & ~1
        tcp = symbols["tcp_write"] & ~1
        tx = symbols["eth_mac_tx_submit"] & ~1
        returns = {}
        ring_base, ring_size = self.ring

        def trace(u, address, _size, _context):
            if address in returns:
                callback = returns.pop(address)
                callback(u.reg_read(UC_ARM_REG_R0))
            if address == copy and not self.fifo:
                view = u.reg_read(UC_ARM_REG_R0)
                dest = u.reg_read(UC_ARM_REG_R1)
                first, second, length, tail, base, capacity = struct.unpack("<6I", u.mem_read(view, 24))
                assert 0 < tail < length and second == base and first + length - tail == base + capacity
                assert length == len(self.fixture)
                self.fifo = bytes(u.mem_read(first, length - tail)) + bytes(u.mem_read(second, tail))
                self.private_address, self.tail = dest, tail

                def copied(value):
                    assert value == 1
                    self.private = bytes(u.mem_read(dest, length))

                returns[u.reg_read(UC_ARM_REG_LR) & ~1] = copied
            elif address == tcp and self._first_frame(self.tcp_ring) is None:
                start = u.reg_read(UC_ARM_REG_R1)
                length = u.reg_read(UC_ARM_REG_R2)
                assert ring_base <= start and start + length <= ring_base + ring_size
                chunk = bytes(u.mem_read(start, length))

                def queued(value):
                    if value & 0xff == 0:
                        self.tcp_ring.extend(chunk)

                returns[u.reg_read(UC_ARM_REG_LR) & ~1] = queued
            elif address == tx and self._first_frame(self.wire) is None:
                start = u.reg_read(UC_ARM_REG_R0)
                length = u.reg_read(UC_ARM_REG_R1)
                frame = bytes(u.mem_read(start, length))
                returns[u.reg_read(UC_ARM_REG_LR) & ~1] = (
                    lambda status: self._receive(frame) if status == 0 else None)

        cpu.hook_add(UC_HOOK_CODE, trace)

    def _receive(self, frame):
        if len(frame) < 54 or frame[12:14] != b"\x08\x00" or frame[23] != 6:
            return
        ip_header = (frame[14] & 0xf) * 4
        tcp = frame[14 + ip_header:14 + int.from_bytes(frame[16:18], "big")]
        if len(tcp) < 20 or struct.unpack_from("!HH", tcp) != (80, 55000):
            return
        sequence = int.from_bytes(tcp[4:8], "big")
        if tcp[13] & 0x02:
            self.wire_base = (sequence + 1) & 0xffffffff
            return
        body = tcp[(tcp[12] >> 4) * 4:]
        if not body:
            return
        assert self.wire_base is not None
        offset = (sequence - self.wire_base) & 0xffffffff
        assert offset <= len(self.wire), "gap in simulated TCP receiver"
        overlap = min(len(self.wire) - offset, len(body))
        assert self.wire[offset:offset + overlap] == body[:overlap], "TCP retransmission differs"
        self.wire.extend(body[overlap:])

    def result(self):
        ring = self._first_frame(self.tcp_ring)
        wire = self._first_frame(self.wire)
        samples = {
            "camera_fifo": self.fifo,
            "axi_private": self.private,
            "tcp_application_ring": ring[0] if ring else b"",
            "simulated_pc_receiver": wire[0] if wire else b"",
        }
        expected = zlib.crc32(self.fixture)
        return {
            "pass": (all(sample == self.fixture for sample in samples.values()) and
                     self.tail > 0 and self.private_address == 0x24020000 and
                     ring is not None and wire is not None and ring[1] == wire[1] == expected),
            "hardware": "NOT_RUN",
            "sequence": 1,
            "fifo_tail_bytes": self.tail,
            "private_address": f"0x{self.private_address:08x}",
            "expected_crc32": f"{expected:08X}",
            "wire_header_crc32": f"{wire[1]:08X}" if wire else None,
            "stages": {
                name: {"length": len(sample), "crc32": f"{zlib.crc32(sample):08X}" if sample else None}
                for name, sample in samples.items()
            },
        }
