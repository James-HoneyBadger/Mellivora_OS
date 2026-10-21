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
| P2 | in progress | long-mode boot path end to end: stage2 builds the identity map, enters long mode and starts a 64-bit nucleus that reports over COM1 (`make check-longmode`, CI job `longmode`) |

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
- [ ] ASLR for stack/heap (mmap base is already randomised); NX (with P2).
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
- [ ] Cross toolchain (`x86_64-elf-gcc`), dual-target build.
- [ ] Higher-half kernel, per-CPU blocks, `SYSCALL` entry.
- [ ] Port PMM/VMM/slab/scheduler/timers/syscalls and the driver set.
- [ ] LP64 ABI plus a 32-bit compat shim so existing binaries keep running.
- [ ] NX, SMEP/SMAP where the CPU provides them.

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
