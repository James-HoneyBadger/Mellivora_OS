# Mellivora OS — Roadmap to a Windows 2000-class system

This is the working plan for turning Mellivora into a system that a 2000-era user
or administrator could sit down at and do the same jobs with it that they did with
Windows 2000 — without noticing that the lineage is different.

The target is **functional equivalence, not protocol compatibility**. Mellivora's
share protocol is not SMB, its installer is not MSI, its configuration store is not
the registry, and its filesystem is not NTFS. Nothing has to be Microsoft's format
to pass the scorecard below, and that is what keeps a plan of this size tractable.

The target edition is **Workstation**. Server-only features — domain controllers,
directory services, Terminal Services — are out of scope. Administering a
workstation remotely is in scope.

The plan is deliberately measured. Every wave closes against a test that fails
today, not against a list of finished features.

## Architectural decisions

| # | Decision | Rationale |
| --- | --- | --- |
| D1 | One 32-bit x86 target | The kernel, drivers and syscall ABI are 32-bit protected mode. There is one build and one image; PAE is the ceiling-raise if more than 4 GB is ever needed. |
| D2 | NASM for the core, freestanding 32-bit C for new subsystems | The boot path, entry stubs, context switch and the existing modules are assembly and stay assembly. The executive, driver model, networking services and shell facilities in M1–M9 are too large to write by hand in asm at a maintainable rate. One calling convention (cdecl, packed structs, documented in `docs/spec/ABI.md`) is fixed at the boundary between the two. |
| D3 | Everything is an object reachable through a handle | Resource access today is by raw number. Named, reference-counted, typeless objects with per-handle rights are what make sharing, waiting on, securing and remotely managing a resource possible at all. |
| D4 | Security is enforced, not advisory | `chmod`/`chown` store an owner and a mode, but `open` does not check them. A 2000-class system denies access, audits the denial, and lets an administrator explain why. |
| D5 | Parity through our own protocols | SMB, MSI, NTFS and the registry are each a multi-year compatibility project. A working equivalent that is *ours* satisfies the user story; a half-finished compatible one satisfies nobody. |
| D6 | Monolithic kernel with a loadable driver model | Best performance per unit of effort, and the existing drivers already live in the kernel. Loadable modules give PnP and third-party drivers without moving to a microkernel. |
| D7 | The compositor stays in the kernel for this plan | The surface IPC and damage-rect compositor already work. A user-space display server is a later, separately gated decision. |
| D8 | ACPI-first hardware model, QEMU-first testing | Every hardware path in this plan is one QEMU can emulate, so the test suite stays honest. Real-hardware coverage is a separate, later campaign. |
| D9 | **A 3 GiB user / 1 GiB kernel address space over PAE** | The flat identity map caps a program at 1 MB and RAM at what a bitmap describes. PAE is the ceiling-raise D1 named: 64 GiB of physical memory, 3 GiB per process, and a kernel that is no longer addressed by physical address. |
| D10 | **No accidental ceilings** | Every ceiling we have hit was a hand-sized array: 128 tasks, 224 directory entries, a 2 MB kernel window that is 95 % full. A limit is either hardware or documented policy. The rest are removed as the waves touch them. |
| D11 | **The 187 existing syscalls are a permanent compatibility layer** | "Monumental" must not mean "rewrite 278 programs and every test". The old calls keep their numbers and meanings forever and are re-implemented over objects (D3). |
| D12 | **Specifications describe the target, not the tree** | A spec constrained by today's buffer size is not a contract, it is a description. Each document marks what exists as **as built** so the distance is visible and countable. |

## The parity scorecard

The scorecard is the definition of done for the whole plan. Each row is a job a
person does, not an API that exists. Status is computed from implementation and
workflow-test evidence; merely finding a subsystem symbol does not earn a pass.
The generated block below is the sole progress-status table. Run
`make check-parity-update` with the change that earns a status change.

<!-- parity-scorecard:start -->
| # | User job | Pass criteria | Status | Missing evidence |
|---:|---|---|---|---|
| 1 | Log on | Graphical logon, per-user profile and desktop, switch user without reboot | **Pass** | — |
| 2 | Manage users | Accounts, groups, passwords, quotas and permissions edited from a GUI | **Pass** | — |
| 3 | Run services | Services start before logon, have dependencies and restart on failure | **Pass** | — |
| 4 | Configure the machine | One hierarchical transactional store for kernel, service and user settings | **Pass** | — |
| 5 | Install software | Install and uninstall with dependencies, registration and clean removal | **Pass** | — |
| 6 | Use the desktop | Multi-app shell, taskbar, file manager, drag-drop, clipboard, associations | **Pass** | — |
| 7 | Multitask hard | Preemptive, protected, per-process memory; a hung app cannot take the machine down over the 24-hour containment run | **Partial** | `.github/workflows/reliability-soak.yml: SOAK_MINUTES=1440` |
| 8 | Network | TCP/IP, DHCP, DNS, and a shared folder another machine mounts | **Pass** | — |
| 9 | Print | Spooler with queued, cancellable jobs | **Pass** | — |
| 10 | Detect hardware | ACPI, PnP enumeration, power states, sleep and soft-off | **Partial** | `kernel: pnp_` |
| 11 | Survive failure | Crash dump, safe mode, recovery console, filesystem repair and restore points | **Partial** | `kernel: crash_dump or kernel_dump`; `kernel: safe_mode` |
| 12 | Develop for it | Documented driver model and SDK, shared libraries, a C runtime, samples | **Pass** | — |
| 13 | Administer remotely | Remote console, remote service and configuration management | **Pass** | — |
| 14 | Play media | Mixed audio from several programs, video playback, a media framework | **Partial** | `kernel: audio_mix`; `kernel: video_play or video_decode` |

Current score: **10 pass, 4 partial, 0 fail**. This table is generated by `tools/parity_scorecard.py`; update it with `make check-parity-update`.
<!-- parity-scorecard:end -->

## End-to-end user journeys

These are release-level workflows, not substitutes for the subsystem
conformance tests. They make integration gaps visible even when each component
passes in isolation.

| Journey | Acceptance workflow | Owning wave | Current gap |
|---|---|---|---|
| First use | Install from a clean disk, create the initial account, configure networking, log on, and reach a usable desktop without editing image files by hand | M4/M6/M10 | No complete first-run wizard |
| Application lifecycle | Install a package, see its shortcut and file association, open a document, change the default handler for one user, uninstall, then verify registration and files are removed while user data follows the user's choice | M6/M10 | Package registration, association integration and clean removal |
| Workgroup sharing | On two clean VMs, authenticate, mount a share, create/read/write a file, verify denied access, disconnect, then restart a remote service from the management UI | M7 | Share server/client and remote administration |
| Removable device and print | Attach USB storage, copy and verify a file, safely eject it; queue three print jobs, cancel the second, and verify the first and third complete | M8 | USB storage and print spooler |
| Recovery | Break a service or driver configuration, boot the recovery path, identify the failure from logs, restore the previous configuration and user data, and verify a normal reboot | M9 | Guided recovery/restore workflow |

Each journey must become an automated QEMU acceptance test before its owning
wave closes. Until the necessary feature exists, it remains a named gap rather
than a skipped or success-shaped test.

## Gates

A wave does not close until these exit criteria are measured and reported.
`make check` runs abbreviated regression gates; it does not by itself satisfy
the long-duration containment, concurrency, or durability criteria. The
monthly `Extended Reliability` workflow runs a four-hour SMP soak and 100
crash-recovery iterations. The full exit tests remain explicit:
`SOAK_MINUTES=1440 make soak` and `CRASH_ITERS=10000 make crash-soak`.

| Gate | Target |
| --- | --- |
| Isolation | A hostile program cannot read, write, signal or open anything it was not granted |
| Containment | A faulting user program is retired; the machine survives 24 hours of it |
| Concurrency | 4 vCPUs under mixed load for 24 hours with no panic and no lost wakeup |
| Durability | 10 000 power-cut iterations on a written volume, every one recoverable |
| Latency | Measured by `make check-perf` on the profile machine; gated on regression against `benchmark/guest-<profile>.txt`. The µs targets (syscall < 1 µs, switch < 5 µs, fork+exec < 5 ms) need a clock of record, which is M2's |
| Footprint | Measured by `make check-perf`: kernel-reserved bytes at boot < 16 MB, plus < 5 s to the desktop under QEMU |
| Admin | Every setting in the machine is reachable from the management console and the store |
| Parity | `make check-parity` reports at least the baseline, and no row ever moves backwards |

## Where the tree stands

What the plan builds on, and must not regress:

- Preemptive four-level priority scheduler, `fork`/`exec`/`waitpid`, threads
  (`SYS_CLONE`, sharing one address space and its own stack and TLS per thread),
  per-thread signals, pipes, shared memory, message queues, semaphores,
  `mmap`/`mprotect`, copy-on-write fork and per-task file descriptors — see
  [sched.inc](../kernel/sched.inc).
- Demand paging with file-backed mappings, a page cache with writeback and
  guarded demand-grown stacks — see [vm.inc](../kernel/vm.inc) and
  [paging.inc](../kernel/paging.inc).
- HBFS with a directory tree, extended attributes, permissions and a
  crash-consistent journal, behind a VFS with a block cache and page cache — see
  [hbfs.inc](../kernel/hbfs.inc), [vfs.inc](../kernel/vfs.inc) and
  [blkcache.inc](../kernel/blkcache.inc).
- HBFS v4 volumes are **verifiable and writable**: the superblock, bitmap and every
  directory run are checked against their CRC-32C, the writer restamps each run it
  changes (including partial bitmap writes and new subdirectories), and extent files
  read through the run list. A volume without the journal feature mounts read-only.
  `make check-hbfs-v4` proves the mount, fragmented reads through `cat` and `fd_read`,
  a fragmented program load, a written file and directory that still pass the checksum
  audit, the read-only case, and rejection of corrupted metadata.
- TCP with a full state machine, ARP, ICMP, DHCP and DNS, sockets, `poll` and
  non-blocking connect — see [net.inc](../kernel/net.inc).
- ATA PIO and bus-master DMA, AHCI, virtio-blk, e1000 and rtl8139, AC'97 and SB16,
  PS/2 keyboard and pointer, VBE/BGA graphics, serial and PCI enumeration.
- The Burrows compositor desktop with surface IPC, damage rectangles, clipboard,
  file dialogs and notifications — see [burrows.inc](../kernel/burrows.inc).
- 210 syscalls, 134 shell built-ins, 295 NASM programs and 15 C program
  sources, an in-OS C compiler, `hbpkg`, and a C-built kernel module with a
  freestanding C runtime behind it.

## Waves

### M0 · Measurement, contract and the C foundation (3–5 weeks) — **closed**

- **The parity scorecard (landed).** `tools/parity_scorecard.py` computes the
  fourteen rows below from evidence in the tree and compares them with
  `docs/parity-baseline.json`; `make check-parity` runs it, `make check` and CI
  both fail when a row regresses, and `make check-parity-update` is the only way
  a row improves — reviewed with the change that earns it. The baseline has
  moved once since it was recorded: "Detect hardware" went `fail` → `partial`
  with the ACPI tables.
- **The machine is measured, not just the build (landed).** `make bench` times
  host make targets, so it says nothing about the machine under test — and the
  Latency and Footprint gates above name numbers nothing measured.
  `programs/perf_test.asm` runs in the guest and reports TSC cycles for a
  syscall round trip, a yield with a second task ready, and a
  fork-exec-waitpid, plus the free-frame counts that give the footprint;
  `tests/bench_guest.sh` (`make check-perf`, part of `make check`) compares them
  with `benchmark/guest-<profile>.txt` and refuses a regression, taking the best
  of three samples because a single sample of a shared host can be three times
  slow through no fault of the tree. Units are cycles, deliberately: the TSC
  rate is not architectural and nothing publishes it, and measuring it against
  a sleep gave 163–6427 cycles/µs on one machine. Microseconds arrive with M2's
  clock of record.
- **The syscall ABI, versioned (landed).** `docs/spec/ABI.md` §6 makes the
  syscall table the one place a number is allocated, `docs/syscalls.json` and
  `docs/syscalls.md` stay generated from `programs/syscalls.inc`, and
  `c/include/mv/syscall_numbers.h` is generated from the same file so a C
  program cannot fall out of step with the numbers. `make check-syscall-header`
  is part of `make lint`.
- **C toolchain bootstrap (landed).** `tools/find_c_toolchain.sh` probes
  `i686-elf-gcc`, `i386-elf-gcc`, `gcc -m32` and `clang --target=i386-elf`, and
  proves each by compiling a probe and checking the object is 32-bit ELF and
  links with `ld -m elf_i386`; the Makefile reads its answer and the build now
  requires one — `make check-toolchain` reports which. The build integration, a
  linker script per C target, and a freestanding runtime
  (`c/rt/string.c`: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`,
  `strncmp`) landed with two real consumers: **a C-built kernel module**
  (`c/kmod/`, linked at a measured address, assembled into the image with
  `incbin`, `cmod` on the shell runs its self-test, `make check-cmodule` reads
  the image back) and **a C-built user program** (`c/prog/`, linked at
  `PROGRAM_BASE`, `cprog` in `make check-runtime`). The module boundary is one
  structure each way, with every offset asserted at compile time
  (`c/include/mv/cmod.h`).
- **Per-task FPU and SSE state (landed).** Each TCB carries a 512-byte image at
  `TCB_FPU_AREA`; every switch path saves and restores it with
  `fxsave`/`fxrstor`, and `CR4.OSFXSR`/`OSXMMEXCPT` are set unconditionally.
  There is no second format: FXSR, SSE and SSE2 are part of the CPU floor
  ([PLATFORM.md](spec/PLATFORM.md#81-the-cpu-floor-is-a-pentium-4)), so a part
  without them is refused at boot. `fpu` saves two distinct x87+SSE states,
  disturbs the live registers and requires each back — at boot, where
  `make check-boot` requires `FPU_SELFTEST_OK`, and on demand in
  `make check-runtime`; `programs/fpu_test.asm` and `programs/sse_test.asm`
  prove the per-task half with two tasks.
- **Normative specs.** `docs/spec/ABI.md` is written and covers the boundary
  this wave built, and every document the [specs table](#specs) names now exists:
  the three that were owed when M0 closed (`OBJECTS.md`, `DRIVERS.md`,
  `CONFIG.md`) were written by the waves that needed them.

**Exit:** met.

| Criterion | State |
| --- | --- |
| The scorecard runs in CI and reports the true baseline; a row moves only by adding evidence | **Met** — `make check-parity` (13 checks), `make check-cmodule` |
| A C-built kernel module and a C-built user program both run | **Met** — `cmod` prints `CMOD_OK`, `cprog` prints `C_PROG_OK` |
| A floating-point workload across a context switch is correct under `make check` | **Met** — `fpu_test` (`FPU_OK`) and `sse_test` (`SSE_OK`) pass, the round trip is proven before any task exists, and since the CPU floor became a Pentium 4 it is also proven on every boot: `make check-boot` requires `FPU_SELFTEST_OK` rather than accepting a SKIP |

The third criterion was blocked by a defect this wave did not cause:
`pmm_alloc_page` returned with `ECX` clobbered, so `paging_clone_pd` copied every
new page table from physical address 0 and every forked address space was built
with no kernel in it. That is fixed, and the contract is now checked at boot by
`pmm_selftest` so the same class of bug cannot return quietly. The full story is
in the CHANGELOG under "Fixed: two tasks could not run".

### M1 · The executive: objects, handles, security (8–12 weeks) — **closed**

The highest-leverage change in the plan. Everything after it is cheaper.

- **The Warren** — the object namespace. Typed objects (file, section, process,
  thread, socket, event, key, device), a per-process handle table with access
  rights, reference counting, name lookup, and wait-on-multiple-objects.
- Existing resources re-expressed over handles with the current syscall numbers as
  compatibility aliases, so all 2 197 existing assertions keep passing.
- **Tokens and access control**: users, groups, an access-control list per object,
  access checks on every open, privileges, and audit hooks that feed the event log
  in M4.
- A deferred-work interrupt model (a DPC analogue), which the SMP work in M2 needs.
- Optional: `poll` on handles rather than sockets only — the waiting primitive
  belongs in the executive, not in the network stack.

- **Permissions enforced (landed).** The mode word and owner stored per entry
  are checked, not advisory: `open` for read and for write, `SYS_FWRITE` over an
  existing file, and `unlink` are refused when the caller lacks the bit; `chmod`
  is the owner's to make and `chown` is root's; `setuid`/`setgid` move the
  effective ID the checks read. `programs/perm_test.asm` proves root bypass,
  the owner case and the stranger case in `make check-runtime`.
- **Directories have rights too (landed).** Adding a name is a write to the
  directory, so `create` and `mkdir` need its write bit; `rename` needs write on
  the file and the directory, `truncate` on the file, `rmdir` on the directory.
  The directory's own mode and owner come from the entry that names it in its
  parent. `programs/dirperm_test.asm` proves it as a user who owns both test
  directories, so a refusal cannot be a permission accident.
- **A handle carries its rights (landed).** A read-only descriptor cannot be
  written back and a write-only one cannot be read, even as root;
  `programs/handle_test.asm` is the gate. This is the seam the Warren's object
  model plugs into.

- **The conformance gate (landed).** `programs/hostile_test.asm` runs as an
  ordinary user and requires eleven refusals and two controls, and it has been
  seen to fail when an enforcement is disabled. Fault containment is the other
  half and already existed: `stack_overflow` must kill the offending task while
  the shell runs the next command.
- **Signals have a uid policy (landed).** `kill` reaches only the sender, a task
  of the same effective user, or anything when the sender is root; a group
  signal is refused whole rather than half-delivered. `setuid`/`setgid` move the
  effective ID the check reads. The hostile gate covers both directions - a
  stranger's task must be unreachable, the caller's own child must not be.
- **`SYS_SLEEP` no longer hangs.** The fallback that runs when no other task is
  ready waited on a tick count held in a register the scan had overwritten, so a
  parent sleeping while its children were blocked waited for millions of ticks.

**Exit:** a hostile-program conformance suite where a deliberately misbehaving
program fails to read, write, signal or open anything it was not granted, and
cannot take the machine down trying.

That suite exists and passes - `hostile_test` for the file and signal rules,
`object_test` for the object model's refusals - and the machine runs the next
case afterwards, which is the other half of the claim.

The Warren is built: typed objects with one 512-byte body, a per-process handle
table with generations and rights, one namespace, wait sets, tokens and an
access check, and a deferred-work queue.

**The cross-task wake works now**, and it took three fixes that nothing could
have caught while every wait in the tree was satisfied by the task that asked
for it: `obj_satisfy_waiters` handed `obj_wait_consume` the evaluation result
instead of the wait set (a kernel fault the first time a signal completed
somebody else's wait), `obj_yield_cpu` turned "no task ready" into an instant
timeout even when the signaller was merely asleep, and `sys_waitpid` inherited
an expired `TCB_WAKEUP` and returned its own syscall number instead of an exit
code. `crosswait_test` proves both directions in `make check-runtime`.

**Objects are accounted for as well as built.** A process may hold 256 live
objects, the ceiling is a refusal that costs nothing rather than a starved slab,
and a close returns the charge - `objquota_test` is the gate. Every successful
`SYS_OPEN` is a `File` object named after its path, owned by the descriptor,
released by the close and by task exit; `fileobj_test` proves it and
`fd_isolate_test` proves an object survives one task closing a number another
still holds. Getting that right meant teaching the refcount that it follows the
handle: a task exiting never switches out, so its private descriptor page is
stale and had to be flushed before draining, and a new task inherits a copy of
its creator's descriptors and therefore owes a reference per entry. The shell
gained the two commands the spec asked for, `objects` and `handles`.

That is what closes the wave. What is left in the object model belongs to waves
that have not run yet, and is written down in
[OBJECTS.md](spec/OBJECTS.md#12-as-built) rather than left implied: the nine
other types arrive with the wave that owns each one, and a file becomes a
handle when the VFS wave replaces the frozen fd path.

### M2 · The machine: ACPI, APIC, SMP, PnP and drivers (14–20 weeks) — **closed**

Where Mellivora trails a 2000 machine most visibly: it cannot use a second CPU,
sleep, or switch itself off.

- **Tables read (landed).** `kernel/acpi.inc` finds the RSDP in the EBDA or the
  BIOS area, validates its checksum, follows the RSDT - or the XSDT from
  revision 2, taking the low half of each 64-bit entry - and reads the MADT for
  the processor count, the local APIC address and the I/O APICs. The boot line
  and `ACPI_BOOT` on COM1 report it; `acpi` prints the table of contents.
  Nothing is programmed yet, so this is inventory, not a change of behaviour.
- **Interrupts follow the hardware (landed).** The interrupt controller is
  chosen by probing, not by trusting the table: the local APIC is enabled and
  read back, and only if the write sticks are the 8259s masked and the I/O APIC
  programmed. QEMU's MADT reports a local APIC even on a model that has none -
  a table describes a machine, it does not guarantee one - so the `p4-noapic`
  profile (the same Pentium 4 with APIC removed from CPUID) stays on the 8259,
  and `make check-runtime` asserts it. Redirection entries come from the
  MADT's interrupt source overrides, which is how the PIT lands on pin 2 rather
  than pin 0; lines the table does not describe are taken as ISA edge/high,
  except the chipset's two PCI link lines (GSI 10/11), which are level/low
  until _PRT parsing replaces the rule.
- **An SMP machine works.** Before this, two vCPUs meant no interrupts at all -
  no tick to wake a halt, no keyboard - so the machine was unusable. Two CPUs
  on a Pentium now boot, take input, run programs and serve the network;
  `make check-runtime` holds all four cases.
- **Application processors start and park (landed).** INIT-SIPI-SIPI to every
  APIC ID the MADT lists, a 16-bit trampoline at 0x8000 that changes mode and
  loads the page directory, and a per-processor stack. They halt with interrupts
  off: a parked AP proves delivery and the shared address space without putting
  a second thread through kernel code that assumes it is alone. What remains of
  SMP - per-CPU state, scheduler and allocator locking, lock-order validation,
  and letting an AP do work - is the next increment, and it lands behind a gate.
- **Spinlocks with a rank order (landed).** Every lock carries a rank and a
  processor may only take one of strictly higher rank than any it holds, so an
  inversion cannot be expressed; a violation names both locks and stops the
  machine rather than deadlocking later. A self-test - at boot and on demand via
  `locks` - requires the gate to refuse the wrong order and grant the right one,
  and has been seen to fail when the check is bypassed. Nothing takes a lock
  yet: the lock users (frame allocator, block cache, run queue) come with
  per-CPU state, which is the next step.
- **The tick belongs to the local APIC timer (landed)** where there is one, and
  to the PIT everywhere else. The APIC timer's rate is measured against the PIT
  and sanity-checked before it is trusted; a bad measurement keeps the PIT. The
  PIT's line is masked afterwards so the clock is not counted twice. This is
  also the prerequisite for per-CPU timers when application processors start.

- **Power is real (landed).** The FADT is parsed - PM1a/PM1b event and control
  blocks, PM_TMR, SCI_INT, SMI_CMD, RESET_REG, FACS - and ACPI is taken over by
  writing `ACPI_ENABLE` to `SMI_CMD` and polling `SCI_EN`, not assumed. The S5
  encoding is read out of the DSDT's `\_S5` package by a scanner that understands
  the AML it needs and skips everything else, because the encoding is
  per-machine and there is no architectural value to guess. `shutdown` powers
  the machine off, `reboot` uses the FADT's reset register, the power button is a
  fixed ACPI event on the SCI that syncs and powers off, and the desktop's
  "Shut Down..." dialog reaches the same paths. Thermal zones and batteries are
  still only enumerable, because reading them needs an AML interpreter - that is
  not part of this wave's exit and is listed below.
- ACPI: RSDP/RSDT/XSDT discovery, MADT, FADT, the SCI interrupt, the power button,
  sleep states and soft-off, thermal and battery where present.
- Local APIC timer taking over the tick from the PIT; I/O APIC routing input;
  inter-processor interrupts.
- **SMP bring-up**: application-processor start, per-CPU state behind a segment
  base, scheduler and allocator locking, and lock-order validation that finds a
  deadlock before two CPUs do.
- **The driver model (landed).** A driver is a record - name, match table,
  attach, detach, power, order - registered through a validated module header
  and bound by a manager against a device tree built from PCI enumeration and
  the platform devices. I/O ranges, memory windows and interrupt lines are
  arbitrated (PCI lines share by design, ISA edges do not), a claimed window is
  mapped for the driver, and binding happens in two phases around the frame
  allocator and PCI. All thirteen in-tree drivers are behind it, nothing in the
  boot path calls one directly, and `drivers` prints what each is bound to or
  what it was refused. What remains - relocatable modules, device stacks, IRPs,
  filters, per-device power objects - is written down in
  [DRIVERS.md](spec/DRIVERS.md#11-as-built-the-gap).
- **Per-CPU state and application processors that work (landed).** Each
  processor has a block behind GS with its index, idle state, job and interrupt
  counters; APs load the kernel's IDT, enable their own local APIC and run a
  work queue fed by a ranked lock and an IPI. `smpstress` is the compressed exit
  test and reports per-processor work.
- Structured boot logging, so a machine that fails to enumerate says why
  (landed: a 256-record ring with level, processor and tick, mirrored to COM1,
  readable with `bootlog`).

**Exit:** four vCPUs under 24 hours of mixed load with no panic; sleep and soft-off
work from the shell and from the desktop; every driver loads as a module.

All three are met. The intermittent stall that kept the wave open was a real
data race, and finding it needed two more bugs removed first:

- the work queue returned each job through a *static* slot that every processor
  shared, written under the lock and read after it, so two processors popping at
  once overwrote each other's result: one job ran twice and another vanished,
  the queue drained to empty and every processor went idle waiting for work that
  no longer existed. The job now travels in a register;
- `smp_delay_ms` reads the PIT, whose latch and count bytes are one set of
  registers for the whole machine, so four processors reading it at once could
  each take half of somebody else's reading - the "stall" report was firing on
  that. Delays inside jobs use the per-processor time-stamp counter instead;
- the AP idle loop lost wakeups between the empty-queue check and `hlt`; the
  check and the halt are now indivisible.

`make check-smp` runs four processors under mixed load - kernel jobs on the
application processors and user programs on the bootstrap processor - for a
minute, and it is part of `make check`. Runs of one, two, three and six minutes
have all completed with every round finishing and the machine still answering;
the 24-hour run is `SOAK_MINUTES=1440 make soak`.

### M3 · Storage: HBFS v5 (12–18 weeks)

**HBFS v4 in the tree today** (the bridge to the list below): a verified, journalled,
writable v4 format with CRC-32C on the superblock and every metadata run; extent files
read through all three readers; the writer restamps partial bitmap writes, directory
runs and new subdirectories. Extent files can be deleted (each run and the extent block
is freed), and `format` now creates a checksummed v4 volume instead of the legacy v2 one.
Extent files are created when no contiguous run fits, truncated by rewriting their
run list, and deleted by freeing each run. ACLs and derived per-user quotas have
since landed, as have alternate streams and compressed files; snapshots remain v5
material.

- On-disk v5: per-file access control lists, alternate streams, per-user quotas,
  transparent compression, encryption at rest, sparse files, hard links, and
  snapshots that back the restore points in M9.
- Volume manager: MBR and GPT, multiple volumes, mount points rather than drive
  letters, removable media with safe ejection, volume labels and health.
- Online repair plus an offline checker at parity with the current `fsck`, and a
  recovery path for a volume whose journal cannot be replayed.
- The redirector seam in the VFS that network shares plug into in M7.

- **Durability is a gate, not a claim (landed).** `tests/test_powercut.sh` runs
  the crash harness over rotating workloads - directory creation and metadata
  churn - killing QEMU at random points with no clean shutdown, then proving
  with fsck that the volume recovers to entirely-old or entirely-new metadata
  and never a mixture. `make check-durability` runs it on every build with a
  handful of crash points; `CRASH_ITERS=10000 make crash-soak` is the exit test,
  and 200 iterations of that command have run clean. The harness itself was
  repaired to earn that number: a boot that never reached the shell used to
  escape as a socket exception and be reported as "a power-cut point left the
  volume inconsistent", which is how a clean run first looked like a failure.
  It now retries the boot once, prints what QEMU said before it died, runs fsck
  on the image anyway, and keeps the 2 GB copy per iteration only with
  `--keep` - the run that finally passed also stopped leaving 200 of them in
  `/var/tmp`.
- **The volume manager (landed).** The partition table is read - MBR, or GPT
  behind a protective MBR - and the filesystem is probed for rather than
  assumed, so a disk with no filesystem says so and `volumes` can name what the
  filesystem is inside. Labels, multiple volumes and mount points are the rest
  of the volume model below.
- **Per-user quotas (landed).** Blocks are accounted per uid with a ceiling
  (0 = unlimited), refused before a write allocates anything, and returned to
  the file's owner on delete. The ledger is derived from the tree at every
  mount and by `quota rebuild`, which is why it cannot drift.
- **Repair, and a console to run it from (landed).** `repair` walks every
  directory and compares what the tree claims with the allocation bitmap - leaks,
  blocks the bitmap had freed that a file still needs, blocks two entries claim,
  and entries that cannot be trusted - then `repair fix` rewrites the bitmap and
  the free count from what the tree actually holds, and `repair journal`
  discards a log that can never be replayed because its pre-image was torn. It
  borrows the bitmap as its own scratch and writes nothing until the walk is
  done, so a machine that dies mid-check leaves the volume untouched.
  `make check-repair` corrupts a copy of the image three ways, requires the
  check to find all three, requires the fix to survive a reboot, and then
  requires the offline checker to agree. This is the wave's corrupted-volume
  exit criterion as a gate.
- **Access control lists (landed).** Where a file carries one, its ACEs decide
  and the mode word is not consulted: named users and groups (`acl:u<id>`,
  `acl:g<id>`), a mask that narrows them, and `acl:o` for everyone else, stored
  one ACE per entry in the file's existing xattr block - so no on-disk
  structure changed and an ACL'd file is still a v3 file to an older reader.
  `acl set`/`acl show` place and inspect them from the shell; `acl_test` is the
  conformance suite: a named user writes a 0600 file the mode word makes
  read-only for them, the mask narrows that grant without touching the ACE, the
  owner is held to their own ACE, a stranger falls to `acl:o`, and root passes.
  Inheritance and group-class unions are the remaining ACL work.
- **Alternate data streams (landed).** A stream is a second content of a
  file, reached as `FILE:STREAM`, with its own size, blocks, permissions and
  xattrs; it is stored as a directory entry of type 6 named `FILE:STREAM`, so
  the path resolver, the journal, quotas, ACLs and repair all understand it
  without new on-disk structures. What the representation costs is paid at the
  three places that own the file: `dir` and readdir hide stream entries,
  deleting a file deletes its streams in the same transaction, and renaming it
  moves them - each stream rename nested under one commit, so the set moves or
  none of it does. `stream list|read|write|delete` is the shell surface, and
  the offline checker reports a stream whose base is gone. `make check-streams`
  writes, lists, reads, hides, deletes and renames streams, then requires
  `tools/fsck_hbfs.py` to call the volume it leaves behind consistent.
  Wildcard renames refuse a set that carries streams rather than rename entries
  under a scan they would invalidate; a stream takes the compression container
  like any other content, and streams on directories are the remaining stream
  work.
- **Compressed files (landed).** A file can be stored as 16 KiB chunks: a
  chunk table sits at the front of the file's own run and the chunks follow it,
  each with a CRC-32C over its plaintext and a flag saying whether it is
  LZ-encoded or kept whole, so a chunk that does not shrink costs nothing and
  incompressible data cannot grow a file. Reads decode transparently - the
  whole-file readers, the program loader and a descriptor's block read all go
  through one seam - and `compact`/`expand` move a file between the two
  layouts inside a journal transaction. A chunked file refuses in-place writes,
  a truncate, a defrag and a mapping, each because its blocks are chunks named
  by a table and none of those operations knows that. `make check-compress`
  compacts a text file and compares it byte for byte in a later boot, runs a 580 KB
  program (37 chunks) while it is compressed, expands it back, and requires
  `tools/fsck_hbfs.py` to decode every chunk with its own decoder and agree on
  every checksum. Twelve power-cut points through `compact` itself, killed with
  no clean shutdown at random moments, all recovered with the checker clean -
  the crash harness takes a shell command, so a command that rewrites a file's
  layout can be crash-tested like any program. Snapshots remain the other half
  of the wave's storage work, and a compressed file cannot yet be written in
  place or mapped: a `write` replaces it whole, which is what every whole-file
  writer does, and an `expand` is the way to a mapping.
- **Volume name and safe ejection (landed).** A volume carries a label - 32
  NUL-padded bytes at offset 80 of the superblock, which both formats leave
  free - so it is a property of the disk and not of the machine: `format` and
  populate.py write one, `label` reads and sets it (root only, journalled
  through the superblock, which is what stops a crash leaving half of two
  names), `volumes` shows it beside the partition table's own name for the
  volume, and `tools/fsck_hbfs.py` refuses a label with no terminator or a
  non-printable character. `eject` is the removable half of the volume model:
  it refuses while any page of the volume is still mapped, naming how many and
  the block the first holds, because the page cache keeps no dirty list and a
  mapped page is a write that has not happened; with nothing mapped it flushes
  the journal and the block cache and releases the storage device without
  disturbing a keyboard on the same controller. `make check-volume` proves the
  label survives a reboot, that `volumes` names it, and that `eject` flushes
  with a stick attached and refuses nothing it should not.
- **A kernel window, not a gap (landed).** The image had filled 4062 of the
  4062 sectors between the loader and the superblock, because the program window
  was pinned at 3 MB and everything above it was hand-placed. The layout moved
  instead of being nudged: a 4 MB kernel window (1-5 MB), `PROGRAM_BASE` at
  5 MB, the page tables, PMM bitmap and heap window each 2 MB higher,
  `KERNEL_RESERVED_END` unchanged at 8 MB, and the superblock at LBA 8481 - a
  fixed 256 sectors past the window rather than wherever the image happened to
  end. The on-disk cost is 512 blocks of a 523 179-block volume. This is the
  first thing the wave did *because* the previous one ran out of room, and it is
  a volume layout change: an image built before it is not readable after it.

**Exit:** 10 000 power-cut iterations clean; a quota and an access-control
conformance suite; a deliberately corrupted volume repaired from the recovery
console; an encrypted volume round-trips with its key held in the store.

Of those, the power-cut harness, the quota and ACL conformance suites, and the
corrupted-volume repair are in place; volume encryption and the VFS redirector
seam have since landed. Compression and snapshots are the wave's remaining
work, and `make crash-soak` with 10 000 iterations is a run that has not
happened.

### M4 · Configuration and services (9–13 weeks)

- **The Sett** — the hierarchical, transactional configuration store. Typed values,
  per-machine and per-user hives, atomic commit riding on the HBFS journal, import
  and export, rollback, and change notification. Kernel tunables, driver settings,
  service records and user preferences all live here; the environment-variable
  system becomes a view onto it.
- **The Watch** — the service manager. Service records in the Sett, start types,
  dependencies, failure and restart policy, control messages, and a service running
  as a named account.
- **Sessions and logon**: local accounts in the Sett, password hashing, per-user
  profiles, session 0 for services, and fast user switching.
- **Chronicle** — structured event log with sources, levels and retention, plus a
  viewer that filters by source and severity.
- A task scheduler with calendar triggers and missed-run policy.

**Exit:** a service starts before logon and survives a crash-loop policy; two users
are logged on at once with separate desktops; every kernel log line is a Chronicle
record, and the boot log is readable from the log viewer.

**As built.** Chronicle landed (source and severity filters, retention counted).
The Sett landed with typed values, a snapshot transaction, a generation and a
checksum, `/sett.bak` rollback, change notification, per-key descriptors, `SECRET`
values in the credential store, hives, export and import. The Watch landed with
records in the Sett, start types, dependencies and cycle refusal, and restart
recovery counted out to `FAILED`. Accounts, logon, lock/unlock, switch and the
session boundary landed; "separate desktops" means separate session namespaces,
not two composited desktops. A task scheduler landed with `boot`, `logon`,
`every:SECONDS` and `daily:HH:MM` triggers and a `skip`/`run` missed-run policy.
A service's recovery policy is stored and run, not assumed: three actions taken
in order (`restart`, `run:IMAGE`, `reboot`, `none`), a reset period that begins
a new streak once a service has run clean, and a start timeout that turns a
service which never appears into a counted failure with its action run. Passwords
are Argon2id (version 1.3, one lane, three passes, 1 MiB - RFC 9106's 64 MiB does
not fit a 128 MB guest), with a scheme number in the account record so a store
written by another function is refused and re-set rather than compared. A locked
session runs `unlock` and nothing else, so a logon screen that dies leaves the
session locked, and a hive that is damaged on disk is refused by name and offset
with the last known good generation loaded instead. A stop is a request the
service answers through its own record, and one that never answers is killed at
its stop timeout with the kill in the Chronicle. A profile is now the thing
section 3 describes rather than a name: the first logon makes `/Users/<name>`
with its five directories, `profile list` says which accounts have one, and
`profile delete` renames the tree aside, then removes it and the hive file entry
by entry so a delete that cannot finish never leaves a half-empty profile -
section 6 item 7 is gated by `make check-runtime`, which ends the case with
`repair` on a volume with no orphan block and no lost one. What is still open: a
hive is one image rather than section 5's cell format; a profile is created from
a fixed list of directories rather than copied from a default profile; and the
Sett's crash-at-every-phase and migration gates in CONFIG.md section 10 are not
built. CONFIG.md section 11 and SERVICES.md section 7 name the rest.

### M5 · Runtime and binaries (16–22 weeks) — **closed**

The largest single lift, and the one that makes "an operating system you develop
for" true.

- **PE/COFF loader** and shared libraries with a stable ABI: relocations, export
  and import tables, per-process module lists, load-time and run-time binding.
  The existing flat and ELF32 loaders stay, so all 295 NASM programs keep working.
- **Unicode end to end**: UTF-16 at the ABI boundary, UTF-8 internally, locale and
  time-zone data, collation, and a font stack that draws every code point the
  system decodes rather than the 5×7 VBE face. A *scalable* (outline) font is
  [MEDIA.md](spec/MEDIA.md)'s, as ABI.md §8 already says: what M5 owes is that
  no character renders as `?`, not a rasteriser.
- A C runtime: string, memory, formatted I/O, allocation, and the start-up code a
  normal C program expects.
- An interface-over-objects model — a COM-flavoured typed dispatch layer on top of
  the Warren — so applications call interfaces instead of raw syscall numbers.
- The POSIX personality stays as a compatibility layer for existing programs and
  future ports.

**Exit:** a multi-source application using shared libraries, a Unicode interface,
the Sett for configuration and Chronicle for logging is built with the SDK and
the in-OS toolchain, and runs.

**As built.** The image format and the loader that reads it
landed first, because everything else in the wave sits on them. HBX images
carry a 96-byte header, a section table, a CRC-32C checksum over the file, a
relocation table and an import table; `kernel/hbx.inc` refuses a writable and
executable section, a 64-bit image, a checksum that does not match, an image
that asks for a base it cannot be moved to, and an entry point outside an
executable section, each with its own message and status. An image built for
another base is relocated by the delta. `image_load_program` is now the one
door: the shell (root and `PATH`) and the service manager no longer each
compare the first dword against `ELF_MAGIC`, and the flat and ELF32 loaders
keep working unchanged. `tools/mkhbx.py` builds an image from a flat payload and
`make check-loader` proves every row of the refusal table. The ELF32 side is
now fed rather than only described: `ELF_PROGS` links the same sources and
runtime as a flat program and leaves the image as an executable, the loader
snapshots the program header table before copying a segment over it and takes
the entry point from `e_entry`, and three utilities (`elfinfo`, `elfcat`,
`elfwc`) ship that way. Feeding it found the two path bugs that had stopped a
program reading a file in a subdirectory: `SYS_FREAD` now splits the path the
way every other file call does, and entering a subdirectory moves the
directory cache tag along with the bytes it read. Then the image moved out of the
fixed window: a relocatable HBX image is mapped into the task's own directory at
a random 64 KB-aligned address in the demand zone, one page per section
protection, with every absolute address in its relocation table fixed by the
delta - and an image without relocations is copied into the window as before
and says that it could not be randomised. Because a mapped image can sit above
the RAM ceiling, the syscall pointer checks now accept any address the task
actually maps instead of only the window, which is the contract mmap'd buffers
already needed. `make check-loader` launches the aslr fixture six times and
requires six different bases. The C runtime is now a runtime and not just
`string.c`: `c/rt/stdio.c` renders `d i u x X s c p %%` with the `-` and `0`
flags and a width, to a buffer (`snprintf`, C99 truncation and return value) or
to the console; `c/rt/malloc.c` is a first-fit heap over `SYS_SBRK` that splits,
frees sorted by address and coalesces with both neighbours; and `c/rt/start.c`
is the start-up, so a C program is one `main` and its return value is the exit
status. `c/prog/crtdemo.c` is the gate - checks over the conversions,
`snprintf`'s contract, 64 blocks allocated and freed out of order, one
allocation of the whole arena afterwards to prove the coalescing, and
`calloc`/`realloc` - and `make check-runtime` runs it. `main` still takes no
arguments: the kernel hands a program a flattened command line, not EXEC.md
section 5.1's block, so `argv` waits for it. Then the second format: the loader
sees `MZ` and hands the file to `kernel/pe.inc`, which accepts the PE32 subset
EXEC.md section 2.1 writes down - i386, magic 0x010B, the window's base, no
imports, sections inside the file and the image, none writable and executable,
an entry point inside code - and refuses everything else in its own words,
because a program built for the parity target's ecosystem is what this wave is
for. Loading is a copy, so the file is staged at the top of the window, the
image area is cleared and the sections are copied down; copying in place would
only work while raw data sat above the whole image, which no linker guarantees.
`tools/mkpe.py` carries one keyword per rule, `tools/pe_fixtures.py` is the
refusal table written as code, and `make check-loader` proves the image that
loads and all seven refusals. Populating the image with seven more fixtures
then filled `/bin` - 224 entries - and dropped programs silently, which is the
ceiling docs/spec/README.md's S1 named, so a subdirectory is now the size of
the root: 455 entries, one documented number instead of two. Then dependencies
became real: `kernel/lib.inc` resolves a program's imports against a shared
library's export table, patches the resolved addresses into the program's IAT
while the file is still in the window, and maps each library into the task's
own address space once the task exists - so `hbx_dll` prints `LIB_HELLO` from
code that lives in another image, at an address no process shares. A library is
an HBX image marked `dll` and installed in `/lib`; unloading, versioning,
delay loading, a library that imports, and per-module TLS are the parts of
section 6 that are still missing. Text then became Unicode end to end in the
way this machine can hold it: UTF-8 is decoded, not passed through; each code
point is mapped to the glyph the CP437 font actually has, so Latin-1, Greek,
box drawing, arrows and the arithmetic signs draw in both the console and the
desktop - which now loads the adapter's 8x16 ROM font so it has 256 glyphs
instead of 95 - and the UTF-16 half of ABI.md section 8 is a real conversion in
`c/rt/utf.c`, surrogate pairs and all, with `unicodedemo` checking the
crossings. Thread-local storage closed the loader story: a module that declares
a TLS directory - the program is module 0, each library the next - gets a block
of its own in every task, copied from its template and mapped into the task's
own address space, with the index assigned at load and the base written at
launch. `hbx_tls` runs twice and has to see the template both times, and the
library's own counter starts at one. The POSIX personality then got the half it
was missing: the calls existed, and nothing could call them by name. `c/rt/posix.c`
is a shim over syscalls the kernel already had, with ten headers in `c/include/`
and `c/prog/posixtest.c` as the gate - a program that includes no `<mv/...>`
header at all and opens, writes, seeks, stats, duplicates, unlinks, forks, waits
and reads `errno`, `getpid`, `signal`, `setlocale` and `ctype`. Writing it was
the cheapest way to find out what those calls did when a program made them, and
five things did not survive: descriptors and PIDs were handed out from 0 (a file
took `stdin`'s number, and a child in slot 0 gave its parent a `fork` return of
0), `getpid` answered with whichever task sat lowest in the table, `dup` copied a
descriptor table nobody used, `/tmp` created names it was only asked to read and
its stat was a stub, and `access` did not know other mounts existed. All five now
behave, and `docs/POSIX.md` writes down what conforms, what does not and why -
including that nothing here has run a POSIX conformance suite, so the word
"compatible" is still the document's opinion rather than a test result. Then the
interfaces and the exit criterion landed: `SYS_LOG_WRITE` and `SYS_SETT`, the
QueryInterface-shaped objects in `c/include/mv/iface.h`, and `mvapp`, two source
files built by the SDK's link step. Threads came next and closed the TLS story:
`SYS_CLONE` makes a second task in the calling process - the same address space,
descriptors, handles and vm areas, with a kernel stack, a user stack and
thread-local blocks of its own - and `SYS_THREAD_EXIT` ends one without ending
the process, so `mv_thread_create`/`mv_thread_join` are the C shape of a
function call. The base slot a module reads its TLS block from lives in the
shared image, so each thread's value is installed by the scheduler on every
switch; `hbx_thread` proves two threads of one image see different blocks across
eight switches, and `threadtest` runs four threads over one heap plus 64
create/join cycles that would exhaust the task slots or the mapping records if
either leaked. The last two items closed the wave: `unicode_to_glyph` covers the
typographic punctuation the tree writes (an em dash drew `?` for as long as the
map lacked it, invisible to any test that reads COM1) and `font_selftest` walks
those code points at boot; `SYS_THREAD_DETACH`/`SETPRIO`/`CANCEL`/`SYS_TESTCANCEL`
give a thread the rest of what a program can ask of it, with cancellation defined
as a request the thread observes rather than an interruption from outside.

### M6 · Shell and user experience (12–16 weeks)

- Burrows grows into an Explorer-class shell: a file manager with copy, move,
  rename and delete over the access-controlled filesystem; a shell namespace
  (This Machine, Network, Control Panel); a start-menu analogue; a taskbar with a
  window list and a tray; shortcuts and file associations; drag and drop between
  applications; multi-selection; property sheets.
- **Control Panel**: device manager, users and groups, services, display, sound,
  network, date and time, add/remove programs.
- **Den** — the management console host, with snap-in-style tools (services, event
  viewer, disk management, users, performance, device manager) in one window.
- **`hbpkg` grows up** into an installer with dependency resolution, install
  scripts, file and association registration, repair, rollback, digital
  signatures, and per-machine versus per-user installs.
- **Printing**: a spooler service, queue management, cancellation, and a page
  description abstraction with at least one real output path.

**Exit:** install an application from a package, see it in the start menu with an
icon and an association, print a document to a cancellable queue, then uninstall
it leaving no files and no settings behind.

**Exit: met.** `pkg install` takes a local path as well as a URL, keeps the
source's file type, and registers the program by writing a link file into
`/Burrows`; the Burrows Launcher reads those links at startup, so an
application installed while the machine runs appears with an icon row
(`tests/test_runtime.sh`: "an installed package appears in the start menu").
`assoc` registers a per-user handler in the Sett and `open` runs it; `print`,
`printcancel` and `printflush` queue, cancel and complete jobs; and uninstalling
the package, its association and the profile leaves `repair` reporting
`REPAIR_CLEAN` ("installed software is associated printed and uninstalled").

### M7 · Networking, the 2000 kind (13–18 weeks)

- **File and printer sharing** over a protocol of our own on TCP: share permissions
  layered on file access control, browsing, and mapped shares in the shell
  namespace.
- Network logon against another Mellivora machine, in a workgroup model.
- A remote procedure call mechanism for remote service and configuration control —
  the foundation of remote administration.
- A firewall with a rules interface. Connection sharing, dial-up and VPN were
  planned here and are now out of scope: this is a self-contained OS, and its
  networking ends at the wire it is given — see the note under the M7 open
  list.
- Remote console and remote management: service control, configuration, event log
  and file transfer against a workstation, without a graphical session.

**Exit:** two virtual machines — share a folder, mount it on the other, log on
across the network, print to a shared printer, and restart a service on the other
machine from Den.

**Exit: met on two machines.** `make check-twovm` boots two QEMU guests on one
virtual Ethernet segment — the profile's RTL8139 on a TCP socket netdev pair,
distinct MACs, and a `-snapshot` overlay per machine so both can run from one
image without seeing each other's local writes — and drives both over their own
serial consoles (`tools/qemu_twonode.py`). Machine A exports `docs` read-write
and `ro` read-only; machine B gets `10.0.0.2`, browses A with `net view`, mounts
`docs` as `/mnt`, reads a file through it, streams a local file back with
`net put`, and is refused by the read-only export and by a wrong secret. It then
proves a password against A's account store with `net logon`, starts a service on
A with `net svc` ("Service pt is running"), and queues a document into A's
spooler with `net print`; A reads the file B wrote, lists it, and shows
`Spool-btest.txt` in `printq`. The loopback cases in `tests/test_runtime.sh`
stay: they cover the protocol's corners (streaming, refusals, the redirector)
without paying for two boots.

The remote control surface is the shell's `net` command rather than Den's
services snap-in, which is the milestone's remaining UI work.

**The case that looked like a KDF defect was a stale selftest vector.** Cutting
`A2_M_KIB` from 1 MiB to 64 KiB during M7 was blamed on a fill loop that stopped
advancing, but the loop was never wrong: the third vector in `argon2_selftest`
still carried the 1 MiB tag while the constant made it hash at 64 KiB, so every
boot printed `ARGON2_SELFTEST_FAIL`, and the old cost under emulation took long
enough to look like a hang. The selftest now picks the vector that matches the
compiled parameters, and the build refuses a cost with no vector, so the same
mismatch cannot ship again. `A2_M_KIB` stays at 64 KiB as a choice, not a
workaround.

**The tick was up to a hundred times too fast, on some boots.** Wiring two
machines up exposed what loopback could not: session deadlines, service stop
timeouts and every sleep are counted in ticks, and the local APIC timer's
reload value came from a measurement that read the PIT counter across a reload.
The PIT reloads every ten milliseconds, so that difference was not elapsed time
- it was whichever of ~0 or ~65535 the arithmetic landed on - and the same
machine calibrated 1x, 2x or 100x depending on when the boot read it. Two more
things were wrong in the same measurement: it started the APIC timer *after*
taking its reference, adding the cost of starting the clock to every result,
and it read the counter in square-wave mode, where the value steps by two, so
a period came out half its true length. The measurement now runs in
rate-generator mode, takes its reference immediately before the timer starts,
and measures between two reload edges, which marks a period exactly. A guest
clock checked against the host is within a couple of percent; before the fix a ten-second boot
reported 1384 seconds.

**Two more defects the second machine found.** `net_send_frame` zeroed EAX -
the transmit descriptor index - while padding a frame to the 60-byte Ethernet
minimum, so the address register written afterwards always selected buffer 0
and every padded frame after the first (ARP, a TCP SYN or ACK, a short ping)
went out as whatever the previous short frame had left there. The share client
also closed its session by marking the socket closed locally instead of
closing the connection, so the server never saw a FIN and held one of its four
session slots until the 60-second idle deadline; a handful of finished
commands then locked out the next connection. Both are fixed, and the
redirector's directory listing now refuses a reply whose shape is not a
listing and reopens the session once, because "cannot list" must not be
reported as "nothing here".

**RPC: two interfaces, and the reason a service kill took the machine with it.**
A call is a 28-byte request frame and an 8-byte reply frame carried inside a
share message after the handshake, so the dispatcher never learns which
transport delivered it: `(uuid, major.minor, opnum)` is matched against a
table, and the reply's status tells a caller "the wire was down" from "the
server said no". `mv.rpc/config` reads and writes a Sett value on the other
machine and `mv.rpc/eventlog` counts and reads its boot log, as `net cfg` and
`net log`. The bindings are written to `\RpcControl` at boot, so a client reads
a version out of Sett instead of assuming one, and a call that arrives before
the handshake finishes is dropped rather than answered.

Building it found a defect older than M7's networking: the SCM stopped a service
that ignored `STOP` with `call sys_kill`. Every syscall handler in this kernel
ends in `iretd`, which returns from a frame only `int 0x80` builds; called
directly it popped the caller's stack as EIP, CS and EFLAGS. The two-machine
plan had been passing by luck - the 30-second stop deadline landed after its
last step - and the moment the plan grew long enough for the kill to land while
something was still watching, machine A stopped answering with the screen
garbled. Both call sites go through `int 0x80` now, and the plan's later steps
are what hold them to it.

The test harness had a matching hole: `run_case` split an `--expect` list on
`|` but passed a `--reject` list through whole, so a case whose reject text was
`CPU EXCEPTION|PAGE FAULT` was searching for that literal string and rejecting
nothing - the fault check included. 151 reject lists were inert. They are split
now, which is why the runtime matrix below is stricter than it was.

**The firewall turned on and the share stopped answering, which is the point.**
Inbound is denied by default now, so the two-machine plan had to be taught what
an administrator does on a real workstation: B is refused, A counts the refusal
and writes `fw: blocked inbound from 10.0.0.2` to its boot log once, `net fw add
tcp 4450` admits the next attempt, and deleting the rule refuses it again. Four
things make that honest rather than a screen: loopback is exempt before the
rules are consulted and is not counted, so every loopback test in the suite
still means what it did; a reply to a datagram this machine sent is admitted by
state, so DHCP and DNS need no rule and a flow sent to a broadcast accepts the
answer from a server whose address it did not know; the decision is taken in
`tcp_handle`'s listen state before a child socket is allocated and in
`udp_handle` before DHCP, DNS or a socket sees the datagram; and a blocked
packet is dropped rather than reset, because a reset would tell a scanner which
ports exist.

Rules are Sett values under `\Registry\Machine\System\Firewall`, written
back as normalized text so what reloads is what was accepted, and edited with
`net fw` - `list`, `add`, `del`, `on`, `off`. What is not there: no
application-image match, no per-package port declaration with the approval
prompt bullet four asks for, and no rule that denies outbound. Those are named
in NETWORK.md section 7.1 rather than implied.

**The remote console, and the question of who runs the command.** The transport
was the easy half: the share connection is already authenticated, so a `CONSOLE`
request costs nothing. The problem was context. The share server lives in the
network receive path, which is no place to execute a shell command, and the
shell is the only thing on this machine that can execute one - it owns
`line_buffer`, the job table and the prompt. So the server runs nothing: it
queues the line and answers later, the shell takes it when it is idle at an
empty prompt, runs it through its own dispatch, and the output is captured in
`vga_putchar` instead of painted. The reply goes out on the next pass of the
idle loop. `net console HOST SHARE SECRET [COMMAND]` is one shot and
`net console HOST SHARE SECRET` is a session until an empty line or `exit`.

Two things that shaped it. The capture had to be its own flag rather than the
shell's `>` buffer, because the shell resets that buffer after every command -
the first attempt answered every request with an empty string and printed the
command's output on the local console instead. And a console pointed at this
machine's own address is refused by name: the console is what would execute the
command and what would be waiting for the answer, so it would deadlock until the
client's deadline, which is a hang pretending to be a timeout.

What it is not, and NETWORK.md section 6.1 records: not a second terminal (the
command runs in the console's context, so `cd` is shared), no pty, and no
`SE_SESSION_CROSS` - the shell is SYSTEM, so whoever holds the share secret can
do what SYSTEM can. Per-capability privilege on the management calls is the next
thing to close, not a detail to leave implied.

**Privilege turned out to be a property of the export, not of the session.** The
transport can prove one thing - that the client knows a share's secret - and
section 6 asks for administration to be refused without a privilege. So the
privilege is the share's `/admin` flag: a folder shared for reading is a folder,
and every management request on it is refused by name and recorded
(`share: refused an administration request`), while `/admin /rw` on the same
directory opens service control, configuration, the event log and the console.
The client is told why - an RPC call answers status 5, the console and `net svc`
answer in words - because a refusal that looks like a dropped connection teaches
the wrong thing. What the flag cannot do is name a *user*: once it is present the
console and the RPC handlers run as SYSTEM. The flag buys SYSTEM, not the
client's own rights, and that is the honest remaining gap.

**The fuzz gate is built, and it is not green.** NETWORK.md section 9 item 2
wants 1 000 000 malformed frames through a running machine. `packet_fuzz.py` had
been generating them with nowhere to send them - a generator, not a gate. The
missing half is `tools/qemu_fuzznode.py`: one guest on a socket netdev, the
frames written down the wire the way QEMU frames them (a four-byte big-endian
length, which is not the little-endian one the tool's file format uses), and
then three questions asked of the guest through its own console - `ifconfig`,
`ping 127.0.0.1` (4/4), and a loopback share mounted and listed. The fault log
answers "did not panic"; those three answer "has not lost the connection".
`make check-fuzz` is the whole of it, and it is deliberately not part of
`make check` while it fails.

It fails, and where it fails is worth writing down. With the generators mixed
the wire stops moving after 12 000-15 000 frames. Nothing faults. The machine is
not wedged in its interrupt handler - it is idle, and its console answers again
once nothing more arrives. What is left behind is a receive ring whose read and
write pointers are a few bytes apart, and that is the one state the emulated NIC
will neither deliver into (it wants a whole 1514-byte frame's worth of room
free, or a wholly empty ring) nor interrupt about, since it does not interrupt
for entries already in the ring. A retry from the timer - the obvious answer -
starved the machine, because the drain was then reading a ring it could not
parse a hundred times a second, and `check-twovm` said so loudly. That the timer
could starve the shell is itself the next thing to understand: the retry has to
be conditional on the ring making progress, not on the ring being non-empty.

Three defects the storm did find are fixed, and each was reached by a frame a
driver should shrug at. **A read pointer outside the ring:** the offset handed
to the NIC is `offset - 16`, and a skipped frame ending inside the first sixteen
bytes made that subtraction negative inside a sixteen-bit mask, so the machine
told the NIC to read next from `0xFFFx` - outside an 8 KB ring - after which it
delivered nothing. **A length used to step over a frame that had already been
rejected:** a header the driver had just decided it could not trust was still
used to advance the pointer, which threw it thousands of bytes outside the ring;
the driver now resynchronises to the start of the ring instead, which costs a lap
of stale entries and nothing else. **An unbounded loop inside the interrupt
handler:** the ring's exit test is a bit in the NIC, so a ring state the driver
could not read left the handler spinning with interrupts off - a machine that
stops answering with no fault to show for it. The drain is now its own routine
with a bound. And the card's interrupt line is now level-triggered, which is
what a PCI line is: as an edge, an assertion that lands while the handler is
still running is lost, and a card with an empty queue has no reason to assert
again.

A storm that costs a minute to run found three defects and one open question
that no interactive test would have, which is the argument for the item existing.

Still open in M7, and named so the milestone is not read as closed:

- **A per-user privilege model.** `/admin` is per share, and the shell behind it
  is SYSTEM, so the flag buys SYSTEM rather than the client's own rights.
  Impersonation is parsed and bounded but carries no authority, because the
  transport authenticates a secret and not a user.
- **`packet_fuzz` at 1 000 000 frames.** The gate is built (`make check-fuzz`)
  and fails at 12 000-21 000 mixed frames, with the receive ring's pointers
  apart and no interrupt coming. Each generator on its own now survives 30 000,
  so it is the mix; NETWORK.md section 9 item 2 records the state, and the five
  defects found on the way are fixed.
- **The conformance suites the spec names:** `net_test` (fragmentation and
  reassembly are not in the stack at all) and `share_test` at 1 000 files with
  leases and reconnect. `rpc_test`'s ground is covered by the two-machine plan
  and the loopback cases.
- **Den's services snap-in**, so the exit line's "from Den" is met by the
  desktop rather than by `net svc`.

Out of scope by decision, not by omission: **connection sharing (NAT), dial-up
(PPP) and VPN tunnels.** Bullet four named them; they are dropped because this
stays a self-contained OS. Its stack speaks TCP/IP on the wire it is given - to
itself on loopback, and to another Mellivora machine on a shared segment - and
it does not become a router, a dial-up peer, or a tunnel endpoint. DHCP and DNS
stay, because they are how a machine learns its own address and resolves names
on whatever wire it finds itself on.

### M8 · Devices and media (16–22 weeks)

- **USB**: host controller stack (UHCI and OHCI, then EHCI), hub enumeration, HID
  keyboard, pointer and gamepad, mass storage, and audio class. This is what makes
  `-device usb-*` real and puts the platform in the right decade.
- Audio mixing so several programs play at once, with per-application volume.
- A media framework: container and stream abstraction, decoders, synchronised
  playback, and capture.
- Two-dimensional acceleration behind an abstraction the compositor can use, and
  enough three-dimensional support for the era's screensavers and demos.
- Multiple monitors, display mode management and DPI.

**Exit:** plug in a USB stick and copy a file, unplug it safely; three programs
play audio simultaneously; a video plays in a window at 30 frames per second.

**Started: the host controller is found and read.** `kernel/usb.inc` registers a
driver against PCI class `0x0C03` with programming interface 0, which is a UHCI
controller - the one a PC of this vintage has, and the one QEMU's default
machine provides. It claims the device through the existing driver model, takes
its I/O base out of BAR4 (refusing an unconfigured one, because I/O to port 0
corrupts the legacy space), resets it, and reads the two root-hub port status
registers. `make check-usb` asserts the three things that claim amounts to: the
controller is found, the ports read as empty on a machine with nothing plugged
in, and a device shows up as connected when one is attached (`--device usb-kbd`,
which is what `tools/qemu_drive.py --device` was added for). The driver is
reported by `drivers` like any other.

A frame list followed: one queue head, 1024 frame pointers, the controller
started on it. Starting is the assertion - a controller handed a schedule whose
pointers are wrong halts instead of running - so `make check-usb` gained a case
for it, and the report is now four: the controller is found, it runs, the ports
read as empty, and a device shows up when one is attached.

Then the two UHCI details that cost the most to find, both recorded in the file
because both look like something else. **Port Enable is written by software, not
by the hardware**: clearing the reset bit is not enough, and a port nobody
enabled answers nothing - the driver spent an hour resetting a port that was
never listening. And **the queue head's element link needs the depth-first bit**:
without it the controller executes the first descriptor of the chain and leaves
by the horizontal link, so a three-stage control transfer does the setup and
never the data.

The transfer that would not complete was two defects, each invisible until the
other was fixed. **`USB_DEV_ADDRESS` is an immediate, not a memory operand**: the
enumeration path read the next address with `movzx eax, byte [USB_DEV_ADDRESS]`,
which loads the byte at address 1 - the top of the real-mode IVT, 0xFF - so the
descriptor read after the address was assigned went to device 127, endpoint 1,
which nothing answered; the transfer timed out and the boot stopped. The boot
log's status dump also printed each transfer descriptor's status word as four
hex digits, which is the whole field below bit 15 and therefore none of the
bits that matter; it now prints all 32.

Enumeration is now called from the boot path and works: the port is reset, the
first eight bytes of the device descriptor are read at address zero to learn the
packet size, the device is given an address, and the full descriptor is read at
that address. `make check-usb` asserts it - six cases now: the controller is
found, it runs, the ports read as empty, a device shows up when one is attached,
it is enumerated, and its descriptors are read and logged.

After the device descriptor comes the configuration descriptor, and with it the
move from addressed to configured. Nine bytes are read first to learn the total
length and the interface count, then the whole descriptor, then
SET_CONFIGURATION(1). `make check-usb` gained two cases for it - eight now: the
configuration is read and the device is configured. A device in the configured
state is the starting point for both HID and mass storage, which is where the
milestone goes next.

The first class driver begins with what a boot keyboard needs before a single
key is read. The configuration descriptor is walked - descriptor by descriptor,
each skipped by its own length - for the interface that is HID, boot subclass,
keyboard protocol, and then for the interrupt IN endpoint under it; the
endpoint's address and packet size are recorded. The two class requests that
put a boot keyboard in boot protocol follow, SET_PROTOCOL(0) and SET_IDLE(1) -
a 4 ms idle, so the keyboard reports its state before any key is pressed, which
is also what lets the pipe be proven with a report rather than with a key. An
interrupt IN descriptor is then hung below the queue head; the controller
retries it each frame while the device NAKs, and it completes with the first
report. The boot log gains `usb: hid endpoint 81, 0008 bytes, ready` and
`usb: hid report read`. `make check-usb` is ten cases now. A mouse - HID but
not a keyboard - walks past silently, which is correct: it is not this driver's
job yet. Turning the reports into keys is the next step.

The reports become keys next. Each report is diffed against the previous one -
the modifier byte and the six keycodes - so every key that went down or up is
one synthetic scancode, and the six navigation keys that need an E0 prefix are
handled apart. The HID usage is translated to a PS/2 set-1 scancode with a
102-entry table and fed into the same routine the PS/2 keyboard interrupt uses,
so shift, ctrl, alt and the shell's Ctrl+C all fall out for free. The polling
lives in the timer interrupt: it arms the descriptor and checks it without
waiting, draining completed reports and re-arming, so a tick never blocks.
`make check-usb` is eleven cases now - the last types `help` through the
keyboard and runs it.

Mass storage is the class the exit line actually needs, and it starts with the
same descriptor walk with a different match: interface class 8, subclass SCSI
transparent, protocol bulk-only, and the bulk IN and OUT endpoints under it.
The bulk-only transport then does what the specification prescribes before it
will answer a command - a class reset, INQUIRY, READ CAPACITY, and one READ(10)
of block zero - so the log names the device it found (`QEMU HARDDISK`), its size
in blocks, and a signature the harness wrote into the image and the device gave
back. Two details cost thought. **The data toggle belongs to the driver, not to
the controller**: a bulk endpoint alternates DATA0 and DATA1 across transfers,
the UHCI schedule does not flip it, and a descriptor handed the wrong one has
its packet ignored, so the toggle is kept per direction and flipped only when a
transfer completes - not when it is NAKed and retried. And **the class drivers
run in a fixed order**, because one queue head holds one descriptor chain: the
mass-storage probe moves bulk data before the HID driver hangs an interrupt
descriptor on it. `make check-usb` is seventeen cases; `tools/qemu_drive.py
--usb-disk` attaches a raw image as a `usb-storage` device for the last six.

The device does not stop being useful when the boot probe ends. `disk` reports
the stick after the ATA disk - its size in blocks and block length - and then
reads block zero again, through the same bulk path, with the machine fully up;
the block's first eight bytes reach the screen in hex. That read is the one the
gate asserts, because the harness wrote the bytes and only the device can give
them back. `usb_msc_read_block` is the block-device entry point in miniature:
one command block at a time, tagged, with the data residue checked.

Plugging a stick in after the machine is up is the same state machine run
again, so `disk` scans the root ports before it reports: a device that appeared
is enumerated and its class driver started, and one that left has its state
taken with it - the HID poll is disarmed before the descriptor that refers to
it can be checked again - and the removal is logged. The first hot-plugged
stick found a real SCSI behaviour the boot case cannot: READ CAPACITY came back
CHECK CONDITION with UNIT ATTENTION and an eight-byte residue, because a device
that has just appeared reports the change once, on the first command that is not
INQUIRY. The driver now reads REQUEST SENSE and repeats the command once. The
sense data is kept at DEBUG, and a rejected status wrapper is logged with its
signature, tag, residue and status, so a failure that is *not* unit attention
still says which of the four checks refused it. `make check-usb` is twenty
cases: the harness plugs a backend in with the monitor's `device_add` and pulls
a boot-attached stick out with `device_del`, so both directions run against a
machine that is already up.

What it is not, in full: there is no hub beyond the root ports, the keypad and
the GUI keys are not mapped, and the driver holds one device at a time - a stick
plugged in beside a working keyboard is left alone until the keyboard leaves.
The stick is read but not yet mounted; no filesystem sits on it, so the exit
line's copy-a-file is still ahead. The block device is not registered with the
VFS, writes have no command of their own yet (the transport can carry one), and
ejection is not implemented. The order from here is unchanged: make the
mass-storage device a block device that HBFS can mount, which is what makes the
exit line real.

### M9 · Reliability and management (9–13 weeks)

- Crash dumps for kernel and user faults, with a tool that can read them, and a
  boot menu with safe mode and last-known-good.
- A recovery console that can repair a filesystem, restore a snapshot and reset a
  service or driver configuration.
- The fault-injection framework generalised into a driver verifier.
- **Pulse** — performance counters with a sysmon-class viewer, feeding `top` and
  `sysmon`, and exportable for scripts.
- Backup and restore, restore points on top of HBFS snapshots, and disk health.
- Instrumentation for scripting, so administration can be automated.

**Exit:** a forced kernel fault writes a dump that the analysis tool explains;
last-known-good rolls back a bad driver; a restore point returns the machine to an
earlier state including its settings.

### M10 · Ecosystem and release (7–11 weeks)

- An SDK: headers, import libraries, samples, a driver kit, and a getting-started
  path that does not require reading the kernel.
- The documentation set completed and kept honest by `make check-docs`.
- A package repository with signing, mirrors and metadata.
- Release engineering: reproducible installers, checksums, release notes.
- QEMU and KVM guest integration: virtio throughout, clipboard sharing, display
  resize, shared folders.

**Exit:** a new developer builds and installs a working application from the SDK
alone, and a release ISO is reproducible from a clean checkout.

## Out of scope

Named explicitly so the plan does not drift into them:

- Win32 binary compatibility, and the SMB, MSI, NTFS and registry wire formats.
- Domain controllers, directory services, Terminal Services.
- DirectX parity, Internet Explorer, Microsoft Office.
- x86-64 support.
- Real-hardware certification beyond QEMU-emulated devices.

## Risks

| Risk | Mitigation |
| --- | --- |
| The C boundary becomes two systems in one tree | One calling convention, one ABI document, one build entry point per module, and a lint gate that rejects a module mixing the two without an entry stub |
| Objects and security are retrofitted too late | They are wave M1, before any feature that would have to be reworked for them |
| SMP destabilises the tree | AP start-up is a separate gate; the machine stays bootable single-CPU, and the 24-hour soak is the entry condition for M3 |
| ~~`paging_map_page` allocates a page table from the frame allocator, and above 128 MB of RAM a mapping taken at boot can leave the paging tests faulting in data~~ **Closed.** The boot identity map now covers every frame the allocator owns (all of RAM up to 512 MB), so kernel code never faults on allocator bookkeeping; the fault handler no longer allocates on the kernel's behalf. `vmtest2` is required at 128, 192, 256 and 512 MB, and a 2-CPU 512 MB machine is part of the default suite | Was: M2 to own the fix |
| Driver breadth exceeds the budget | QEMU-first hardware list; a driver is only in scope if a test can exercise it |
| The scorecard is gamed by partial credit | Rows move only when their named test passes in CI |
| Printing and USB are larger than they look | Both are scoped to one working path first, breadth afterwards |

## First 90 days

1. **Weeks 1–3** — the scorecard harness and the true baseline; the ABI
   versioning table; `docs/spec/OBJECTS.md` and `docs/spec/ABI.md` reviewed.
2. **Weeks 4–6** — the C toolchain probe, the freestanding runtime, a C-built
   kernel module and a C-built user program in the build.
3. **Weeks 7–9** — per-task FPU and SSE state, with a floating-point workload
   across a context switch added to `make check`.
4. **Weeks 10–13** — the Warren, part 1: object types, handle tables and name
   lookup, with the current syscalls re-expressed over them.

## Specs

The normative documents. They describe the system being built, not the tree —
where a document and the tree disagree, the tree is the bug unless the section
is marked **as built**. [docs/spec/README.md](spec/README.md) holds the index,
the status vocabulary and the table of ceilings each wave removes.

| Document | Governs | Normative from |
| --- | --- | --- |
| [ABI](spec/ABI.md) | Calling convention, types, structure layout and versioning, status codes, handles, names, syscall encoding | M0 (C boundary), M1 (v2) |
| [OBJECTS](spec/OBJECTS.md) | The object namespace: types, handles, rights, lifetime, waiting, sessions | M1 |
| [SECURITY](spec/SECURITY.md) | Threat model, tokens, privileges, integrity, ACLs, the access check, audit | M1 |
| [VMM](spec/VMM.md) | Address space, regions, page tables, PFN database, faults, sections | M1 |
| [IPC](spec/IPC.md) | Ports, messages, completion ports, sections, pipes, deadlock rules | M1 (sections), M5 (ports) |
| [SCHED](spec/SCHED.md) | Processors, priorities, dispatch, timers, jobs, accounting | M2 |
| [PLATFORM](spec/PLATFORM.md) | ACPI, interrupts, clocks, SMP, power, caching, CPU features | M2 |
| [DRIVERS](spec/DRIVERS.md) | Modules, device tree, resources, IRPs, DMA, power, PnP | M2 |
| [STORAGE](spec/STORAGE.md) | HBFS v5: format, volumes, transactions, cache, quotas, snapshots | M3 |
| [CONFIG](spec/CONFIG.md) | The Sett: keys, types, transactions, notifications, hives | M4 |
| [SERVICES](spec/SERVICES.md) | Services, dependencies, recovery, logon, sessions, profiles | M4 |
| [EXEC](spec/EXEC.md) | Images, loader, libraries, TLS, ASLR, process creation | M5 |
| [SHELL](spec/SHELL.md) | Shell namespace, associations, drag and drop, clipboard, notifications | M6 |
| [NETWORK](spec/NETWORK.md) | Sockets, stack contracts, shares, RPC, remote administration, firewall | M7 |
| [MEDIA](spec/MEDIA.md) | Audio graph, video, USB, printing | M8 |
| [RELIABILITY](spec/RELIABILITY.md) | Dumps, safe mode, recovery console, restore points, tracing, gates | M9 |
| [ECOSYSTEM](spec/ECOSYSTEM.md) | Packages, registration, trust, SDK, ABI and release policy | M10 |

A wave does not close until its documents' **Conformance** sections pass. The
gates in [RELIABILITY](spec/RELIABILITY.md#9-the-gates) are the same numbers the
Gate table above uses; that document is the authority if the two ever differ.
