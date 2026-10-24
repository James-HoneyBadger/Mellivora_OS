#!/usr/bin/env python3
"""packet_fuzz.py — Lightweight malformed-packet generator for Mellivora's
network stack (Phase 4 enhancement).

Generates a stream of malformed Ethernet / ARP / IPv4 / TCP / UDP frames
to exercise kernel/net.inc parsers under hostile input. It does NOT
inject into a live NIC; it writes raw frames to a file (or stdout) so
they can be replayed via QEMU's -netdev dump=file=… or fed to a future
kernel-side replay harness.

Usage:
    python3 tools/packet_fuzz.py [--count N] [--seed S]
                                              [--out FILE]

Output format: pcap-savefile-compatible "raw" stream, each frame
prefixed by a uint32 little-endian length. This keeps the script
dependency-free (no scapy required).
"""

from __future__ import annotations

import argparse
import os
import random
import struct
import sys
from typing import Callable


def make_eth_runt(rng: random.Random) -> bytes:
    """Ethernet frame shorter than the 14-byte header."""
    return os.urandom(rng.randint(0, 13))


def make_arp_oversize(rng: random.Random) -> bytes:
    """ARP packet claiming hlen/plen far larger than reality."""
    eth = b"\xff" * 6 + b"\x00" * 6 + b"\x08\x06"
    # htype=1, ptype=0x0800, but hlen=255, plen=255, op=1
    arp = struct.pack(">HHBBH", 1, 0x0800, 255, 255, 1)
    arp += os.urandom(rng.randint(8, 60))
    return eth + arp


def make_ipv4_short_total(rng: random.Random) -> bytes:
    """IPv4 datagram with total_length < 20."""
    eth = b"\xff" * 6 + b"\x00" * 6 + b"\x08\x00"
    # ver=4, ihl=5, tos=0, total_len=8 (impossibly short)
    ip = struct.pack(">BBHHHBBH4s4s",
                     0x45, 0, 8, 0, 0, 64, 6, 0,
                     b"\x0a\x00\x00\x01", b"\x0a\x00\x00\x02")
    ip += os.urandom(rng.randint(0, 40))
    return eth + ip


def make_tcp_huge_doff(rng: random.Random) -> bytes:
    """TCP segment with data offset declaring more header than packet."""
    eth = b"\xff" * 6 + b"\x00" * 6 + b"\x08\x00"
    ip = struct.pack(">BBHHHBBH4s4s",
                     0x45, 0, 40, 0, 0x4000, 64, 6, 0,
                     b"\x0a\x00\x00\x01", b"\x0a\x00\x00\x02")
    # data offset = 15 (= 60 bytes header) but only 20 bytes of TCP data follow
    tcp = struct.pack(">HHIIBBHHH",
                      1234, 80, 0, 0, 0xF0, 0x18, 0xFFFF, 0, 0)
    return eth + ip + tcp


def make_udp_negative_len(rng: random.Random) -> bytes:
    """UDP datagram whose length field is < 8."""
    eth = b"\xff" * 6 + b"\x00" * 6 + b"\x08\x00"
    ip = struct.pack(">BBHHHBBH4s4s",
                     0x45, 0, 28, 0, 0, 64, 17, 0,
                     b"\x0a\x00\x00\x01", b"\x0a\x00\x00\x02")
    udp = struct.pack(">HHHH", 1234, 53, 1, 0)  # len=1 (< 8 mandatory)
    return eth + ip + udp


def make_random_garbage(rng: random.Random) -> bytes:
    """Pure random bytes, sized 14..1500."""
    return os.urandom(rng.randint(14, 1500))


GENERATORS: list[Callable[[random.Random], bytes]] = [
    make_eth_runt,
    make_arp_oversize,
    make_ipv4_short_total,
    make_tcp_huge_doff,
    make_udp_negative_len,
    make_random_garbage,
]


def main() -> int:
    ap = argparse.ArgumentParser(description="Mellivora packet fuzzer")
    ap.add_argument("--count", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=0xBADBADBA)
    ap.add_argument("--out", default="-",
                    help="output file (default: stdout)")
    args = ap.parse_args()

    rng = random.Random(args.seed)

    if args.out == "-":
        out = sys.stdout.buffer
    else:
        out = open(args.out, "wb")

    try:
        for _ in range(args.count):
            gen = rng.choice(GENERATORS)
            frame = gen(rng)
            out.write(struct.pack("<I", len(frame)))
            out.write(frame)
    finally:
        if out is not sys.stdout.buffer:
            out.close()

    print(f"Wrote {args.count} fuzz frames to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
