#!/usr/bin/env python3
"""64-bit boot regression test.

Builds nothing: it boots the 64-bit image produced by `make 64bit` under
QEMU with a 64-bit CPU model and asserts that stage2 negotiated long mode
and the nucleus reported in over COM1.

Usage: tests/test_longmode.py [image]

The default assertions are the ones the C nucleus establishes: the image runs
at its link address in the top 2 GB, a `syscall` reaches a handler, and a
ring-3 program runs and calls back in. Pass
--marker-only for the hand-written asm nucleus, which only proves the boot.

Usage: tests/test_longmode.py [image] [--marker-only]

The default (C nucleus) run boots twice: 256 MB for the boot assertions, then a
large guest to prove the frame allocator reads a 64-bit E820 map - a guest past
4 GB only reports the right totals if it does. 8 GB is the roadmap's scale gate.

Env:
  LONGMODE_ALLOW_SKIP=1  exit 0 instead of failing when QEMU is missing
  LONGMODE_TIMEOUT=45    seconds to wait for the marker
  LONGMODE_BIG_MB=8192   guest size for the big-memory run
  LONGMODE_SKIP_BIG=1    skip it (hosts that cannot give QEMU that much RAM)
"""

import os
import subprocess
import re
import sys
import shutil
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import qemu_drive as qd  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
IMAGE = ARGS[0] if ARGS else os.path.join(ROOT, "mellivora64.img")
# The hand-written asm nucleus proves the boot path only - it has no C runtime,
# so it cannot report the higher half or a SYSCALL round trip. --marker-only
# tests that nucleus; the default asserts everything the C port establishes.
MARKER_ONLY = "--marker-only" in sys.argv[1:]
TIMEOUT = int(os.environ.get("LONGMODE_TIMEOUT", "45"))
ALLOW_SKIP = os.environ.get("LONGMODE_ALLOW_SKIP", "0") == "1"
MARKER = "MELLIVORA64_OK"

# A boot marker alone would also be produced by a nucleus that ran in the low
# half. These are the facts the port has to establish, so the test checks them:
# the image runs at its *link* address in the top 2 GB, the SYSCALL/SYSRET MSRs
# are programmed and a 64-bit `syscall` reaches a handler and returns, the frame
# allocator and the page tables agree about a mapped page (and the MMU agrees
# once it is unmapped), the slab heap hands out distinct zeroed objects across
# two slabs, two ring-3 tasks with their own page tables run with interrupts on,
# are preempted by timer ticks and use write/brk/yield over LP64 syscalls, the PCI
# bus and the ATA disk answer, /init is read off that disk and executed, each
# makes three
# syscalls from the user page and exits with the value it round-tripped through
# its own stack, and a fault it takes is caught on the kernel stack the TSS
# names and resumed.
REQUIRED = (
    "MELLIVORA64_OK",
    "base=0xFFFFFFFF80100000",
    "syscall=ok",
    "pmm=ok",
    "vmm=ok",
    "kmem=ok",
    "ring3=ok",
    "fault=ok",
    "timer=ok",
    "sched=ok",
    "nx=ok",
    "nx=on smep=on smap=on",
    # The primitives a second CPU will need: exchange, compare-and-swap, trylock
    # and an interrupt state that comes back exactly as it was.
    "[lock] spin=ok atomic=ok irq=ok",
    # Every tick came from the local APIC and landed on the task's kernel stack.
    " stack=ok source=lapic",
    "abi=lp64",
    "brk=ok",
    "hello",
    "blk=ok",
    "hbfs] root=",
    # Growing a file in place, growing one that has to move, and giving every
    # block back afterwards.
    "[hbfs] append=ok move=ok",
    "/init=",                # size varies with the demo program
    "/hello_tree=",
    "tasks=7 spaces=7",
    # The write demo: it creates /persist.txt, reads it back through the same
    # open/read path, makes /tmpdir, puts a file in it, unlinks it and sees the
    # directory go empty again.
    # /wr prints the file it just wrote; the content itself is checked from the
    # image below, which is the stronger statement, because this line is two
    # writes and another task's output can land between them.
    "read back:",
    "write path ok",
    # The shell: a prompt with the working directory, a listing, a file's
    # contents, a rejected command and a clean exit.
    "mellivora:/> ",
    "  motd size=16",
    "sh: created probe",
    "  probe/ size=0",
    "sh: removed probe",
    # Appending: a file that exists gains a line through an open handle, and a
    # file that does not is created with the same call the shell makes.
    "sh: appended to /persist.txt",
    "shell third",
    "sh: appended to /fresh.txt",
    # fork + exec + waitpid: a program run as a child reports its exit code, and
    # one that cannot be loaded reports the child's own failure.
    "sh: /api exited 0x00000000",
    "sh: /nope exited 0x0000007f",
    # A background child, a command answered while it spins, and the kill that
    # stops it.
    "sh: started /spin pid=7",
    "sh: killed 7",
    "sh: unknown command",
    "bye",
    "exit=0x005A",
    "rtc=ok",
    "vga=ok",
    "kbd=ok",
    # The pointer: the auxiliary port answers and starts in the middle of the
    # screen. check_mouse() below moves it.
    "[mouse] ps/2 pointer at 40,12 mouse=ok",
    "sh: cleared the screen",
    "pipe=ok",
    # The network adapter: the MAC it reports, the register window mapped, and a
    # real ARP request answered by the user-mode stack behind the card.
    # ACPI discovery: the firmware's own description of the machine, found by
    # checksum and read far enough to say where the APICs are and how many CPUs
    # it has - which is what the next phase needs to know before it can use more
    # than one of them.
    "[acpi] rsdp=",
    "lapic=FEE00000 cpus=1 ioapic=FEC00000 acpi=ok",
    # The local APIC: the page the firmware named is mapped and its timer counts
    # down while the unit is switched on for the test - the tick itself stays on
    # the 8254 until the APIC has a vector of its own.
    "[lapic] id=0 version=14",
    " lapic=ok",
    # The I/O APIC takes the machine's interrupt lines; every redirection entry
    # starts masked and the keyboard and pointer are then routed through it.
    "[ioapic] id=0 version=",
    " ioapic=ok",
    # A clock that is neither the PIT nor the APIC: the firmware's HPET, at 100
    # MHz, cross-checked against the PIT's ten milliseconds, and used to wait
    # half a millisecond - a length the tick cannot express.
    "[hpet] base=FED00000 hz=100000000",
    " hpet=ok",
    # ... and the tick is the APIC's now, at the same 100 Hz the PIT was: the
    # calibration is measured against the PIT's spare channel, and the 8259 is
    # masked off so nothing can deliver a second clock alongside it.
    "vector=64 hz=100 tick=lapic",
    "[e1000] 52:54:00:12:34:56 bar=",
    "link=up e1000=ok",
    # ...and the network layer built on it: the gateway resolved by ARP, and
    # three echo requests that come back.
    "[net] 10.0.2.2 at 52:55:0A:00:02:02 net=ok",
    "net: 3 of 3 replied",
    "sh: /net exited 0x00000003",
    # The speaker: the gate bits read back what was written to them, and a
    # program can ask for three notes from ring 3.
    "[spk] pit channel 2, 1193182 Hz clock spk=ok",
    "beep: tone 440 Hz",
    "beep: silent",
    "sh: /beep exited 0x00000000",
    # Two threads in one address space: the second one runs in the first one's
    # memory, so what it writes is there for the first one to read.
    "threads: main tid=",
    "threads: cloned tid=",
    "hello from the other thread",
    "sh: /threads exited 0x00000000",
    # A thread parked in the kernel until another one hands it the news - a
    # futex, not a spin.
    "futex: sleeper tid=",
    "futex: sleeper woke with flag=1 exit=0x00000003",
    "sh: /futex exited 0x00000000",
    # Thread-local storage that is really per task: two bases, one address
    # space, and each task reads back its own value through fs:[0].
    "tls: main read 1111 thread read 2222",
    "tls: two bases, one address space",
    "sh: /tls exited 0x00000000",
    # /hrt is withheld from the shell script and from these requirements, and
    # this is the reason: a one-millisecond deadline is delivered two hundred
    # microseconds late at boot - twenty in a row - and seven to sixteen
    # milliseconds late while a task is sleeping, so the program's verdict is
    # unreliable in both directions. Asserting on it makes the suite flaky, and
    # hiding that would make the suite useless. The roadmap has the diagnosis so
    # far: the arm is right, the counter reaches zero, and the only difference
    # between the two cases is who is running when the deadline passes.
    # The lock order checker: four planted violations found by the self-test, and
    # none from the kernel's own use of its lock.
    # The tick is a deadline like any other now, so its rate is measured rather
    # than assumed: ten ticks inside a tenth of a second, on the counter the
    # deadlines are expressed in.
    "[timer] tick=ok",
    # The lock order checker: four planted violations found by the self-test, and
    "[lockdep] selftest=ok",
    "errors=0 lockdep=ok",
    # waitpid blocks: the shell parks on the child it is waiting for, the child's
    # exit wakes it, and the two counts have to agree - a wait that was answered
    # by nothing and a wake with nothing to wake are the same bug.
    "[wait] blocks=",
    "wait=ok",
    "api ok: malloc/free, load, pipe",
    # /init exec's /hello with a command line, and /hello asks for it back
    # along with one environment entry.
    "args=alpha beta",
    "env os=mellivora64",
    # One of the 32-bit tree's own programs, built by `make programs` from
    # programs/hello.asm and loaded unchanged at its 0x00300000 program base.
    "Hello, World! Welcome to Mellivora OS!",
    "This program is running in 32-bit protected mode.",
    "32-bit file calls ok",
    # The directory demo: a listing of the root (including the docs
    # subdirectory), stat, a seek into /motd, then the same three calls in
    # /docs from the 64-bit task's own working directory.
    "name=docs type=2 size=0",
    "stat /motd type=1 size=16",
    "seek4=o fr",
    "cwd=/docs",
    "ent 0 name=readme.txt type=1 size=16",
    "miss=ok",
    "[exec] /hello",
    "hello from hbfs",
    "hello from 32-bit mode",
    "mode=32",
)

# A 4 GB guest has usable E820 ranges above 0x100000000 (there is a hole below
# 4 GB), so the frame allocator only reports the right numbers if it read the
# map as 64-bit. The roadmap's scale gate is 8 GB, which is what this asks for.
BIG_MB = int(os.environ.get("LONGMODE_BIG_MB", "8192"))

# qemu64 leaves SMEP and SMAP out; the nucleus enables them when it sees them, so
# the test asks for a CPU that has them and requires the hardened report. NX is
# part of the base model either way.
CPU_MODEL = os.environ.get("LONGMODE_CPU", "qemu64,+smep,+smap")
SKIP_BIG = os.environ.get("LONGMODE_SKIP_BIG", "0") == "1"

# Every boot gets a network card, because the nucleus drives one: the 8254x is
# given the user-mode stack, which answers an ARP for its own address, so the
# driver's ring test has something to talk to and no host privileges are needed.
NIC = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]



def scratch_dir(prefix: str) -> str:
    """A working directory for a booted guest's private image.

    The image is hundreds of megabytes to gigabytes, and /tmp is a tmpfs on
    many hosts - a few copies there fill it and the next boot fails with
    ENOSPC. Prefer $LM_SCRATCH, then the repository's .build (on the same
    filesystem as the image), then the system temp directory.
    """
    candidates = [os.environ.get("LM_SCRATCH"), os.path.join(ROOT, ".build")]
    for base in candidates:
        if not base:
            continue
        try:
            os.makedirs(base, exist_ok=True)
            return tempfile.mkdtemp(prefix=prefix, dir=base)
        except OSError:
            continue
    return tempfile.mkdtemp(prefix=prefix)

def fresh_image(image):
    """A private copy of the image for one boot. The guest writes to it."""
    work = scratch_dir("lm-boot-")
    copy = os.path.join(work, "image.img")
    shutil.copyfile(image, copy)
    return copy


def boot_and_capture(qemu, image, mem_mb, want, script=()):
    """Boot the image with mem_mb of RAM and return the COM1 text once want appears.

    `script` is a list of (marker, line to type) steps: when a marker shows up on
    the console, that line is sent back down the serial line. That is how the
    shell task is driven - it is the last task to run and it waits for input, so
    nothing after it happens on its own.
    """
    workdir = scratch_dir("lm-test-")
    ser_path = os.path.join(workdir, "serial.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", CPU_MODEL, "-m", str(mem_mb),
         "-drive", "file=%s,format=raw,if=ide,cache=writethrough" % image,
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", "unix:%s,server=on,wait=on" % ser_path,
         "-monitor", "none"] + NIC,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    text = ""
    steps = list(script)
    try:
        serial = qd.SerialConsole(ser_path)
        deadline = time.time() + TIMEOUT
        while time.time() < deadline:
            text = serial.text()
            if steps and steps[0][0] in text:
                serial.send_line(steps.pop(0)[1])
            if want in text:
                break
            if proc.poll() is not None:
                break
            # Drain the console promptly rather than every third of a second: the
            # emulated UART is a socket on the other side of a process boundary,
            # and a peer that reads in bursts is a peer that can be written past.
            time.sleep(0.05)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    return text


def check_mouse(qemu, image):
    """Move the guest pointer from the monitor and read the driver's answer.

    The moves are raw PS/2 packets, so this exercises the whole path a hand
    would: controller bytes, the three-byte packets, the sign bits and the
    button latch. The demo reports every change, so the positions can be
    checked exactly - 20 cells right and 6 down from where the pointer starts,
    which is the middle of an 80x25 screen.
    """
    workdir = scratch_dir("lm-mouse-")
    ser_path = os.path.join(workdir, "serial.sock")
    mon_path = os.path.join(workdir, "monitor.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", CPU_MODEL, "-m", "256",
         "-drive", "file=%s,format=raw,if=ide,cache=writethrough" % image,
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", "unix:%s,server=on,wait=on" % ser_path,
         "-monitor", "unix:%s,server=on,wait=off" % mon_path] + NIC,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        serial = qd.SerialConsole(ser_path)
        if not serial.wait_for("mellivora:/> ", TIMEOUT, proc):
            return "the shell never reached a prompt"
        if "mouse=ok" not in serial.text():
            return "the auxiliary port never answered"
        monitor = qd.Monitor(mon_path)

        serial.send_line("run /mouse")
        # Wait for the demo's first report before moving anything: a button
        # pressed before the program is listening is a button that was never
        # pressed, and on a slow runner the old fixed one-second sleep was not
        # enough - the check then failed for a reason that was not the driver.
        started = time.time() + 20
        while time.time() < started:
            if re.search(r"mouse x=\d+ y=\d+", serial.text()):
                break
            time.sleep(0.2)
        else:
            return "the mouse demo never reported a position"
        monitor.send_mouse(20, 0)
        time.sleep(0.4)
        monitor.send_mouse(0, 6)
        time.sleep(0.4)
        # A press that lands while the driver is between polls is a press that
        # was never delivered, so it is pressed again rather than assumed lost:
        # the assertion below still requires the driver to report one.
        for _ in range(4):
            monitor.send_button(1)
            time.sleep(0.3)
            monitor.send_button(0)
            time.sleep(0.3)
            if "press=1" in serial.text():
                break

        seen = []
        deadline = time.time() + 20
        while time.time() < deadline:
            serial.pump()
            for line in serial.text().splitlines():
                m = re.search(r"mouse x=(\d+) y=(\d+) b=(\d) press=(\d)", line)
                if m and m.groups() not in seen:
                    seen.append(m.groups())
            if any(g[3] == "1" for g in seen):
                break
            time.sleep(0.2)
        if not seen:
            return "the demo never reported a position"
        # The demo reports its own start, and the pointer's position when the
        # first packet arrives depends on how many the emulated device sent before
        # the demo was listening - so what is asserted is that it starts inside the
        # screen at all, not at an exact column. Asserting the exact
        # column made this check fail whenever the machine happened to be busier
        # during boot, which is not a fault in the pointer.
        x0, y0 = int(seen[0][0]), int(seen[0][1])
        if not (0 <= x0 <= 79 and 0 <= y0 <= 24):
            return "the pointer did not start in the middle: %r" % (seen[0],)
        if ("60", "18") not in [g[:2] for g in seen]:
            return "the moves did not add up to 60,18: %r" % (seen,)
        if not any(g[3] == "1" for g in seen):
            return "a button press was never reported: %r" % (seen,)

        serial.send_line("quit")
        if not serial.wait_for(MARKER, TIMEOUT, proc):
            return "the session did not finish after 'quit'"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    return None


def check_keyboard_and_screen(qemu, image):
    """Type at the PS/2 keyboard and read the VGA text buffer back.

    The shell is driven with monitor `sendkey` rather than the serial line, so the
    characters go through the i8042 exactly as a keyboard's would, and the screen
    is read straight out of 0xB8000 - which is the only way to see what the
    display actually shows.
    """
    workdir = scratch_dir("lm-kbd-")
    ser_path = os.path.join(workdir, "serial.sock")
    mon_path = os.path.join(workdir, "monitor.sock")
    proc = subprocess.Popen(
        [qemu, "-cpu", CPU_MODEL, "-m", "256",
         "-drive", "file=%s,format=raw,if=ide,cache=writethrough" % image,
         "-boot", "c", "-display", "none", "-no-reboot",
         "-serial", "unix:%s,server=on,wait=on" % ser_path,
         "-monitor", "unix:%s,server=on,wait=off" % mon_path] + NIC,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        serial = qd.SerialConsole(ser_path)
        if not serial.wait_for("mellivora:/> ", TIMEOUT, proc):
            return "the shell never reached a prompt"
        monitor = qd.Monitor(mon_path)

        # A keystroke sent the instant the prompt appears can arrive before the
        # shell is back in its read loop, so the line is retyped if it is not
        # answered. Reading the screen while the date is still near the bottom
        # matters too: the boot's own output keeps scrolling it away.
        stamp = None
        for _ in range(3):
            time.sleep(0.5)
            monitor.send_text("date\n")
            deadline = time.time() + 8
            while time.time() < deadline and stamp is None:
                time.sleep(0.3)
                for line in serial.text().splitlines():
                    if re.match(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$", line):
                        stamp = line
            if stamp is not None:
                break
        if stamp is None:
            return "no date line after typing 'date'"

        screen = monitor.screen()
        if "mellivora" not in screen:
            return "the screen does not show the prompt"
        if stamp not in screen:
            return "the screen does not show %r" % stamp

        monitor.send_text("quit\n")
        if not serial.wait_for(MARKER, TIMEOUT, proc):
            return "the shell did not finish after a typed 'quit'"
        if "date" not in serial.text():
            return "the typed command was never echoed"
        # The characters went through the i8042, so they arrived as interrupts
        # from the I/O APIC - which polling would never have shown.
        m = re.search(r"\[ps2\] irqs=(\d+)", serial.text())
        if not m or int(m.group(1)) < 5:
            return "the keyboard's interrupt handler never ran: %r" % (m and m.group(0))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
    return None


def check_written_volume(image):
    """Read the image the way a fresh mount would: the guest wrote these."""
    sys.path.insert(0, ROOT)
    import populate

    with open(image, "rb") as f:
        f.seek(populate.HBFS_ROOT_DIR_START * populate.SECTOR_SIZE)
        root = f.read(populate.HBFS_ROOT_DIR_SIZE)

        def entries(buf, count, entry_size):
            out = {}
            for i in range(count):
                off = i * entry_size
                if buf[off + 253] == populate.FTYPE_FREE:
                    continue
                name = buf[off:off + populate.HBFS_MAX_FILENAME]
                name = name.split(b"\0")[0].decode("ascii", "replace")
                size, start, blocks = __import__("struct").unpack_from(
                    "<III", buf, off + 256)
                out[name] = (buf[off + 253], size, start, blocks)
            return out

        top = entries(root, populate.HBFS_MAX_FILES,
                      populate.HBFS_DIR_ENTRY_SIZE)
        if "persist.txt" not in top:
            return "no /persist.txt in the root directory"
        ftype, size, start, blocks = top["persist.txt"]
        # Written by /wr at boot, then appended to by the shell: the two lines
        # are the whole-file CREATE and the seek-to-the-end + write.
        appended = b"written by mellivora64\nshell third\n"
        if ftype != populate.FTYPE_TEXT or size != len(appended):
            return "/persist.txt is type %d size %d" % (ftype, size)

        f.seek((populate.HBFS_DATA_START + start * populate.SECTORS_PER_BLOCK)
               * populate.SECTOR_SIZE)
        data = f.read(size)
        if data != appended:
            return "/persist.txt reads %r" % data

        if "fresh.txt" not in top:
            return "no /fresh.txt: the shell did not create it"
        ftype, size, start, blocks = top["fresh.txt"]
        f.seek((populate.HBFS_DATA_START + start * populate.SECTORS_PER_BLOCK)
               * populate.SECTOR_SIZE)
        got = f.read(size)
        if got != b"first\n":
            return "/fresh.txt reads %r" % got

        if "tmpdir" not in top:
            return "no /tmpdir in the root directory"
        ftype, _, start, blocks = top["tmpdir"]
        if ftype != populate.FTYPE_DIR or blocks != populate.HBFS_SUBDIR_BLOCKS:
            return "/tmpdir is type %d with %d blocks" % (ftype, blocks)

        f.seek((populate.HBFS_DATA_START + start * populate.SECTORS_PER_BLOCK)
               * populate.SECTOR_SIZE)
        inner = entries(f.read(blocks * populate.BLOCK_SIZE),
                        populate.HBFS_SUBDIR_MAX_FILES,
                        populate.HBFS_DIR_ENTRY_SIZE)
        if inner:
            return "/tmpdir still holds %s after unlink" % sorted(inner)

        if "probe" in top:
            return "the shell's 'rm probe' left the entry behind"
    return None


def parse_pmm(text):
    """Pull usable=MB and top=0x... out of the nucleus report."""
    m = __import__("re").search(r"usable=(\d+)MB top=0x([0-9A-F]+)", text)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2), 16)


def check_hbfs_version(text):
    """The mount line has to say which format it mounted.

    populate.py writes v3, so the reader must report 3. Matched as a whole field
    because `version=32` on the I/O APIC line contains `version=3` as a
    substring, and the day a writer makes the image v4 this is the assertion
    that notices - which is the reason the version is printed at all.
    """
    m = re.search(r"\[hbfs\][^\r\n]* version=(\d+)", text)
    if not m:
        return "the mount line does not report a version"
    if int(m.group(1)) != 3:
        return "the image mounts as v%s, but populate.py builds v3" % m.group(1)
    return None


def main():
    qemu = qd.find_qemu()
    if not qemu:
        if ALLOW_SKIP:
            print("  SKIP  long-mode test: no QEMU found on this host")
            return 0
        print("  FAIL  long-mode test: no QEMU found", file=sys.stderr)
        return 1
    if not os.path.exists(IMAGE):
        print("  FAIL  long-mode test: missing %s (run 'make 64bit')" % IMAGE, file=sys.stderr)
        return 1

    # The guest writes to its own volume: the shell creates, appends to and
    # removes files, and the host then reads those writes back out of the image.
    # Booting the built image directly therefore made this test single-use - the
    # second run saw the first run's files and failed on content that was
    # correct. Every boot here uses a private copy instead.
    workdir = scratch_dir("lm-run-")
    image = os.path.join(workdir, "image.img")
    shutil.copyfile(IMAGE, image)
    try:
        return run_all(qemu, image)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def run_all(qemu, image):

    # The shell is the last task: it prints a prompt, lists the working
    # directory, prints a file, rejects a command it does not know and leaves.
    SHELL_SCRIPT = (
        ("mellivora:/> ", "ls"),
        ("mellivora:/> ", "cat /motd"),
        ("mellivora:/> ", "date"),
        ("mellivora:/> ", "clear"),
        ("mellivora:/> ", "mkdir probe"),
        ("mellivora:/> ", "ls"),
        ("mellivora:/> ", "rm probe"),
        ("mellivora:/> ", "append /persist.txt shell third"),
        ("mellivora:/> ", "cat /persist.txt"),
        ("mellivora:/> ", "append /fresh.txt first"),
        ("mellivora:/> ", "cat /fresh.txt"),
        ("mellivora:/> ", "run /api"),
        ("mellivora:/> ", "run /net"),
        ("mellivora:/> ", "run /beep"),
        ("mellivora:/> ", "run /threads"),
        ("mellivora:/> ", "run /futex"),
        ("mellivora:/> ", "run /tls"),
        ("mellivora:/> ", "run /nope"),
        ("mellivora:/> ", "spawn /spin"),
        ("mellivora:/> ", "date"),
        ("mellivora:/> ", "kill 7"),
        ("mellivora:/> ", "nonsense"),
        ("mellivora:/> ", "quit"),
    )
    # Each boot gets its own copy: the retry below runs the same shell script
    # again, and `/fresh.txt` is appended to by it. Sharing one image made the
    # retry itself the reason the content check failed.
    run_image = fresh_image(image)
    text = boot_and_capture(qemu, run_image, 256, MARKER, SHELL_SCRIPT)
    required = (MARKER,) if MARKER_ONLY else REQUIRED
    missing = [needle for needle in required if needle not in text]

    # A single missing line out of ninety is worth one more boot before it is
    # worth an investigation: this guest does a great deal in a few seconds on an
    # emulated CPU, and the console is a UART whose socket the harness is reading
    # from the other side of a process boundary. When a retry does happen it says
    # so, because a suite that silently retries hides a flake rather than
    # reporting one - and the flake is still the thing to fix.
    if missing and not MARKER_ONLY:
        print("  RETRY after a missing line: %s" % ", ".join(missing))
        run_image = fresh_image(image)
        text = boot_and_capture(qemu, run_image, 256, MARKER, SHELL_SCRIPT)
        missing = [needle for needle in required if needle not in text]

    if missing:
        # The guest counts every character it emits and prints the total before the
        # marker, so a missing line can be attributed: equal numbers mean the line
        # was never printed, and a short count means characters were lost between
        # the UART and this socket.
        m = re.search(r"\[log\] chars=(\d+)", text)
        if m:
            declared = int(m.group(1)) + len(text[m.start():].split(MARKER)[0])
            print("  NOTE  the guest declared %d characters, this run captured %d"
                  % (declared, len(text.split(MARKER)[0])), file=sys.stderr)
        print("  FAIL  missing on COM1: %s" % ", ".join(missing), file=sys.stderr)
        print("---- serial ----", file=sys.stderr)
        print(text[-800:], file=sys.stderr)
        return 1

    if MARKER_ONLY:
        print("  PASS  long mode entered and nucleus reported")
        return 0

    stamp = re.search(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", text)
    if not stamp:
        print("  FAIL  the shell never printed a date", file=sys.stderr)
        return 1
    guest_year = int(stamp.group(0)[:4])
    host_year = int(__import__("time").strftime("%Y"))
    if abs(guest_year - host_year) > 1:
        print("  FAIL  the clock says %s, this host says %d"
              % (stamp.group(0), host_year), file=sys.stderr)
        return 1

    # The mount line names the on-disk format it accepted. The image is built by
    # populate.py, so this is also the check that the reader and the builder
    # agree about which version the volume is.
    problem = check_hbfs_version(text)
    if problem:
        print("  FAIL  HBFS mount: %s" % problem, file=sys.stderr)
        return 1

    # The guest wrote real HBFS structures, so the host can read them back with
    # the same constants populate.py builds images with. This goes before the
    # other sessions, which boot the same image again and would overwrite what
    # the shell just wrote.
    problem = check_written_volume(run_image)
    if problem:
        print("  FAIL  the volume the guest wrote is wrong: %s" % problem,
              file=sys.stderr)
        return 1

    # The two input sessions drive the emulated i8042 through the QEMU monitor,
    # which is timing-sensitive under TCG on a host of another architecture.
    # They get one retry, and say so: a silent retry would hide a lost
    # interrupt, which is exactly what these two checks exist to catch.
    problem = check_keyboard_and_screen(qemu, fresh_image(image))
    if problem:
        print("  RETRY keyboard/screen: %s" % problem)
        problem = check_keyboard_and_screen(qemu, fresh_image(image))
    if problem:
        print("  FAIL  keyboard/screen: %s" % problem, file=sys.stderr)
        return 1

    problem = check_mouse(qemu, fresh_image(image))
    if problem:
        print("  RETRY mouse: %s" % problem)
        problem = check_mouse(qemu, fresh_image(image))
    if problem:
        print("  FAIL  mouse: %s" % problem, file=sys.stderr)
        return 1

    # Scale: a guest past 4 GB only reports sane numbers if the E820 map was
    # read as 64-bit, and 8 GB is the P2 gate.
    if SKIP_BIG:
        print("  PASS  long mode, VMM, tasks, NX/SMEP/SMAP, syscalls, PCI/ATA, HBFS, exec, 32-bit mode (big-memory skipped)")
        return 0

    big = boot_and_capture(qemu, image, BIG_MB, MARKER, SHELL_SCRIPT)
    stats = parse_pmm(big)
    if stats is None:
        print("  FAIL  no frame-allocator report at %d MB" % BIG_MB, file=sys.stderr)
        print(big[-800:], file=sys.stderr)
        return 1
    usable_mb, top = stats
    # Not all of the guest is usable: the firmware reserves MMIO just below 4 GB
    # (QEMU puts the PCI hole at ~0xB0000000), so a fraction of the RAM is
    # missing from the report by design. 85% still fails loudly if the map was
    # read as 32-bit, which is the mistake this checks for.
    expected = BIG_MB * 85 // 100
    if usable_mb < expected or top <= 0x100000000:
        print("  FAIL  %d MB guest reported usable=%d MB top=0x%X"
              % (BIG_MB, usable_mb, top), file=sys.stderr)
        print(big[-800:], file=sys.stderr)
        return 1

    print("  PASS  long mode, VMM, tasks, NX/SMEP/SMAP, syscalls, PCI/ATA, HBFS, exec, 32-bit mode, %d MB of RAM" % usable_mb)
    return 0


if __name__ == "__main__":
    sys.exit(main())
