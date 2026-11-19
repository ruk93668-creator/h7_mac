import asyncio
import contextlib
import socket
from types import SimpleNamespace
import unittest

import e5_peer as peer


class TestProtocol(unittest.IsolatedAsyncioTestCase):
    async def test_fragmentation_and_coalescence(self):
        reader = asyncio.StreamReader()
        a = peer.encode(peer.REQUEST, 1, b'one')
        b = peer.encode(peer.REPLY, 2, bytes(range(256)) * 5)
        task = asyncio.create_task(peer.read_frame(reader))
        reader.feed_data(a[:5])
        await asyncio.sleep(0)
        self.assertFalse(task.done())
        reader.feed_data(a[5:] + b)
        self.assertEqual(await task, (peer.REQUEST, 1, b'one'))
        self.assertEqual(await peer.read_frame(reader), (peer.REPLY, 2, bytes(range(256)) * 5))
        damaged = a[:-1] + bytes([a[-1] ^ 1])
        with self.assertRaises(ValueError):
            peer.decode(damaged)
        with self.assertRaises(ValueError):
            peer.encode(peer.REQUEST, 3, bytes(1457))

    async def test_sequence_and_accounting(self):
        m = peer.Metrics()
        x = peer.Exchange(m, 7)
        _, seq, body = peer.decode(x.request())
        x.receive(peer.REPLY, seq, body)
        x.receive(peer.REPLY, seq, body)
        self.assertEqual(m.verified_replies, 1)
        self.assertEqual(m.duplicates, 1)
        self.assertIsNotNone(m.rtt_min)
        self.assertIsNotNone(x.receive(peer.REQUEST, 5, b'5'))
        x.receive(peer.REQUEST, 4, b'4')
        self.assertEqual(m.reordered, 1)
        x.request()
        x.finish()
        self.assertEqual(m.lost_or_unconfirmed, 1)

    async def test_concurrent_loopback_tool(self):
        await self.check_concurrent_loopback_tool()

    async def test_wait_for_both_tcp_roles_and_drain(self):
        await self.check_concurrent_loopback_tool(wait_concurrent=3, drain_seconds=.1)

    async def check_concurrent_loopback_tool(self, wait_concurrent=0, drain_seconds=0):
        asyncio.get_running_loop().set_debug(False)
        ip = '127.0.0.1'
        tasks = set()
        with socket.socket() as reserve:
            reserve.bind((ip, 0))
            pc_tcp_port = reserve.getsockname()[1]

        async def board_stream(reader, writer):
            writer.write(peer.encode(peer.REQUEST, 1, b'board'))
            await writer.drain()
            try:
                while True:
                    kind, seq, data = await peer.read_frame(reader)
                    if kind == peer.REQUEST:
                        writer.write(peer.encode(peer.REPLY, seq, data))
                        await writer.drain()
            except (asyncio.IncompleteReadError, OSError):
                pass
            finally:
                writer.close()
                with contextlib.suppress(OSError, asyncio.TimeoutError):
                    await asyncio.wait_for(writer.wait_closed(), 1)

        def incoming(reader, writer):
            task = asyncio.create_task(board_stream(reader, writer))
            tasks.add(task)
            task.add_done_callback(tasks.discard)

        async def board_client():
            while True:
                try:
                    reader, writer = await asyncio.open_connection(ip, pc_tcp_port)
                    break
                except OSError:
                    await asyncio.sleep(.01)
            await board_stream(reader, writer)

        class BoardUdp(asyncio.DatagramProtocol):
            sent = False

            def connection_made(self, transport):
                self.transport = transport

            def datagram_received(self, data, addr):
                kind, seq, body = peer.decode(data)
                if kind == peer.REQUEST:
                    self.transport.sendto(peer.encode(peer.REPLY, seq, body), addr)
                    if not self.sent:
                        self.sent = True
                        self.transport.sendto(peer.encode(peer.REQUEST, 1, b'board UDP'), addr)

        listener = await asyncio.start_server(incoming, ip, 0)
        board_tcp_port = listener.sockets[0].getsockname()[1]
        udp, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            BoardUdp, local_addr=(ip, 0))
        board_udp_port = udp.get_extra_info('sockname')[1]
        client = asyncio.create_task(board_client())
        try:
            cfg = SimpleNamespace(local_ip=ip, board_ip=ip, seed=10, seconds=5, interval=.10,
                                  count=20, burst=1, reply_timeout=2, broadcast=None, fragment=True,
                                  reconnect_every=0, rst=False, stall_seconds=0,
                                  wait_concurrent=wait_concurrent, drain_seconds=drain_seconds,
                                  board_udp_port=board_udp_port, local_udp_port=0,
                                  board_tcp_port=board_tcp_port, local_tcp_port=pc_tcp_port)
            result = await peer.run(cfg)
        finally:
            udp.close()
            listener.close()
            await listener.wait_closed()
            pending = [client, *tasks]
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
        self.assertFalse(result['hardware_pass'])
        self.assertIsNotNone(result['traffic_started_epoch'])
        self.assertGreaterEqual(result['drain_started_epoch'] - result['traffic_started_epoch'], 4.9)
        for role, stats in result['channels'].items():
            self.assertEqual(stats['verified_replies'], 20, (role, stats))
            self.assertEqual(stats['board_requests'], 1)
            self.assertEqual(stats['lost_or_unconfirmed'], 0)
            self.assertEqual(stats['invalid'], 0)
        self.assertGreater(result['concurrent_activity_seconds'], 0)


if __name__ == '__main__':
    unittest.main()
