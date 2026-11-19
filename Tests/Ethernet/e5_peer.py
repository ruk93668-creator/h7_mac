"""E5 full-duplex peer. Run on the directly connected test PC, not on the MCU."""
import argparse
import asyncio
from collections import deque
import contextlib
from dataclasses import dataclass, field
import ipaddress
import json
from pathlib import Path
import random
import socket
import struct
import time
import zlib

HEADER = struct.Struct('!4sBBHII')
REQUEST, REPLY = 1, 2
MAX_PAYLOAD = 1456
SIZES = (1, 16, 64, 256, 512, 1200, MAX_PAYLOAD)


def encode(kind, sequence, body):
    if kind not in (REQUEST, REPLY) or len(body) > MAX_PAYLOAD:
        raise ValueError('invalid E5 frame')
    return HEADER.pack(b'H7E5', 1, kind, len(body), sequence, zlib.crc32(body)) + body


def header(data):
    magic, version, kind, length, seq, crc = HEADER.unpack(data)
    if magic != b'H7E5' or version != 1 or kind not in (REQUEST, REPLY) or length > MAX_PAYLOAD:
        raise ValueError('invalid E5 header')
    return kind, length, seq, crc


def decode(data):
    if len(data) < HEADER.size:
        raise ValueError('short E5 frame')
    kind, length, seq, crc = header(data[:HEADER.size])
    body = data[HEADER.size:]
    if length != len(body) or zlib.crc32(body) != crc:
        raise ValueError('E5 length/CRC mismatch')
    return kind, seq, body


async def read_frame(reader):
    prefix = await reader.readexactly(HEADER.size)
    _, length, _, _ = header(prefix)
    return decode(prefix + await reader.readexactly(length))


@dataclass
class Metrics:
    requests_sent: int = 0
    board_requests: int = 0
    peer_replies_sent: int = 0
    verified_replies: int = 0
    tx_bytes: int = 0
    rx_bytes: int = 0
    lost_or_unconfirmed: int = 0
    duplicates: int = 0
    reordered: int = 0
    invalid: int = 0
    unexpected_replies: int = 0
    errors: int = 0
    connections: int = 0
    disconnects: int = 0
    rtt_sum: float = 0.0
    rtt_min: float | None = None
    rtt_max: float | None = None
    first_active: float | None = None
    last_active: float | None = None
    max_activity_gap_seconds: float = 0.0

    def active(self):
        now = time.monotonic()
        if self.first_active is None:
            self.first_active = now
        if self.last_active is not None:
            self.max_activity_gap_seconds = max(self.max_activity_gap_seconds, now - self.last_active)
        self.last_active = now

    def summary(self, elapsed):
        d = vars(self).copy()
        d['rx_mbps'] = self.rx_bytes * 8 / elapsed / 1e6
        d['tx_mbps'] = self.tx_bytes * 8 / elapsed / 1e6
        d['loss_ratio'] = self.lost_or_unconfirmed / self.requests_sent if self.requests_sent else None
        d['rtt_avg_ms'] = self.rtt_sum / self.verified_replies if self.verified_replies else None
        d['rtt_min_ms'] = self.rtt_min
        d['rtt_max_ms'] = self.rtt_max
        return d


class Exchange:
    # One instance per TCP connection or UDP lifetime; bounded history and in-flight requests.
    def __init__(self, metrics, random_seed=None):
        self.metrics = metrics
        self.rng = random.Random(random_seed) if random_seed is not None else None
        self.seq = 0
        self.pending = {}
        self.high = {REQUEST: 0, REPLY: 0}
        self.seen = set()
        self.history = deque()

    def request(self, timeout=5):
        self.expire(timeout)
        if len(self.pending) >= 32:
            return None
        self.seq += 1
        size = SIZES[(self.seq - 1) % len(SIZES)]
        body = (self.rng.randbytes(size) if self.rng else
                bytes((self.seq + i) & 255 for i in range(size)))
        self.pending[self.seq] = (time.monotonic(), body)
        self.metrics.requests_sent += 1
        self.metrics.tx_bytes += size
        self.metrics.active()
        return encode(REQUEST, self.seq, body)

    def expire(self, timeout=5):
        now = time.monotonic()
        for seq in list(self.pending):
            if now - self.pending[seq][0] >= timeout:
                self.pending.pop(seq)
                self.metrics.lost_or_unconfirmed += 1

    def finish(self):
        self.metrics.lost_or_unconfirmed += len(self.pending)
        self.pending.clear()

    def receive(self, kind, seq, body):
        m = self.metrics
        m.active()
        m.rx_bytes += len(body)
        key = kind, seq
        if key in self.seen:
            m.duplicates += 1
            return None
        if seq < self.high[kind]:
            m.reordered += 1
        self.high[kind] = max(self.high[kind], seq)
        self.seen.add(key)
        self.history.append(key)
        if len(self.history) > 4096:
            self.seen.remove(self.history.popleft())
        if kind == REQUEST:
            m.board_requests += 1
            m.peer_replies_sent += 1
            m.tx_bytes += len(body)
            return encode(REPLY, seq, body)
        sample = self.pending.pop(seq, None)
        if sample is None:
            m.unexpected_replies += 1
        elif sample[1] != body:
            m.invalid += 1
            m.lost_or_unconfirmed += 1
        else:
            rtt = (time.monotonic() - sample[0]) * 1000
            m.verified_replies += 1
            m.rtt_sum += rtt
            m.rtt_min = rtt if m.rtt_min is None else min(m.rtt_min, rtt)
            m.rtt_max = rtt if m.rtt_max is None else max(m.rtt_max, rtt)
        return None


class DatagramPeer(asyncio.DatagramProtocol):
    def __init__(self, cfg, metrics):
        self.config = cfg
        self.exchange = Exchange(metrics, cfg.seed)

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, packet, addr):
        if addr != (self.config.board_ip, self.config.board_udp_port):
            return
        try:
            answer = self.exchange.receive(*decode(packet))
        except ValueError:
            self.exchange.metrics.invalid += 1
            return
        if answer:
            self.transport.sendto(answer, addr)

    def error_received(self, exc):
        self.exchange.metrics.errors += 1

    async def send(self, traffic, stopping):
        await traffic.wait()
        for count in range(self.config.count):
            if stopping.is_set():
                break
            packet = self.exchange.request(self.config.reply_timeout)
            if packet is not None:
                dst = self.config.broadcast if self.config.broadcast and count % 2 else self.config.board_ip
                self.transport.sendto(packet, (dst, self.config.board_udp_port))
            await asyncio.sleep(self.config.interval)


def reset_socket(writer):
    fmt = 'HH' if __import__('os').name == 'nt' else 'ii'
    writer.get_extra_info('socket').setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack(fmt, 1, 0))
    writer.transport.abort()


async def run_connection(reader, writer, cfg, metrics, role, traffic, stopping):
    exchange = Exchange(metrics, cfg.seed)
    metrics.connections += 1
    write_lock = asyncio.Lock()

    async def write(data, split=False):
        async with write_lock:
            if split and len(data) > 5:
                writer.write(data[:5])
                await writer.drain()
                await asyncio.sleep(0.01)
                data = data[5:]
            writer.write(data)
            await writer.drain()

    async def receive():
        if cfg.stall_seconds and role == 'pc_client':
            writer.transport.pause_reading()
            try:
                await asyncio.sleep(cfg.stall_seconds)
            finally:
                if not writer.is_closing():
                    writer.transport.resume_reading()
        while True:
            response = exchange.receive(*await read_frame(reader))
            if response is not None:
                await write(response)

    async def send():
        await traffic.wait()
        remaining = cfg.count
        while remaining > 0 and not stopping.is_set():
            n = min(remaining, cfg.burst)
            frames = [p for p in (exchange.request(cfg.reply_timeout) for _ in range(n)) if p]
            if frames:
                await write(b''.join(frames), cfg.fragment)
                remaining -= len(frames)
            await asyncio.sleep(cfg.interval)
        await asyncio.Future()  # keep echoing responses until the timed run completes

    tasks = [asyncio.create_task(receive()), asyncio.create_task(send())]
    try:
        done, _ = await asyncio.wait(tasks, timeout=cfg.reconnect_every or None,
                                     return_when=asyncio.FIRST_COMPLETED)
        if not done and cfg.rst:
            reset_socket(writer)
        for task in done:
            task.result()
    except asyncio.IncompleteReadError as exc:
        if exc.partial:
            metrics.invalid += 1
    except (OSError, ValueError):
        metrics.errors += 1
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        exchange.finish()
        metrics.disconnects += 1
        writer.close()
        with contextlib.suppress(OSError, asyncio.TimeoutError):
            await asyncio.wait_for(writer.wait_closed(), 1)


async def run(cfg):
    stats = {key: Metrics() for key in ('pc_client', 'pc_server', 'udp')}
    accepted = set()
    started = time.monotonic()
    traffic = asyncio.Event()
    stopping = asyncio.Event()
    connected = asyncio.Event()
    roles = set()
    setup_errors = 0
    traffic_started_epoch = None
    drain_started_epoch = None
    wait_concurrent = getattr(cfg, 'wait_concurrent', 0)

    async def service(reader, writer, role):
        roles.add(role)
        if len(roles) == 2:
            connected.set()
        try:
            await run_connection(reader, writer, cfg, stats[role], role, traffic, stopping)
        finally:
            roles.discard(role)
            connected.clear()

    async def incoming(reader, writer):
        if writer.get_extra_info('peername')[0] != cfg.board_ip or accepted:
            writer.close()
            await writer.wait_closed()
            return
        task = asyncio.current_task()
        accepted.add(task)
        try:
            await service(reader, writer, 'pc_server')
        finally:
            accepted.discard(task)

    async def outgoing():
        nonlocal setup_errors
        while True:
            try:
                reader, writer = await asyncio.wait_for(asyncio.open_connection(
                    cfg.board_ip, cfg.board_tcp_port, local_addr=(cfg.local_ip, 0)), 3)
                await service(reader, writer, 'pc_client')
            except (OSError, asyncio.TimeoutError):
                if traffic.is_set():
                    stats['pc_client'].errors += 1
                else:
                    setup_errors += 1
            await asyncio.sleep(0.5)

    server = await asyncio.start_server(incoming, cfg.local_ip, cfg.local_tcp_port)
    udp = DatagramPeer(cfg, stats['udp'])
    transport = None
    tasks = []
    try:
        transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            lambda: udp, local_addr=(cfg.local_ip, cfg.local_udp_port), allow_broadcast=bool(cfg.broadcast))
        tasks = [asyncio.create_task(outgoing()), asyncio.create_task(udp.send(traffic, stopping))]
        print('E5_PEER_READY', flush=True)
        try:
            if wait_concurrent:
                await asyncio.wait_for(connected.wait(), wait_concurrent)
        except asyncio.TimeoutError:
            print('E5_CONCURRENT_TIMEOUT', flush=True)
        else:
            traffic_started_epoch = time.time()
            traffic.set()
            print('E5_TRAFFIC_STARTED', flush=True)
            await asyncio.sleep(cfg.seconds)
            drain_started_epoch = time.time()
            stopping.set()
            print('E5_DRAINING', flush=True)
            await asyncio.sleep(getattr(cfg, 'drain_seconds', 0))
    finally:
        server.close()
        tasks += list(accepted)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        await server.wait_closed()
        if transport is not None:
            transport.close()
        udp.exchange.finish()
    elapsed = time.monotonic() - started
    starts = [m.first_active for m in stats.values()]
    ends = [m.last_active for m in stats.values()]
    overlap = max(0, min(ends) - max(starts)) if all(t is not None for t in starts + ends) else 0
    return dict(elapsed_seconds=elapsed, concurrent_activity_seconds=overlap,
                setup_errors=setup_errors, traffic_started_epoch=traffic_started_epoch,
                drain_started_epoch=drain_started_epoch,
                config={k: v for k, v in vars(cfg).items() if k != 'output'},
                channels={k: m.summary(elapsed) for k, m in stats.items()},
                hardware_pass=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--local-ip', required=True, type=lambda x: str(ipaddress.IPv4Address(x)))
    parser.add_argument('--board-ip', required=True, type=lambda x: str(ipaddress.IPv4Address(x)))
    parser.add_argument('--board-udp-port', type=int, default=5001)
    parser.add_argument('--local-udp-port', type=int, default=5002)
    parser.add_argument('--board-tcp-port', type=int, default=5003)
    parser.add_argument('--local-tcp-port', type=int, default=5004)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--interval', type=float, default=0.05)
    parser.add_argument('--count', type=int, default=100000)
    parser.add_argument('--burst', type=int, default=4, choices=range(1, 9))
    parser.add_argument('--reply-timeout', type=float, default=5)
    parser.add_argument('--broadcast', type=lambda x: str(ipaddress.IPv4Address(x)))
    parser.add_argument('--seed', type=int, help='optional reproducible random payloads')
    parser.add_argument('--fragment', action='store_true')
    parser.add_argument('--reconnect-every', type=float, default=0)
    parser.add_argument('--rst', action='store_true', help='reset instead of FIN at reconnect interval')
    parser.add_argument('--stall-seconds', type=float, default=0)
    parser.add_argument('--wait-concurrent', type=float, default=0,
                        help='wait this many seconds for both TCP roles before generating requests')
    parser.add_argument('--drain-seconds', type=float, default=0,
                        help='stop generating requests and wait for replies before closing sockets')
    args = parser.parse_args()
    if (not 1 <= args.seconds <= 86400 or not .001 <= args.interval <= 30 or
            not 0 <= args.count <= 1000000 or not 0.1 <= args.reply_timeout <= 60 or
            not 0 <= args.reconnect_every <= 3600 or not 0 <= args.stall_seconds <= 60 or
            not 0 <= args.wait_concurrent <= 300 or not 0 <= args.drain_seconds <= 60 or
            not all(1 <= p <= 65535 for p in
                    (args.board_udp_port, args.local_udp_port, args.board_tcp_port, args.local_tcp_port))):
        parser.error('invalid duration/count')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Reserve a unique evidence path before opening sockets.
    with args.output.open('x', encoding='utf-8') as output:
        result = asyncio.run(run(args))
        json.dump(result, output, indent=2)
    print(json.dumps(result))
    if result['traffic_started_epoch'] is None:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
