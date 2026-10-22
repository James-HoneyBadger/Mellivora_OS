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
| P3 | in progress | threads (`clone` with a shared address space), a blocking futex, a shared heap and thread-local storage; ACPI/RSDP/MADT/HPET discovery; the local APIC's timer calibrated against the PIT and carrying the 100 Hz tick; the I/O APIC routing the keyboard and pointer, which are now interrupt-driven; atomics and a spinlock under the kernel's critical section; IPI delivery, an interrupt-free boot clock, and the HPET with the TSC calibrated against it; AP start-up attempted twice and pulled, with the firmware's behaviour documented; high-resolution timers, so `SLEEP` is a deadline and `TIME` reports nanoseconds; lock-order validation that finds a deadlock before two CPUs have to meet; a `waitpid` that really waits, on top of an idle loop |
| P2 | done, exit gates met | long-mode boot path end to end: stage2 builds the identity map, enters long mode and starts a 64-bit nucleus that reports over COM1 (`make check-longmode`, CI job `longmode`); freestanding C11 nucleus built by a probed cross toolchain, linked into the higher half at `0xFFFFFFFF80100000` with a second mapping in stage2, SYSCALL/SYSRET MSRs programmed and a `syscall` round trip proven; per-CPU blocks behind the GS base and a SYSCALL entry stub that switches to the CPU's kernel stack; ring 3 reached and returned to (user GDT/TSS, `U/S` user pages, `SWAPGS` at entry, three syscalls from the user program with the caller's stack and registers intact); an IDT that catches a ring-3 fault on the TSS `RSP0` stack, resumes it and reports any other fault instead of triple-faulting; a VMM over the frame allocator with 4 KB map/unmap, translation and a per-level U/S check, exercised by a self-test that faults on an unmapped page on purpose; a slab allocator over a dedicated heap window mapped by that VMM; a 100 Hz PIT whose ticks reach ring 3 through the TSS stack; two ring-3 tasks round-robined by that tick with per-task kernel stacks and per-task address spaces; NX on heap and stack pages with SMEP/SMAP enabled where CPUID offers them; a table-driven LP64 syscall ABI with write/yield/brk behind it; PCI enumeration and an ATA PIO block driver whose read/write paths are self-tested; an HBFS reader and VFS front-end, with `/init` loaded from that disk and `exec`/`open`/`read`/`close` behind it; a 32-bit program running in compatibility mode beside the 64-bit tasks through an `INT 0x80` bridge, with one of the tree's own flat binaries loaded unchanged at its `0x00300000` program base; directories and file metadata (`seek`/`stat`/`readdir`/`chdir`/`getcwd`) over a per-task working directory; a writable HBFS (`create`/`mkdir`/`unlink` over the allocation bitmap, plus writes through an open handle, so a file can be appended to) whose output the host test reads back out of the image; console input over COM1 with a line-based shell (`ls`/`cat`/`date`/`mkdir`/`rm`/`quit`) that the host test types at; a CMOS clock; a user-space heap, whole-file load and pipes, with `exec` passing a command line on to `getargs`; a VGA text console with real colour, cursor and clear calls; a PS/2 keyboard whose input the test types with real scancodes; a PS/2 pointer polled from the timer tick, with a `/mouse` cursor demo the test drives through real packets; fork/exec/waitpid, so the shell can run programs and report their exit codes; children that fault are retired instead of stopping the machine; kill and background spawn; an e1000 network driver with ARP and ICMP echo on top of it, so a ring-3 program can ping; the PC speaker behind `BEEP`; first ported subsystem is a 64-bit frame allocator over the E820 map, self-tested at 256 MB and at 8 GB |

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
- [x] Processes. `FORK` copies the caller's address space page by page and the
  saved syscall frame with it, so the child resumes at the same instruction with 0
  in RAX and its own copy of memory; `WAITPID` reaps it and hands back its exit
  code, freeing the slot, the kernel stack and the whole address space. Both are
  non-blocking by design (a syscall cannot switch tasks and resume), so waiting
  means asking again. A user program that faults is now *retired* like one that
  exits, with the vector in the exit code, instead of halting the machine - one bad
  program, or a file that was never a program, can no longer take the system down.
  The shell gained `run <program> [arguments]`: it forks, execs in the child and
  reports `sh: <program> exited 0x...`; `/motd` run as a program reports
  `0xfa00000d`, the #GP it takes.
- [x] Signals, as far as a single-user port needs them: `KILL` retires another
  task outright, with the signal in its exit code, and its parent's `WAITPID`
  reaps it like any other child. `SIGTERM` is what the shell's `kill <pid>` sends.
  The shell can also `spawn` a program and not wait for it, so `/spin` (a program
  that only pauses) runs in the background while the shell answers commands -
  which is what a preemptive scheduler is for - and `kill 7` stops it.
- [x] A mouse. `boot/lm_mouse.c` drives the same i8042 controller's auxiliary
  port: 0xF6 and then 0xF4 to start reporting, three-byte packets whose flags
  byte carries the buttons and the sign of each delta, and bit 3 of that byte as
  the only resync marker there is. It polls, like the keyboard, but from the
  timer tick - a hundred drains a second keeps a pointer in step with a hand
  without an IRQ12 handler - and `kbd_getc` hands auxiliary bytes to it instead
  of dropping them, so a packet is never split by a keystroke. `MOUSE` returns
  the cell in RAX and RDX and the buttons in RCX, with the presses since the last
  call latched above them so a slow reader still sees every click; the 32-bit
  bridge hands the tree EAX/EBX/ECX, which is the shape it expects. `/mouse`
  draws a block cursor where the pointer is, and the test moves the pointer with
  the monitor's `mouse_move` and `mouse_button` - real PS/2 packets - and checks
  that 20 cells right and 6 down arrives as `x=60 y=18` and that a click is
  reported.
- [x] Writing into a file that is already there, which is what `FWRITE`'s append
  form needs and what a log, a note or the tree's own `bcal` does all day:
  `open` takes a flag that asks for write access, `write` on that handle writes at
  the handle's position, and `seek` to the end followed by one write is an append.
  HBFS keeps a file in one contiguous extent, so `hbfs_write` grows it in place
  when the blocks after it are free and otherwise moves it - copy the sectors,
  release the old run, take a new one - and rewrites the directory entry with the
  new size. A write that does not cover a sector reads it, patches it and writes
  it back, and the slack past the new end is zeroed so it cannot show what an
  older file left there. The shell gained `append <file> <text>` (creating the
  file when it is not there), and the host test reads both the appended file and
  the created one back out of the image with the same constants `populate.py`
  uses.
- [x] A network adapter. `boot/lm_e1000.c` drives the 8254x family: the PCI
  handover (memory space and bus mastering, without which the rings are never
  read), the register block mapped into a window of the kernel's own half with
  caching disabled - BAR0 sits near 0xFEB00000, outside both RAM and the direct
  map, and a cached read of a device register would never see the device move -
  and two descriptor rings in frames the allocator hands out, with the physical
  addresses in the descriptors because that is what the card reads. Polled like
  the disk: the transmit path waits for the descriptor's done bit and the receive
  path reports whether a frame arrived. The boot self-test sends a real ARP
  request and prints the answer (`arp=52:55:0A:00:02:02 e1000=ok`), which proves
  the handover, the window, both rings, bus mastering, the MAC the device
  reports and the receive path in one 42-byte frame - and needs no IP address,
  because ARP is what a machine does before it has one. Every test boot now
  attaches an `e1000` behind QEMU's user-mode network so there is something to
  answer.
- [x] Enough of a protocol stack to answer "is the network real?": ARP to turn an
  address into a hardware address, and ICMP echo, in `boot/lm_net.c`. `NETPING`
  takes the four address bytes and a timeout, sends one echo request and waits for
  the matching reply, returning the round trip in the 10 ms the timer can actually
  see; the tree's `SYS_PING` (48) reaches the same handler through the bridge. `/net`
  pings the user-mode network's gateway three times and exits with the number of
  replies, which is what the test asserts (`net: 3 of 3 replied`). This is the first
  ported subsystem that a 32-bit program could use without knowing it moved.
- [x] Sound, as far as a machine with no codec needs it: the PC speaker, which is
  the 8254's third channel through the gate in port 0x61 - the one piece of hardware
  every PC has and the only one with nothing to read back. `BEEP` (frequency and
  duration) is the tree's `SYS_BEEP` (24, duration in ticks) through the bridge, and
  `/beep` plays three notes from ring 3. The boot self-test can only prove the gate
  bits read back what was written, which is what it checks; the rest is a person
  listening. The sound cards (SB16, AC'97) are still to come, and want an audio layer
  rather than a driver.
- [ ] What is left of P2's breadth: the sound cards above, and the rest of the 187
  calls.
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

All four are met, and what proves each one is a command anyone can run:

- `tests/test_longmode.py` boots the image twice - 256 MB for the boot assertions and
  the shell, keyboard, mouse and volume checks, then an 8 GB guest - and asserts the
  COM1 log, the VGA text buffer and the HBFS structures the guest wrote. `--marker-only`
  covers the hand-written asm nucleus.
- The 8 GB run reports `7167 MB of RAM`: the difference is the firmware's PCI hole
  below 4 GB, and the number is only right if the E820 map was read as 64-bit.
- The 32-bit side is unchanged: `make check` is green (2197 assertions), and one of the
  tree's own programs still boots from the 64-bit volume and runs in compatibility mode.
- The nucleus links to 73 KB, far inside the 1 MB gate.

What is deliberately left is breadth, not foundation: the remaining syscall numbers and
the drivers behind them, and the sound cards that want an audio layer rather than a
driver.

### P3 · Concurrency and scale (4 sprints)

- [x] ACPI discovery: `boot/lm_acpi.c` finds the RSDP by looking for its signature on
  a 16-byte boundary in the EBDA and then in the ROM area, checks the checksum, follows
  either the RSDT or - on a machine with a revision 2 RSDP - the 64-bit XSDT, and reads
  the MADT for the local APIC address, the enabled-CPU count, the I/O APIC and how many
  interrupt-source overrides the firmware published. Discovery only: nothing is
  programmed, so the machine behaves exactly as it did before, and the report is the
  proof - `[acpi] rsdp=000F52E0 rsdt=4 lapic=FEE00000 cpus=1 ioapic=FEC00000 acpi=ok`,
  with `cpus=` following `-smp` because the count comes from the table and not from a
  guess.
- [x] Local APIC: its register page (at the address the MADT named) is mapped into the
  driver window, the unit is switched on with everything masked, its timer is started
  and watched counting down, and then it is switched off again - because an interrupt
  from the APIC needs a gate in the IDT for the vector it would deliver, and nothing
  above the 8259's sixteen exists yet. `[lapic] id=0 version=14 count=994979->979229
  lapic=ok` is the proof, and the tick stays on the 8254 until the next step. Restoring
  the 8259's path afterwards is part of it: on a machine whose APIC can be enabled the
  PIC reaches the CPU through LINT0 in ExtINT mode, so a masked LINT0 silences every ISA
  interrupt, the timer among them.
- [x] The APIC timer as the tick. It counts at the bus clock divided by a divider the
  firmware does not publish and no two machines agree on, so it is *measured*: the
  PIT's spare channel is programmed as a one-shot ten milliseconds long, its output is
  read from bit 5 of port 0x61, and the APIC's count over that interval is the divisor.
  The unit is then enabled with vector 64 in its timer and vector 255 as its spurious
  vector (a spurious interrupt is never acknowledged - an EOI there would lose the real
  one behind it), and the 8259 is masked off completely: two clocks with different
  phases would double the rate and make every sleep, timeout and ping wrong. The
  scheduler's report proves the switch - `source=lapic` only holds when *every* tick
  came from the APIC - and the 100 Hz the rest of the port assumes is unchanged.
- [x] The I/O APIC, and input on interrupts. One chip takes the machine's 24 lines
  and forwards each to a CPU's local APIC on a vector of the driver's choosing; it is
  two registers, a selector and a window, and one 64-bit redirection entry per line.
  Every entry starts masked - an unmasked one pointing at a vector with no handler is a
  machine that dies on its first interrupt - and the keyboard (IRQ1) and pointer (IRQ12)
  are then routed through it to the boot CPU. That is what the interrupt is *for*: a
  keystroke is a fact, and polling only reads the port when somebody asks, so a program
  that computed for a tenth of a second used to lose every key pressed in it. The
  controller now drains into a 64-byte ring of scancodes from one handler for both
  devices, and `kgetc` translates from there, still falling back to the port for
  anything that arrived with interrupts off. Acknowledge moved with it: entries deliver
  through the local APIC, so that is where the EOI goes and the 8259 knows nothing.
  The test types at the machine with `sendkey` and requires `[ps2] irqs=` to be more
  than zero, which polling could never show.
- [x] Atomics and a real lock. Exchange, compare-and-swap and fetch-add are the
  instructions a second CPU cannot interleave, and `sched_lock` is now built on them:
  the outermost acquisition takes a spinlock word and turns interrupts off, so a tick
  can no longer observe a half-written file table, and the interrupt flag that was
  there before it is the one put back - a section entered from ring 3 leaves interrupts
  on, one entered from a syscall leaves them off. The boot self-test proves what one CPU
  can prove (`[lock] spin=ok atomic=ok irq=ok`), and every filesystem path in the suite
  is the regression test for the lock itself.
- [x] The boot clock, and the two pieces of AP start-up that could be proved on their
  own. `pit_delay_ms` waits on the PIT's spare channel - the one accurate interval
  available before there are ticks - and everything that has to wait during the boot
  now uses it, including the APIC timer's calibration (which got shorter for it).
  `lapic_send_ipi` sends an inter-processor interrupt and waits for the delivery-status
  bit, and the MADT's processor ids are kept, not just counted. Both are tested: with
  `-smp 2`, QEMU's own `-d int,cpu_reset` log shows `CPU Reset (CPU 1)` when an INIT is
  sent to the second processor, which is the delivery working.
- [ ] SMP to 8 vCPUs. Attempted twice and pulled both times: the firmware parks APs in a
  state that ignores a SIPI, and an INIT hands them back to the firmware's own path, which
  faults. See below for exactly what the evidence showed and what the next attempt has to
  read first.
- [x] A clock with resolution the tick does not have. The HPET is found through its
  ACPI table (a base address and nothing else to walk), mapped like any other device,
  and started; the firmware publishes its period in femtoseconds, so its rate is exact
  rather than measured. The self-test then checks it against the PIT - one tick of
  10 ms, timed by both, `10048us` against `10000us` - and waits half a millisecond with
  it, which is a length the tick cannot express at all. The TSC is calibrated against
  it in the same breath, because `rdtsc` is free but has no published rate:
  `[hpet] base=FED00000 hz=100000000 timers=3 bits=64`,
  `[hpet] a tick measures 10048us (the pit says 10000us), tsc=1005MHz waited=546us
  hpet=ok`.
- [x] Threads. `CLONE` takes an entry point and a stack top, both in the caller's own
  space, and builds a task that runs in that space instead of a copy of it - so what one
  writes the other reads, which is the point and the hazard. The frame is built rather
  than copied: it enters ring 3 at the entry with interrupts on, and the selectors carry
  their ring in the low two bits (0x23 and 0x1B), because `IRETQ` refuses a CS whose RPL
  is not the privilege it is returning to - the first version used the raw table indexes
  and took a `#GP` on the way back to ring 3. The space is shared, so it now outlives the
  first of its tasks to exit: the last one reaped releases it, which is what
  `space_users()` decides. `/threads` shows the whole thing - a thread writes into the
  parent's memory, exits with 7, and the parent's `WAITPID` collects it and prints what
  the thread left behind. Nothing serialises two threads yet: an allocator shared between
  them is still a race, and that is the futex milestone's problem.
- [x] A disk write that reports an error is retried. Chasing a flaky boot showed the
  write-through cache turning a flush into a real one and the poll giving up too early,
  which reads as a filesystem error. The flush is now retried with the sector and the
  wait bound is twenty times larger; a failing directory write says which sector it was,
  so the next one of these is not a mystery.
- [x] Futex, and a thread that sleeps instead of spinning. `FUTEX_WAIT` marks the task
  `BLOCKED`, records it against the word and switches away - which is allowed because the
  frame being left is the caller's *ring-3* frame, the case the ring-0 refusal does not
  cover - and `FUTEX_WAKE` puts it back on the run queue. The value is read inside the
  syscall, so a wake that arrives between the caller's test and the call is not lost: if
  the word has already changed, the wait returns at once. `/futex` parks a thread, wakes
  it eight yields later and collects it; nothing polls.
- [x] A heap that belongs to the address space. Threads share their creator's memory, so
  they now share the bump pointer and the free list as well - owned by the lowest-numbered
  live task in the space, a choice that is arbitrary but does not move when a thread exits.
  Two of each handed out the same block twice, which reads as a correct program corrupting
  itself.
- [x] Thread-local storage. The FS base is a task's, not a CPU's, so it is part of the
  switch: `become()` loads the task's value into `IA32_FS_BASE` next to the page tables,
  `SET_TLS` sets it to an address in the caller's own space, and a thread inherits its
  creator's until it asks for its own. `/tls` is the proof - two tasks, one address space,
  each writing and reading `fs:[0]` in the middle of the other's run: `tls: main read 1111
  thread read 2222`. A `wrmsr` per switch is a hundred cycles; FSGSBASE would make it a
  register write, and is the optimisation not the milestone.
- [x] High-resolution timers. The APIC timer stopped being a metronome and became a
  one-shot device, so a deadline is an instant on the HPET and not a count of ticks
  (`boot/lm_timer.c`). Sixty-four deadlines sit in a queue that is scanned on every timer
  interrupt; the one-shot is then armed for whichever is nearer, the next deadline or the
  next tick, so the 100 Hz tick survives at no extra cost. `SLEEP` parks the task against a
  deadline and a callback wakes it, `TIME` (47) hands ring 3 the same free-running counter
  in nanoseconds, and a wake-up now asks the scheduler for a switch at once instead of
  waiting for the next tick - without that the timer's accuracy was wasted on a scheduler
  that still thought in ten-millisecond steps. The boot self-test arms 1, 2, 4 and 8 ms
  deadlines and checks that the lateness does not grow with the interval; `/hrt` asks for
  two hundred one-millisecond sleeps and reports the total, the worst one and how many
  overran: `hrt: deadlines, not ticks` in about a quarter of the time a tick would take.
  Two things stay as they were, deliberately: a task inside a syscall is not switched out
  (that is preemption, and the next line), so a wake can wait for the tick when the running
  task is in the kernel; and a machine with no APIC timer still runs on the 8259's tick. The
  tick itself is now measured rather than assumed - ten ticks inside a tenth of a second,
  counted on the same counter the deadlines use - because it stopped being a device that runs
  by itself and became an entry in the queue.
- [x] Lock-order validation (`boot/lm_lockdep.c`). Every lock is given a class when it is
  initialised, the classes it is held inside are recorded as a graph, and the first path
  that takes the pair the other way round is reported - which is a fact about the code and
  not about the schedule, so a deadlock is found before two CPUs have to meet. The same
  stack catches the two mistakes one CPU makes alone: taking a lock it already holds, and
  releasing one that is not the innermost. The self-test plants four violations and counts
  what came back (`caught=5`), because a checker that agrees with everything looks exactly
  like a correct kernel; the boot then reports `locks=4 deepest=2 errors=0`. One class per
  lock object rather than per type, and one stack because interrupts are off while a lock
  is held - both have to change when the other processors start.
- [x] A `waitpid` that waits, and an idle loop to make it possible. A parent is parked against
  the child it is waiting for and the child's exit wakes it, so a shell running a program
  costs nothing at all instead of spinning on `pause`. That exposed the other half of the
  problem: two tasks can now be waiting at once - a waiter and a sleeper - so "nothing
  runnable" stopped meaning "finished". When no task can run the CPU idles on the boot stack
  (never on a dead task's, whose slot can be reaped and its stack freed underneath) until an
  interrupt makes something runnable, and only reports when every task is gone. `[wait]
  blocks=N wakes=N wait=ok`, and the two counts are the whole invariant: a wait answered by
  nothing and a wake with nothing to wake are the same bug.
- [ ] Per-CPU run queues, IPIs, TLB shootdown, preemptible kernel. The kernel half was
  attempted and pulled, and the attempt is worth more than the code was: both exit stubs now
  decide the GS-base swap from the frame they are returning to (see the reference), because a
  frame handed back by a syscall need not be the caller's. With that in place a thread holding
  a 400 ms note inside a syscall could be switched out and the machine stayed up - eight
  ring-0 switches - but the other thread still saw only one slice in four hundred
  milliseconds, so the switches were not being taken where they were expected to be. That
  count is the thing to chase next: `pick_next` found nothing runnable on twenty of the ticks
  that interrupted the kernel, which means the waiting task was not in the state the
  experiment assumed.

#### AP start-up: the plan for the next attempt

Three attempts have been pulled and the evidence is in the section below: a bare SIPI is
ignored because the parked AP is halted *inside the firmware* in protected mode (CR0.PE=1,
CR3=0, EIP=0xfd0b1) rather than in wait-for-SIPI, and an INIT hands it back to firmware code
that then triple-faults. So the next attempt starts by reading the firmware instead of
guessing at it:

1. Read SeaBIOS's own AP path — `src/smp.c` (how it copies and points its AP boot code, what
   it writes into the parameter block, and what address it SIPIs) and the 16-bit/32-bit entry
   it lands on in `src/romlayout.S`. The question to answer from the source is precise: what is
   the parked AP waiting for, and what does it do when it gets it?
2. From that, pick one of three: cooperate with the firmware's AP boot page (send the vector
   the firmware set up and let its code jump to our trampoline through the parameter block);
   or persuade the APs to stay in wait-for-SIPI by not letting the firmware's own SMP bring-up
   run (a different boot path); or give the firmware its INIT and wait long enough — hundreds
   of milliseconds, not the tens of microseconds the last attempt used — before the SIPI.
3. Evidence, before code: `-d int,cpu_reset` showed the difference between "ignored" and
   "reset" last time, and the QEMU monitor's `info registers -a` shows each CPU's state. A
   per-stage trace letter on COM1 from the trampoline was what made the first attempt
   debuggable and should be the first thing written again.

Step 1 has partly been answered, from the firmware this machine actually boots rather than
from upstream: `~/.local/share/qemu/bios-256k.bin`, at the address the parked AP was measured
at (EIP `0xfd0b1`, i.e. file offset `0x3d0b1`), is

    0xfd0af:  f4        hlt
    0xfd0b0:  eb fd     jmp  -3        <- a two-instruction halt loop
    0xfd0b2:  fa fc     cli; cld
    0xfd0b4:  66 b8 80 da 00 00   mov ax, 0xda80
    0xfd0ba:  8e d8     mov ds, ax

and what follows the loop is *not* a wake path - a correction to an earlier reading of these
same bytes. `0xfd0b2` is `cli; cld`, `mov ax, 0xda80`, `ds = ss = ax`, `sp = 0xfa18`, then a
jump into `0xfca61`, which reads and clears CMOS register 0x0F and hands over to the firmware's
16-bit reset path. That is the reset handler, laid out next to the halt loop by the linker and
not by design: two instructions of padding is all that separates them, and reading a
neighbouring routine as a handshake was a mistake worth recording so the next attempt does not
repeat it.

What that means is narrower and more useful: the firmware parks its APs with `hlt; jmp` and
provides nothing to hand them over to. There is no landing pad to aim at and no parameter block
to fill in, so the tool cannot be "cooperate with the firmware's AP stub" - it has to be either
a boot path where the APs are still in wait-for-SIPI when we get control, or taking a parked AP
over with an interrupt whose handler redirects it, which needs a gate in an IDT the AP is
actually using (the firmware's, not ours).

The experiment that decides between the two, before any kernel code is written: boot the same
image with `-smp 4` under QEMU and check, in the monitor, whether the other three cores end up in
that same `hlt; jmp` loop (`info registers -a`, EIP `0xfd0b0`) or stay in wait-for-SIPI
(`HALT=0`, no CS loaded). If they are in the firmware loop with an MBR boot, then the entry path
is what has to change - a loader the firmware hands control to earlier, or a firmware option that
leaves the APs alone - and that is a design decision rather than a patch. Half an hour of
monitor work answers it, and it is the first thing to do next.

So the AP is not in wait-for-SIPI at all - it is halted in a `hlt; jmp` loop inside the
firmware, which is exactly why a bare SIPI was ignored: SIPI only means anything to a core in
wait-for-SIPI. It also says what the right tool is. The AP will leave `hlt` on an *interrupt*,
and its handler is what has to redirect it (the firmware-style trick is to rewrite the stacked
return address so the `iret` lands elsewhere); or the loop is polling a flag the BSP writes and
the interrupt only exists to break it out of the halt. Either way the handshake is with the
firmware's AP stub, not with our trampoline, and the segment setup right after the loop
(`ax=0xda80`, then `ds`/`ss`/`sp`) is the stub the next attempt has to make sense of.

Nothing in this is worth doing until step 1 has that answer: the three pulled attempts all
guessed, and the guesses were wrong in the same way.

#### AP start-up: what was tried, and what it taught

A 16-bit to 32-bit to 64-bit trampoline was written, copied to a page below 1 MB,
described to the processor through a parameter block (CR3, stack, entry, the boot
processor's GDTR and IDTR), and started with the prescribed sequence. It printed a letter
per stage on COM1, because a processor that faults before it has an IDT can only be traced
by the code that is faulting. Its details were the ones that matter: the trampoline's own
GDT has to agree with the kernel's about selectors 0x08 and 0x10, because the IDT's gates
name 0x08 for the kernel's code segment; PAE, PGE and the SSE bits go into CR4 before
paging is switched on; the kernel's GDT and IDT are loaded only once those selectors mean
the same thing.

It does not work under this machine's firmware, and the tree does not keep code that does
not work. The evidence, in the order it was gathered:

- **The processor is not where the architecture says it should be.** Inspecting CPU 1 with
  QEMU's monitor shows it halted in *protected mode inside the ROM*: CS is a 32-bit code
  segment, CR0.PE is set, CR3 is zero, and EIP is 0xfd0b1, which is the firmware's own
  AP park loop. The architectural state that accepts a SIPI - real mode, waiting for one -
  is not this.
- **So a SIPI is ignored.** The delivery-status bit clears, the machine is fine, and not
  one letter of the trampoline's trace appears: the vector page was never fetched.
- **An INIT does reach it** - QEMU's `-d int,cpu_reset` log records `CPU Reset (CPU 1)` -
  **and then the whole machine resets**, which is a triple fault on the AP taking the
  system with it. That happened with the trampoline at 0x9000 (where the firmware keeps
  its own AP trampoline, SIPI vector 0x09 - not a safe choice), at 0x8000, with the delay
  between INIT and SIPI at 10 ms and at 1 ms, and with the INIT's trigger bits corrected
  to level-asserted as the architecture requires. So the AP's post-INIT path is the
  firmware's, not ours, and it ends in a fault.

What that means for the next attempt: the firmware has to be part of the answer rather
than bypassed. Either an AP is started **through the firmware's own handshake** - its
SIPI vector at 0x9000 plus whatever word in its data area it reads to decide whether it
is running for the BIOS or for an OS - or the machine is booted by something that leaves
APs in the architectural wait-for-SIPI state. Both are a bigger piece of work than one
milestone, and neither is guessable: the next attempt starts by reading the firmware.

Two things came out of the attempt anyway, and are in the tree: the boot clock above, and
one lesson about the console. A bounded spin in `kputc` looked like robustness - a UART
that stops answering should cost characters, not the machine - and it silently dropped log
lines, because the transmit register stays busy while the host is not draining the socket.
It went back to waiting.

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
