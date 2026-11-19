import unittest

from e42_lease_peer import CLIENT, SERVER, LEASE, T1, T2, decode, options
from e5_lease_peer import ContinuousLease
from test_e42_lease_peer import request


class TestContinuousLease(unittest.TestCase):
    def test_repeated_renewals_keep_advertised_timers(self):
        machine = ContinuousLease()
        xid = b'E5T1'
        self.assertEqual(machine.handle(decode(request(1, xid)), 0)[0], 'discover')
        self.assertEqual(machine.handle(decode(request(3, xid, select=True)), 1)[0], 'initial_ack')
        for elapsed in range(T1, 660, T1):
            action, response = machine.handle(decode(request(3, xid, CLIENT, SERVER)), elapsed)
            self.assertEqual(action, 't1_ack')
            self.assertEqual(response[30:34], CLIENT)
            fields = options(response[282:])
            self.assertEqual(fields[53], b'\x05')
            self.assertEqual([int.from_bytes(fields[key], 'big') for key in (51, 58, 59)], [LEASE, T1, T2])
        self.assertEqual(machine.phase, 'renew')


if __name__ == '__main__':
    unittest.main()
