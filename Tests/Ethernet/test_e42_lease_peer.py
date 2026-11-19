"""Pure packet and lease scenario checks; does not open a network adapter."""
import struct
import unittest

from e42_lease_peer import (
    ANY, BOARD, BROADCAST, CLIENT, COOKIE, LEASE, PEER, SERVER, T1, T2, Scenario,
    board_server_frame, checksum, decode, options,
)


def request(kind, xid, ciaddr=ANY, dest=BROADCAST, select=False):
    dhcp = bytearray(240)
    dhcp[:3] = b'\x01\x01\x06'
    dhcp[4:8] = xid
    dhcp[12:16] = ciaddr
    dhcp[28:34] = BOARD
    dhcp[236:240] = COOKIE
    dhcp.extend((53, 1, kind))
    if select:
        dhcp.extend(b'\x36\x04' + SERVER + b'\x32\x04' + CLIENT)
    dhcp.append(255)
    udp = struct.pack('!HHHH', 68, 67, 8 + len(dhcp), 0)
    ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp) + len(dhcp), 0, 0, 64, 17, 0, ciaddr, dest)
    return (PEER if dest == SERVER else b'\xff' * 6) + BOARD + b'\x08\x00' + ip + udp + dhcp


class LeasePeerTest(unittest.TestCase):
    def test_full_sequence(self):
        machine = Scenario()
        xid = b'1234'
        expect = [
            (0, request(1, xid), 'discover', 2, BROADCAST),
            (1, request(3, xid, select=True), 'initial_ack', 5, BROADCAST),
            (61, request(3, b'0002', CLIENT, SERVER), 't1_ack', 5, CLIENT),
            (121, request(3, b'0003', CLIENT, SERVER), 'renew_no_reply', None, None),
            (181, request(3, b'0004', CLIENT), 't2_ack', 5, BROADCAST),
            (241, request(3, b'0005', CLIENT, SERVER), 'renew_no_reply', None, None),
            (301, request(3, b'0006', CLIENT), 'rebind_no_reply', None, None),
            (361, request(1, b'0007'), 'discover_after_expiry', None, None),
            (365, request(1, b'0007'), 'discover_no_reply', None, None),
        ]
        for now, frame, action, kind, dest in expect:
            decoded = decode(frame)
            self.assertIsNotNone(decoded)
            actual, response = machine.handle(decoded, now)
            self.assertEqual(actual, action)
            if kind is None:
                self.assertIsNone(response)
                continue
            self.assertEqual(checksum(response[14:34]), 0)
            self.assertEqual(response[30:34], dest)
            self.assertEqual(response[46:50], decoded[1])
            self.assertEqual(response[58:62], CLIENT)
            fields = options(response[282:])
            self.assertEqual(fields[53], bytes((kind,)))
            self.assertEqual(fields[54], SERVER)
            for key, value in ((51, LEASE), (58, T1), (59, T2)):
                self.assertEqual(int.from_bytes(fields[key], 'big'), value)
            self.assertEqual(board_server_frame(response), response)
        self.assertEqual(machine.phase, 'expired')

    def test_untrusted_frames_do_not_select_or_start_lease(self):
        frame = request(1, b'abcd')
        self.assertIsNone(decode(frame[:6] + b'\0' * 6 + frame[12:]))
        self.assertIsNone(decode(frame[:-1]))
        self.assertIsNone(board_server_frame(frame))
        machine = Scenario()
        self.assertEqual(machine.handle(decode(request(3, b'abcd', select=True)), 0),
                         ('ignored_request', None))


if __name__ == '__main__':
    unittest.main()
