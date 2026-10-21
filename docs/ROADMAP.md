# Mellivora OS — Roadmap to a general-purpose OS

This is the working plan for turning Mellivora from a strong hobby OS into a system
whose *core* is comparable to a 2000-era commercial OS (Windows 2000 / NT 5), and in
several respects better: 64-bit, capability-secured, crash-containing, instant to boot
and tiny in memory.

The plan is deliberately measured. Every phase closes against numbers produced by
`make check` and the benchmark suite, not against opinion.

## Architectural decisions

| # | Decision | Rationale |
|---|---|---|
| D1 | 64-bit long mode is the primary target | 32-bit already limits address space, RAM and ABI; the existing `make 64bit` target is a placeholder, not a port |
| D2 | New kernel code in freestanding C11, NASM for boot/entry/context-switch/fast paths | A 64-bit SMP kernel entirely in hand-written asm does not scale; keep asm where it matters |
| D3 | Hard user/kernel isolation: no kernel RAM reachable from ring 3 | Today the identity map marks 0–128 MB `PG_USER`, so any app can write the kernel |
| D4 | POSIX-shaped ABI plus native extensions | Ports, libc and muscle memory transfer; GUI/package/manifest extensions stay Mellivora-specific |
| D5 | Monolithic kernel with a real driver interface | Best performance per effort; a clean driver boundary keeps user-mode drivers possible later |
| D6 | Object manager + capability security | Devices, config, files and services become named objects with ACLs; manifest permissions become enforced |
| D7 | Compositor stays in-kernel for v1, gains damage tracking and a font stack | A user-space display server is a later, separately gated decision |

## Gates

A phase does not close until these are measured:

| Gate | Target |
|---|---|
| Isolation | A hostile app cannot read or write a byte outside its own mappings |
| Scale | 8 vCPUs, 8 GB RAM, 1 TB disk, 4 096 concurrent tasks |
| Latency | syscall < 200 ns · context switch < 1 µs · fork+exec < 2 ms · 1080p frame < 8 ms |
| Storage | > 100 MB/s sequential; crash-consistent across 10 000 random power-cuts |
| Network | 1 000 concurrent sockets; survives 5% loss and reordering |
| Reliability | 24 h soak with zero panics; every user fault is contained |
| Footprint | < 8 MB RAM idle; < 2 s boot to desktop under QEMU |
| Security | Unprivileged code cannot open a device, peer memory or a forbidden file; fuzz-clean |

## Progress

| Phase | State | Landed so far |
|---|---|---|
| P1 | in progress | `vm_area` table, demand-zero `mmap`, file-backed `mmap_file`, partial `munmap`/`mprotect`, fork COW of mmap areas, exit/exec reclaim, randomised placement, W^X, ring-3/kernel isolation, fail-closed demand faults, supervisor demand faults on the task's own areas, console-mode fault containment, guarded demand-grown user stacks, kernel identity demand above 128 MB, unified block cache, user heap on the VMM (`malloc`/`free`/`realloc`), coherent shared file mappings through a page cache, a kernel slab allocator for per-task tables, `vmtest`/`vmtest2`/`vmtest3`/`vmtest4`/`vmtest5`/`heaptest`/`memprot_test`/`sbrk_test`/`stack_test` conformance fixtures |
| P2 | in progress | long-mode boot path end to end: stage2 builds the identity map, enters long mode and starts a 64-bit nucleus that reports over COM1 (`make check-longmode`, CI job `longmode`); freestanding C11 nucleus built by a probed cross toolchain, linked into the higher half at `0xFFFFFFFF80100000` with a second mapping in stage2, SYSCALL/SYSRET MSRs programmed and a `syscall` round trip proven; per-CPU blocks behind the GS base and a SYSCALL entry stub that switches to the CPU's kernel stack; ring 3 reached and returned to (user GDT/TSS, `U/S` user pages, `SWAPGS` at entry, three syscalls from the user program with the caller's stack and registers intact); an IDT that catches a ring-3 fault on the TSS `RSP0` stack, resumes it and reports any other fault instead of triple-faulting; a VMM over the frame allocator with 4 KB map/unmap, translation and a per-level U/S check, exercised by a self-test that faults on an unmapped page on purpose; a slab allocator over a dedicated heap window mapped by that VMM; a 100 Hz PIT whose ticks reach ring 3 through the TSS stack; two ring-3 tasks round-robined by that tick with per-task kernel stacks and per-task address spaces; NX on heap and stack pages with SMEP/SMAP enabled where CPUID offers them; a table-driven LP64 syscall ABI with write/yield/brk behind it; PCI enumeration and an ATA PIO block driver whose read/write paths are self-tested; an HBFS reader and VFS front-end, with `/init` loaded from that disk and `exec`/`open`/`read`/`close` behind it; a 32-bit program running in compatibility mode beside the 64-bit tasks through an `INT 0x80` bridge, with one of the tree's own flat binaries loaded unchanged at its `0x00300000` program base; directories and file metadata (`seek`/`stat`/`readdir`/`chdir`/`getcwd`) over a per-task working directory; a writable HBFS (`create`/`mkdir`/`unlink` over the allocation bitmap) whose output the host test reads back out of the image; console input over COM1 with a line-based shell (`ls`/`cat`/`date`/`mkdir`/`rm`/`quit`) that the host test types at; a CMOS clock; a user-space heap, whole-file load and pipes, with `exec` passing a command line on to `getargs`; a VGA text console with real colour, cursor and clear calls; a PS/2 keyboard whose input the test types with real scancodes; first ported subsystem is a 64-bit frame allocator over the E820 map, self-tested at 256 MB and at 8 GB |

## Phases

### P0 · Guardrails and measurement (2 sprints)

- CI on every push: lint, unit tests, boot, runtime, desktop, fsck, benchmarks (see
  `.github/workflows/ci.yml`). Add: spec presence check, CI time budget, 64-bit target
  as a non-blocking job until P2.
- Benchmarks: boot time, syscall round-trip, context switch, fork+exec, file create/read
  /write, TCP throughput, compositor frame time. Trend-tracked (`tools/bench_trend.py`).
- Fuzzers: syscall argument fuzzer, HBFS image fuzzer, GUI event fuzzer.
- Fault injection: random task kills, OOM, disk full, network drop.
- Normative specs under `docs/spec/` (see below).

**Exit:** every later claim is reproducible in CI; specs reviewed.

### P1 · Memory and isolation (4 sprints)

- [x] `vm_area` model; `mmap/munmap/mprotect` with randomised placement and W^X.
- [x] Guard pages and on-demand stack growth for the user stack.
- [x] Demand-zero anonymous mappings; partial `munmap`/`mprotect`; fork COW and
  exit/exec reclaim of `mmap` areas.
- [x] File-backed (private, demand-paged) mappings via `SYS_MMAP_FILE`.
- [x] Shared write-back file mappings (`MF_SHARED`): dirty pages go back to the file
  when the mapping is released.
- [x] **Remove user access to kernel RAM**; map only what a process needs (program
  window, stack, framebuffer, IPC surfaces). See `docs/spec/VMM.md`.
- [x] Unified block cache in front of the disk data path (read-through, flushed on
  every write).
- [x] User heap on top of the VMM: `SYS_MALLOC`/`SYS_FREE`/`SYS_REALLOC` are
  anonymous demand-zero areas, and the SB16 path stages demand-zone buffers because
  ISA DMA needs a physical address.
- [x] Page cache in front of shared file mappings (`kernel/pagecache.inc`): one frame
  per HBFS block, shared by every `MF_SHARED` mapping of it, so two mappings of one
  file - in the same task or in different tasks - are coherent with each other, and a
  fork keeps sharing them instead of copying on write. `vmtest5` covers it.
- [x] Kernel slab allocator (`kernel/kmem.inc`): fixed-size objects carved out of
  4 KB pages taken on demand, with intrusive free lists and zeroed allocations. Its
  first two users are every task's mmap area table and argv snapshot, which were static
  arrays sized for `MAX_TASKS`: the kernel image lost 175 KB (2.02 MB → 1.84 MB of the
  2 MB limit) and now pays only for the tasks that exist.
- [x] ASLR for stack/heap. The `mmap` base was already randomised; the user stack now
  starts at a page-granular offset below the top of its region on every launch (4 bits
  of entropy - the 1 MB window leaves only 16 pages of slack before the stack could no
  longer grow - so it defeats a fixed-address assumption, not a patient attacker), the
  trampoline and the mapped window move with it, exec in place keeps its top (it has no
  mapping left outside its existing window), and the randomiser is seeded from the RTC
  and the PIT instead of `tick_count`, which was 0 at boot. NX and a wider stack region
  belong with P2.
- [x] Allocations above 128 MB (pre-existing). Three faults, all only reachable on a
  guest with more RAM than the boot identity map covers: the page-fault handler saved
  three registers while its helpers clobbered EBX/ESI/EDI/EBP, so the buddy allocator's
  free-list cursor was silently replaced by the faulting page number (the "next of 0"
  that walked to page 0); the handler's `pf_*` scratch was not re-entrant, and it
  re-enters itself whenever it zeroes a frame above the identity map, which mapped the
  page at the frame's address instead of the program's; and `cow_ref` stopped at 128 MB,
  so a forked share above it was freed twice. `make check-runtime` now runs `vmtest`,
  `vmtest2`, `cow_test` and `heaptest` on a 192 MB and a 384 MB guest.
- [x] Console-mode fault containment: a crashing task dies, the shell survives.
- [x] No stale bytes between processes. A task's window is a copy of the shell's, and
  nothing cleared it between launches: the next program could read the previous one's
  heap, `mmap` scratch and whole user stack. The launcher now clears
  `[image end, PROGRAM_END_ADDR)` before it writes the trampoline and creates the task,
  and `stale_write`/`stale_probe` prove it (`STALE_LEAK 00340000` on the old kernel).
- [x] `waitpid` after a signal-killed task. The kill path freed the target's slot
  instead of leaving a `TASK_ZOMBIE`, so `waitpid` never found it and blocked the parent
  forever. Both death paths now end in `sched_publish_death`: status stored, zombie kept,
  waiters woken, `SIGCHLD` queued, and a shell-launched task reaped at once. A killed
  task reports `0x80000000 | signal`; `kill_leak_test` waits for a child it killed and
  checks both the status and that nothing leaked.

**Exit:** the isolation conformance test passes, 100 MB heap workload passes
(`make check-heap`), no stale-data leaks between processes.

### P2 · 64-bit foundation (6 sprints)

- [x] Long-mode boot: CPUID gate, identity map, far jump, 64-bit nucleus on COM1.
- [x] Cross toolchain, dual-target build. `tools/find_lm_toolchain.sh` probes for
  `$LM_CC`, `x86_64-elf-gcc`, `clang --target=x86_64-elf` or the host gcc and checks the
  object it produces, so the build works on a non-x86 host (clang+lld here) and on x86
  CI (host gcc). `make` still builds the 32-bit tree; `make 64bit` builds the C nucleus.
- [x] Higher-half kernel. The nucleus is linked at `0xFFFFFFFF80100000` and stage2 maps
  that address range over the same physical pages as the identity map, then jumps to the
  link address; the nucleus reports its own address to prove where it runs.
- [x] `SYSCALL` entry (MSRs programmed, 64-bit `syscall` reaches a handler and returns
  with `iretq`). The per-CPU kernel stack, `swapgs` and per-CPU blocks are what remains.
- [x] First ported subsystem: a 64-bit physical frame allocator (`boot/lm_pmm.c`) with
  a bitmap over the E820 map, the low megabyte and the nucleus image reserved, and a
  self-test in the nucleus. Verified at 256 MB and at 8 GB - past 4 GB the map only
  reads correctly with 64-bit arithmetic, and the allocator covers ranges above
  `0x100000000` across the PCI hole.
- [x] Per-CPU blocks: `boot/lm_cpu.c` keeps one block per processor behind the GS base
  MSR, and the SYSCALL entry stub reads it (`%gs:0x10`/`%gs:0x18`) to save the caller's
  stack pointer and switch to that CPU's kernel stack before it calls any C. The nucleus
  checks that the handler ran on the kernel stack and that the caller's RSP came back
  unchanged. Ring 0 only at that point; the next item crosses into ring 3.
- [x] Ring-3 path: `SWAPGS`, a user GDT/TSS, and the return to a user stack.
  `boot/lm_user.c` builds a GDT with user data/code descriptors and a 64-bit TSS
  (`RSP0` = the per-CPU kernel stack), maps two frames at `0x40000000` with `U/S`
  at every level, and `IRETQ`s to a user program. A `SYSCALL` from ring 3 arrives
  with kernel selectors but the user's RSP and GS base, so the entry stub starts
  with `SWAPGS` (the per-CPU block lives in `KERNEL_GS_BASE`), saves all general
  registers in the frame and returns with the user's CS/SS. The program calls
  `PING`, `WHOAMI` and `EXIT`; the exit code is the value `PING` returned,
  carried through the user's own stack, and the nucleus prints `ring3=ok`.
- [x] IDT and exception entry: `boot/lm_idt.c` installs a long-mode IDT for vectors
  0..21 (the rest are absent, so the CPU raises `#GP` into the same handler), one stub
  shape with a dummy error code where the CPU pushes none, `SWAPGS` plus fifteen saved
  registers on the common path, and a C dispatcher that reports vector, error code,
  RIP, CS, CR2 and whether the TSS supplied the stack. A ring-3 `INT3` proves the TSS
  `RSP0` transition and is resumed; an unexpected fault reports and halts instead of
  triple-faulting.
- [x] VMM: `boot/lm_vmm.c` walks the four-level tables from CR3, allocates and zeroes
  the intermediate tables a mapping needs, maps/unmaps 4 KB leaves with W/U flags,
  translates (including stage2's 2 MB and 1 GB pages) and answers "may ring 3 touch
  this" at every level. stage2's direct map grew to 1 GB so any frame the allocator
  hands out can be written; the ring-3 pages are mapped through the VMM now, and the
  self-test writes through a mapping, reads it back through the direct map, refuses a
  double map, and proves an unmapped address faults (via a fix-up path) before being
  mapped and read again.
- [x] Slab allocator: `boot/lm_kmem.c` ports the 32-bit cache design - fixed-size
  objects threaded through their own storage, a page taken only when a cache runs dry,
  objects zeroed on allocation - over a 16 MB kernel-heap window at
  `0xFFFFC00000000000`, where every slab page is a PMM frame mapped by the VMM. The
  self-test forces two slabs, writes to every object, checks the slots are distinct and
  not user-mapped, frees them and checks a recycled object comes back zeroed.
- [x] Timers and interrupt delivery: `boot/lm_irq.c` remaps the 8259 to vectors
  32..47, masks everything but IRQ0 and runs the PIT at 100 Hz. The IDT gained the
  sixteen hardware gates (the same stub shape, no error code), interrupts are enabled
  only in ring 3, and `SYS_TICKS` lets the program wait for three ticks. The nucleus
  reports `ticks=N user=N stack=ok`, which is true only when every tick came from ring
  3 and crossed the TSS stack - the delivery a scheduler will preempt on.
- [x] Scheduler: `boot/lm_sched.c` creates tasks with a kernel stack from the slab (a
  page in the heap window) and a user stack, gives each a saved frame in the same
  twenty-two-value layout both entry stubs build, and round-robins them at the timer
  tick and at `SYS_EXIT`. `TSS.RSP0` follows the task on every switch, so the next
  fault or tick lands on that task's stack. Two tasks run the same program and each
  exits with its own id after two shared ticks; the nucleus checks every task's id,
  `PING` value, `WHOAMI` answer and call count, and requires at least one preemption.
  Tasks share one address space; per-task CR3s come with the address-space work.
- [x] Syscall table and the LP64 ABI: `boot/lm_syscall.c` dispatches from a table
  (number in RAX, arguments in the LP64 registers, result in RAX) with holes for the
  calls still to come, and ring-3-only entries for the ones that end or yield the
  caller. `write` reads a user buffer under the STAC/CLAC bracket SMAP requires and
  validates pages through the VMM, `yield` shares the tick's switch, and `brk` grows
  and shrinks a per-task mapping that the scheduler checks is gone at exit.
- [x] First drivers: `boot/lm_pci.c` enumerates configuration space over
  0xCF8/0xCFC (vendor/device/class, multi-function aware) and reports the IDE
  controller; `boot/lm_ata.c` is a polling ATA PIO driver - IDENTIFY for the model and
  LBA28 sector count, 512-byte read and write with the 400 ns select delay, bounded
  status polling and a FLUSH after writes. The self-test reads LBA 0's boot signature
  and round-trips a pattern through the last sector.
- [x] HBFS and a VFS front-end: `boot/lm_hbfs.c` reads the same on-disk format
  populate.py writes (superblock at LBA 4096, 288-byte entries, data blocks relative to
  the data area) and `boot/lm_vfs.c` is the open/read front-end over it. The demo
  program moved out of the nucleus into `boot/lm_userprog.asm`, which
  `tools/mk_hbfs64.py` writes into the image as `/init` - so `make 64bit` boots a
  program loaded from a filesystem rather than bytes in `.rodata`.
- [x] `exec` and the file syscalls: `SYS_EXEC` loads a file into a private frame,
  replaces the task's code mapping, zeroes the stack and rewrites the saved frame so the
  syscall's `IRETQ` enters the new program; `open`/`read`/`close` copy user paths and
  buffers under the STAC/CLAC bracket with per-page validation. The demo is now `/init`
  exec'ing `/hello`, which reads `/motd` off the disk and prints it.
- [ ] Port the rest of the 187 syscalls and the driver set (directories and the other
  VFS back-ends, network, display, input, audio).
- [x] LP64 ABI: the syscall table's convention (number in RAX, arguments in
  RDI/RSI/RDX/R10/R8/R9, result in RAX) is the one the 32-bit calls are bridged to.
- [x] 32-bit compatibility mode: a DPL-3 `L=0` code segment, a DPL-3 `INT 0x80` gate, and
  a bridge that translates the 32-bit tree's numbers and EBX/ECX/EDX convention onto the
  LP64 handlers. A 32-bit program runs as a task beside the 64-bit ones, preempted by
  the same timer.
- [x] Load and run the existing 32-bit binaries unchanged. They are not ELF32: they are
  flat `[BITS 32]` images with `[ORG 0x00300000]` from `programs/syscalls.inc`, so the
  loader maps one frame at that program base, points the stack at `0x00400000` and
  enters it through the compatibility segment. `programs/hello.asm` is built by the
  same NASM line `make programs` uses, packed into HBFS as `/hello_tree` and started as
  a third task beside the other two: it calls `SYS_SETCOLOR`/`SYS_PRINT`/`SYS_EXIT`
  through `INT 0x80` and prints both of its lines over COM1. A compatibility task whose
  program leaves no meaningful exit code passes `TASK_ANY_EXIT`.
- [x] Directories and file metadata through both ABIs: `SEEK` (with whence),
  `STAT`, `READDIR`, `CHDIR` and `GETCWD`. HBFS grows a directory layer - a
  subdirectory is an entry with `FTYPE_DIR` whose blocks hold 288-byte entries of
  their own, exactly as the root's do - and each task keeps its own working
  directory, so two tasks listing different directories do not tread on each
  other. `STAT` answers in both of the tree's shapes: the `struct vfs_stat` the
  64-bit table passes, and the 12-byte `size/type/mtime` buffer (or the legacy
  no-buffer form returning size in EAX and blocks in ECX) the 32-bit bridge
  translates. `/ls` lists the root, stats `/motd`, seeks into it, chdirs into
  `/docs` and lists that; `/hello32` makes the same calls from compatibility mode
  and prints `32-bit file calls ok`.
- [x] Write to the volume. HBFS's allocation bitmap is read at mount and kept in
  memory; `create`/`mkdir`/`unlink` allocate contiguous blocks, write the data
  (zero-filling the tail of the last block), add, rewrite or clear a 288-byte
  directory entry, and write the bitmap and the superblock's free count back.
  Creating a name that already exists frees its old blocks first, so a program can
  be re-run without leaking the volume one copy at a time. `/wr` creates
  `/persist.txt`, reads it back through the same `open`/`read` path, makes
  `/tmpdir`, puts a file in it, unlinks that file and watches the directory report
  it gone; the host test then reads `/persist.txt` straight out of the image with
  the same constants `populate.py` writes it with, which is the proof that what
  the guest wrote is a real on-disk structure and not a cached illusion.
- [x] Console input, and a shell to use it. COM1's line status register gives the
  kernel one character at a time (`kgetc`), and the console calls ride on it:
  `PUTCHAR`, `GETCHAR`/`READ_KEY`/`SERIAL_IN`, `CLEAR` (an ANSI sequence, since the
  console is the UART) and `SLEEP`, which is a real `hlt` until the PIT interrupts
  rather than a spin. `/sh` is the first program that waits for input: it echoes a
  line, lists the working directory with `ls`, prints a file with `cat`, rejects
  what it does not know and leaves on `quit`. It sleeps a tick between polls, so a
  waiting shell costs 334 syscalls instead of 590,000. The host test drives it down
  the serial line - prompt, `ls`, `cat /motd`, a bogus command, `quit` - and the
  shell's session interleaves with the other five tasks' output, which is the
  clearest evidence yet that the scheduler is really switching.
- [x] A clock, and directories that can be removed. `boot/lm_rtc.c` reads the CMOS
  chip through ports 0x70/0x71, waits out an update-in-progress and re-reads if the
  seconds moved underneath, undoes BCD and 12-hour time, and reports `[rtc]
  2026-10-21T21:46:6 rtc=ok` at boot. `SYS_DATE` fills the tree's six-byte buffer and
  returns the four-digit year; `SYS_GETTIME` is the tick count, as in the tree.
  `unlink` now removes an empty directory as well as a file - it refuses a
  non-empty one rather than walking it, since a caller that wants a recursive
  delete can drive `readdir` itself. The shell gained `date`, `mkdir` and `rm`.
- [x] The library-shaped calls: `MALLOC`/`FREE` over the break (headers in front of
  each block, the free list threaded through the blocks themselves, first-fit
  reuse), `SBRK`, `FREAD` as a whole-file load, `PIPEn` create/write/read/close
  over four 4 KB rings with a kernel self-test, `GETARGS` (the kernel keeps the
  command line `EXEC` was given, so it survives the address space being rebuilt)
  and `GETENV` (a five-entry table). `/api` exercises all of them and `/init` now
  execs `/hello` with a command line that `/hello` asks back for.
- [x] A display. `boot/lm_vga.c` drives the 80x25 text buffer at 0xB8000 through the
  kernel's *direct map* - a task's address space leaves the identity map out, so a
  console write from inside a syscall would fault on the physical address, and a
  fault while printing a fault is a short way to lose the machine. `kputc` now fans
  out to COM1 and to the screen, and `SETCOLOR` (attribute byte), `SETCURSOR` and
  `CLEAR` are real calls instead of accepted no-ops. The driver proves itself at
  boot by writing through its own path and reading the text memory and the CRTC
  cursor register back: `[vga] 80x25 at 0xB8000 vga=ok`. The shell gained `clear`.
- [x] A keyboard. `boot/lm_kbd.c` polls the i8042 (status 0x64, data 0x60), translates
  scancode set 1 with shift, control and caps state tracked from the release codes,
  and skips the bytes the controller reports as coming from the mouse. `kgetc` asks
  the keyboard first and the UART second, so the console is now the machine's own
  keyboard *and* a serial terminal. The test types `date` and `quit` through the
  monitor's `sendkey` - real scancodes, not the UART - and then reads the VGA text
  buffer straight out of 0xB8000 to confirm the display shows the same thing.
- [ ] What is left of P2's breadth: `FWRITE`'s append form, the mouse, e1000
  networking, audio, and `fork`/signals for the process calls.
- [x] NX, SMEP/SMAP where the CPU provides them: `cpu_harden()` gates each on CPUID,
  sets `EFER.NXE`/`CR4.SMEP`/`CR4.SMAP` and reports what the machine got. Heap pages
  and user stacks carry `NX`; the user code page is the one leaf without it, checked
  per task space with `vmm_flags`. The identity and direct maps are still executable
  2 MB pages because they cover the image; splitting them is the next hardening step.
- [x] Per-task address spaces: `vmm_space_create` copies the kernel's higher-half and
  heap entries into a fresh PML4 and leaves the identity map out, `become()` loads CR3
  on every switch, and both tasks run at the same user addresses with separate stacks -
  each stores its id at the same virtual address, is preempted, reads it back and exits
  with it, so a missing CR3 switch fails a task.

**Exit:** the whole existing suite green on 64-bit; 8 GB RAM usable; 32-bit apps run
unchanged; kernel image < 1 MB.

### P3 · Concurrency and scale (4 sprints)

ACPI/MADT, local APIC + IOAPIC, SMP to 8 vCPUs, per-CPU run queues, spinlocks, atomics,
IPIs, TLB shootdown, preemptible kernel, lockdep-style lock validation, threads
(`clone` with shared VM), futex, TLS, HPET/TSC clocksource, high-resolution timers.

**Exit:** near-linear scaling on a parallel workload; 24 h SMP stress with no lost
wakeups.

### P4 · Storage (3 sprints)

HBFS v4: 64-bit block numbers, extents, checksummed journal, online fsck, COW snapshots,
quotas, ACLs. VFS page cache with writeback and readahead. NVMe, AHCI NCQ, GPT.

**Exit:** 1 TB image, > 100 MB/s, 10 000 crash-injection iterations clean.

### P5 · Networking (3 sprints)

Socket layer with non-blocking I/O, `select/poll/epoll`, `SO_*` options and
`getsockopt`; TCP window scaling, SACK, RTT/RTO estimation, TIME_WAIT; UNIX sockets;
netdevice abstraction; IPv6 basics; stateful packet filter; TLS 1.3 port; service
supervisor (DHCP renew, DNS cache, NTP).

**Exit:** line-rate virtio throughput, 1 000 sockets, chaos-tested under loss.

### P6 · Security and multi-user (4 sprints)

Accounts with shadow password hashes, login and sessions, `su`/`sudo` equivalent, groups;
**enforced capabilities** from app manifests; object manager for hierarchical config with
ACLs; audit log; kernel KASLR; hardened allocator; stack canaries; secure RNG.

**Exit:** unprivileged code cannot reach a device, peer memory or a forbidden file;
permission suite and fuzzers clean.

### P7 · Devices, PnP and power (3 sprints)

PCI BAR assignment, MSI/MSI-X, driver registry with probe/attach and refcounts; ACPI
shutdown/reboot/sleep/battery/thermal; USB (UHCI/EHCI) + HID; virtio-gpu; HDA audio.

**Exit:** clean shutdown, USB input, audio, no hard-coded IRQs or addresses.

### P8 · UI and UX (4 sprints)

Damage-tracked compositing, vsync-aware flip, hardware cursor, shadows and rounded
corners, window animations, 1080p/4K, multi-monitor; TrueType rasterizer and a system
font; widget toolkit and design system; taskbar grouping with live thumbnails,
start-menu search and favourites, notification centre with actions, drag and drop,
multi-format clipboard, shortcut editor, settings with live preview, onboarding,
accessibility (keyboard navigation, focus rings, high contrast, scalable UI).

**Exit:** 60 FPS at 1080p with 30 windows, UI latency < 50 ms, drag and drop between
editor and file manager, one visual language.

### P9 · Applications and utilities (continuous from P1)

Productivity (BEdit+ with multi-document and undo tree, BSheet, BWriter, BSlides,
BDraw, BMail, BCal, BContacts, BTasks), system tools (BTerm+ tabs/splits/256 colours,
BForager+ tabs/previews/archives, task manager, package manager GUI, backup, disk
tools), internet (BBrowser with an HTML 4 / CSS1 subset, feed reader), development
(BDev IDE, debugger front-end, disassembler, `bscript`), media (image viewer, player,
screen capture), and a shell grown to ~200 commands with consistent flags and man pages.

### P10 · Reliability and performance (continuous)

Panic screen with symbolised stacks and recovery, crash dumps over serial, 24 h soak
and reboot loops, OOM/disk-full/network-chaos injection, performance gates that fail a
pull request on regression.

- [x] OOM / disk-full / dropped-frame injection (`kernel/faultinject.inc`,
  `SYS_FAULTINJECT`): arm a counter and the next page allocations, disk writes or
  outgoing frames fail. `fault_inject_test` walks every failure depth of the fork path
  and requires the free-page count to come back each time, a file create whose writes
  all fail to report an error and still work afterwards, and a dropped frame to be
  retried past. `make check-faults` repeats it; `check-runtime` runs it once.
- [ ] Crash dumps over serial, 24 h soak, performance gates.

### P11 · Ecosystem and release engineering (continuous)

SDK container with cross toolchain, libc, headers and samples; `hbpkg` v2 with signatures
and dependencies; reproducible builds, SBOM, signed ISOs; generated API reference;
porting guide.

## Out of scope

Printing, domains/Active Directory, COM/DCOM, Terminal Services, and binary compatibility
with Win32 applications. These would consume the effort the kernel needs and cannot be
reached credibly.

## Specs

Normative documents live in `docs/spec/`:

| Document | Covers |
|---|---|
| `VMM.md` | Address spaces, isolation rules, `mmap` semantics, fault handling |
| `ABI.md` | Syscall conventions, 32/64-bit compatibility, structure layouts |
| `SCHED.md` | Task model, priorities, preemption, SMP affinity, signals |
| `FS_V4.md` | On-disk format, journal, snapshots, quotas, ACLs |
| `DRIVER_MODEL.md` | Driver registry, probe/attach, IRQ/MSI, DMA rules |
| `SECURITY.md` | Identities, capabilities, ACLs, audit, sandboxing |
| `COMPOSITOR.md` | Surface model, damage, present path, input routing |

## Risks

| Risk | Mitigation |
|---|---|
| 64-bit port stalls | 32-bit tree stays green; port behind a HAL, subsystem by subsystem; go/no-go at P2 exit |
| C in the kernel erodes style | Freestanding subset only, MISRA-lite rules in `STYLE_GUIDE.md`, enforced by `make lint` |
| Scope explosion | Fixed sprint exit criteria and the explicit out-of-scope list above |
| Emulator-only confidence | QEMU first, then real hardware; KVM in CI |
| CI time balloons | Snapshot-based QEMU tests, parallel jobs, hard 10-minute budget |
