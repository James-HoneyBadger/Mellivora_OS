#!/usr/bin/env python3
"""qemu_fuzznode.py - feed a running Mellivora guest malformed frames.

NETWORK.md section 9 item 2 asks for this: "packet_fuzz feeds 1 000 000
malformed frames and the stack neither panics nor loses a valid connection".
`tools/packet_fuzz.py` generates the frames; until now there was nowhere to
send them, so the item was a generator with no gate. This is the gate.

One guest is booted with its NIC on a TCP socket netdev - the same wire the
two-machine suite uses, with the host as the only peer - and the host then:

  1. writes N malformed frames, taken from packet_fuzz's generators, framed the
     way QEMU's socket netdev expects (a 4-byte big-endian length, not the
     little-endian one the tool's file format uses);
  2. proves the stack still answers afterwards, using the guest's own console:
     `ifconfig` has to print the interface, `ping 127.0.0.1` has to come back
     4/4, and a loopback share has to mount and list.

The run fails on a fault on the serial console ("CPU EXCEPTION", "PAGE FAULT",
"double fault", "triple fault"), on a guest that stops taking frames at all, or
on any of those three checks not answering. A guest that survives the storm and
then cannot ping itself has lost the connection, which is the other half of the
requirement.

The wire is written as fast as QEMU accepts frames, so a long storm leaves a
backlog - QEMU queues frames and hands them to the ring as the guest makes room
- and the console gets its turn only once that drains. The first check is given
`--answer-timeout` seconds for exactly that reason. A machine whose receive
interrupt is spinning never answers however long it is given, which is the
distinction the check is making.

The liveness proof is deliberately made of the guest's own paths. An earlier
version answered ARP and ICMP from the host so the guest could `ping` the peer;
the emulated link did not cooperate (ARP replies went unaccepted, ICMP replies
arrived after the ping had timed them out), and a host that answers badly is
not evidence about the guest. Loopback exercises the same receive, ICMP and
socket code without asking the host to be part of the proof.

`--only N` runs a single generator. It is a diagnosis flag, not part of the
gate: every generator on its own survives 60 000 frames, and it is the mix that
does not.

It is not green. Mixed, the wire stops moving after 12 000-15 000 frames: the
machine is idle rather than faulting, its console answers again once nothing
more arrives, and the receive ring is left with its read and write pointers a
few bytes apart - the one state the NIC will neither deliver into nor interrupt
about. docs/spec/NETWORK.md section 9 item 2 records it, and
docs/ROADMAP.md records the three defects the storm has already found and the
one it has not.

Usage:
    python3 tools/qemu_fuzznode.py --image mellivora.img [--count N]
                                   [--seed S] [--rate FPS] [--only N]
                                   [--boot-timeout S] [--answer-timeout S]
                                   [--stall-timeout S] [--keep] [--json]

Exits 0 when the guest survives, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import select
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import packet_fuzz as pf          # noqa: E402  the frame generators
import qemu_twonode as qt         # noqa: E402  the single-node driver
import qemu_drive as qd           # noqa: E402  monitor and serial plumbing


def eth(dst: bytes, src: bytes, ethertype: int, payload: bytes) -> bytes:
    return dst + src + struct.pack(">H", ethertype) + payload


class _Stop(Exception):
    """Leave the run early: the reason is already in `detail`."""


class Stalled(Exception):
    """The guest stopped taking frames off the wire."""


class Peer:
    """The host end of the wire: frames out, and a reader that keeps QEMU free.

    QEMU's socket netdev prefixes every frame with its length in *network*
    order. packet_fuzz's own file format is little-endian; that difference is
    the kind of thing that produces a run where every frame is silently the
    wrong length, so it is spelled out here rather than remembered.
    """

    def __init__(self, sock: socket.socket, stall: float = 60.0) -> None:
        self.sock = sock
        self.stall = stall
        self.closed = threading.Event()
        self.received = 0
        self._reader = threading.Thread(target=self._read_loop, daemon=True)

    def start(self) -> None:
        self._reader.start()

    def _read_loop(self) -> None:
        # The reader exists so QEMU never blocks writing to us: a guest whose
        # replies fill the socket's send buffer stalls, and a stalled guest
        # looks exactly like a fuzzer that broke it.
        try:
            while not self.closed.is_set():
                header = self._recv_exact(4)
                if header is None:
                    break
                length = struct.unpack(">I", header)[0]
                if length == 0 or length > 65536:
                    break
                if self._recv_exact(length) is None:
                    break
                self.received += 1
        except OSError:
            pass
        finally:
            self.closed.set()

    def _recv_exact(self, count: int) -> bytes | None:
        chunks = b""
        while len(chunks) < count:
            try:
                part = self.sock.recv(count - len(chunks))
            except OSError:
                return None
            if not part:
                return None
            chunks += part
        return chunks

    def send(self, frames: list[bytes]) -> None:
        blob = b"".join(struct.pack(">I", len(f)) + f for f in frames)
        view = memoryview(blob)
        while view:
            # A guest that stops taking frames has lost the connection, which
            # is the failure this gate exists to catch - but it looks exactly
            # like a fuzzer blocked on a full socket. Wait for room instead of
            # blocking in sendall, so the run ends with a reason rather than
            # being killed by its own timeout fifteen minutes later.
            try:
                _, writable, _ = select.select([], [self.sock], [], self.stall)
            except OSError as exc:
                raise Stalled(str(exc)) from exc
            if not writable:
                raise Stalled(f"no room on the wire for {self.stall:.0f}s")
            view = view[self.sock.send(view):]

    def close(self) -> None:
        self.closed.set()
        try:
            self.sock.close()
        except OSError:
            pass


def fuzz_frames(count: int, seed: int, only: int = -1) -> list[bytes]:
    """A batch of malformed frames, from packet_fuzz's generators.

    `only` picks one generator by index, which is how a storm that wedges the
    machine is narrowed to the frame that did it.
    """
    rng = random.Random(seed)
    pool = pf.GENERATORS if only < 0 else [pf.GENERATORS[only]]
    return [rng.choice(pool)(rng) for _ in range(count)]


def storm(peer: Peer, count: int, seed: int, batch: int, only: int = -1,
          rate: float = 0.0) -> int:
    """Write *count* frames, in batches rather than one syscall per frame.

    `rate` is frames per second, or 0 for as fast as the wire takes them. A
    paced storm is not a weaker test: a wire has a rate, and QEMU's socket
    backend will queue a burst far faster than an emulated NIC and a driver can
    retire it, which measures the queue rather than the stack. Pacing puts the
    frames in front of the driver at a rate the driver can be asked to keep up
    with.
    """
    batch = max(1, min(batch, count))
    started = time.time()
    sent = 0
    while sent < count:
        frames = fuzz_frames(min(batch, count - sent), seed + sent, only)
        try:
            peer.send(frames)
        except Stalled as exc:
            raise Stalled(sent) from exc
        sent += len(frames)
        if rate > 0:
            due = started + sent / rate
            delay = due - time.time()
            if delay > 0:
                time.sleep(delay)
    return sent


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", required=True)
    ap.add_argument("--count", type=int, default=1_000_000)
    ap.add_argument("--seed", type=int, default=0xBADBADBA)
    ap.add_argument("--boot-timeout", type=float, default=180)
    ap.add_argument("--only", type=int, default=-1,
                    help="send only this packet_fuzz generator (index)")
    ap.add_argument("--answer-timeout", type=float, default=300.0,
                    help="seconds the first liveness check may wait for the "
                         "backlog to drain before the machine is called hung")
    ap.add_argument("--rate", type=float, default=2000.0,
                    help="frames per second to offer, or 0 for as fast as the "
                         "wire takes them (see the note in storm())")
    ap.add_argument("--stall-timeout", type=float, default=60.0,
                    help="seconds without room on the wire before the guest is "
                         "called a lost connection")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    image = str(Path(args.image).resolve())
    if not os.path.isfile(image):
        print(f"FAIL: image not found: {image}", file=sys.stderr)
        return 1
    try:
        qemu = qd.find_qemu()
    except SystemExit as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    workdir = tempfile.mkdtemp(prefix="mellivora-fuzz-")
    port = qt.free_port()
    netdev = f"socket,id=net0,listen=127.0.0.1:{port}"
    # The tag picks the MAC out of the two-machine suite's table, so this
    # guest is addressable by the constants at the top of this file.
    node = qt.Node("a", image, qemu, netdev, workdir)
    peer: Peer | None = None
    fault = ""
    detail = ""
    ok = True
    result: dict[str, object] = {"count": args.count, "seed": args.seed}
    # Each liveness check, so a reader of the report can see which one broke
    # without parsing the detail sentence.
    checks = {"ifconfig": False, "ping": False, "share": False}

    try:
        node.start()
        if not qt.wait_listening(port, 30):
            ok = False
            detail = "the wire never started listening"
        else:
            # QEMU accepts one peer: this is it.
            peer = Peer(socket.create_connection(("127.0.0.1", port)),
                        stall=args.stall_timeout)
            peer.start()
            node.connect()
            booted, why = node.boot(args.boot_timeout)
            if not booted:
                ok = False
                detail = why
            else:
                started = time.time()
                try:
                    sent = storm(peer, args.count, args.seed, 512, args.only,
                                 args.rate)
                except Stalled as exc:
                    ok = False
                    sent = exc.args[0]
                    detail = (f"the guest stopped reading the wire after {sent} "
                              "frames; a receiver that stops taking frames has "
                              "lost the connection")
                    if exc.__cause__:
                        detail += f" ({exc.__cause__})"
                result["sent"] = sent
                result["storm_seconds"] = round(time.time() - started, 1)
                result["frames_from_guest"] = peer.received
                if not ok:
                    raise _Stop

                # Liveness is proved on the machine's own paths rather than on
                # a round trip with this peer: the peer can inject and count,
                # but it is not a host (see the note at the top), and a check
                # that depends on it would be measuring the harness.
                # The wire is written as fast as QEMU will take frames, so at
                # the end of a long storm there is a backlog to drain before
                # the console gets a turn. That is slowness, not a hang: the
                # first check waits it out, and only a machine that never
                # answers is called hung.
                _ok, why = node.run(["ifconfig 10.0.0.9"], ["IP set to 10.0.0.9"],
                                    [], args.answer_timeout)
                checks["ifconfig"] = _ok
                if not _ok:
                    ok = False
                    detail = f"the console stopped answering: {why}"
                else:
                    _ok, why = node.run(["ping 127.0.0.1"],
                                        ["4/4 packets received"], [], 60)
                    checks["ping"] = _ok
                    if not _ok:
                        ok = False
                        detail = f"the stack lost its own echo: {why}"
                    else:
                        # One command per step: the console buffers nothing
                        # while a command runs, so three lines sent together
                        # lose the tail of the second and third.
                        _ok, why = node.run(["net share docs /docs s3cret"],
                                            ["Shared: docs = /docs"], [], 60)
                        if _ok:
                            _ok, why = node.run(
                                ["net use 127.0.0.1 docs /mnt s3cret"],
                                ["Mounted /mnt from docs"], [], 90)
                        if _ok:
                            _ok, why = node.run(["dir /mnt"], ["readme.txt"],
                                                [], 60)
                        checks["share"] = _ok
                        if not _ok:
                            ok = False
                            detail = f"a TCP session failed after the storm: {why}"
                found = qt.FAULT_RE.search(node.say())
                fault = found.group(0) if found else ""
                if fault:
                    ok = False
                    detail = f"the guest faulted: {fault}"
    except _Stop:
        pass
    finally:
        result["checks"] = checks
        if fault:
            result["fault"] = fault
        node.save_transcript()
        node.stop()
        if peer:
            peer.close()
        if not ok or args.keep:
            print(f"transcripts: {workdir}", file=sys.stderr)
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    if args.json:
        print(json.dumps({"ok": ok, "detail": detail, **result}, indent=2))
    elif ok:
        print(f"FUZZ_OK frames={result.get('sent')} "
              f"seconds={result.get('storm_seconds')} "
              f"answers={result.get('frames_from_guest')}")
    else:
        print(f"FUZZ_FAIL {detail}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
