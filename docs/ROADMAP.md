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
|---|---|---|
| D1 | One 32-bit x86 target | The kernel, drivers and syscall ABI are 32-bit protected mode. There is one build and one image; PAE is the ceiling-raise if more than 4 GB is ever needed. |
| D2 | NASM for the core, freestanding 32-bit C for new subsystems | The boot path, entry stubs, context switch and the existing modules are assembly and stay assembly. The executive, driver model, networking services and shell facilities in M1–M9 are too large to write by hand in asm at a maintainable rate. One calling convention (cdecl, packed structs, documented in `docs/spec/ABI.md`) is fixed at the boundary between the two. |
| D3 | Everything is an object reachable through a handle | Resource access today is by raw number. Named, reference-counted, typeless objects with per-handle rights are what make sharing, waiting on, securing and remotely managing a resource possible at all. |
| D4 | Security is enforced, not advisory | `chmod`/`chown` store an owner and a mode, but `open` does not check them. A 2000-class system denies access, audits the denial, and lets an administrator explain why. |
| D5 | Parity through our own protocols | SMB, MSI, NTFS and the registry are each a multi-year compatibility project. A working equivalent that is *ours* satisfies the user story; a half-finished compatible one satisfies nobody. |
| D6 | Monolithic kernel with a loadable driver model | Best performance per unit of effort, and the existing drivers already live in the kernel. Loadable modules give PnP and third-party drivers without moving to a microkernel. |
| D7 | The compositor stays in the kernel for this plan | The surface IPC and damage-rect compositor already work. A user-space display server is a later, separately gated decision. |
| D8 | ACPI-first hardware model, QEMU-first testing | Every hardware path in this plan is one QEMU can emulate, so the test suite stays honest. Real-hardware coverage is a separate, later campaign. |

## The parity scorecard

The scorecard is the definition of done for the whole plan. Each row is a job a
person does, not an API that exists.

| # | User job | Pass criteria | Today |
|---|---|---|---|
| 1 | Log on | Logon screen, per-user profile and desktop, switch user without reboot | No |
| 2 | Manage users | Accounts, groups, passwords, quotas, permissions edited from a GUI | Partial — `uid`/`gid` and `chmod`/`chown` only |
| 3 | Run services | Services start before logon, have dependencies, restart on failure | No |
| 4 | Configure the machine | One hierarchical transactional store for kernel, service and user settings | No — env vars and compiled-in constants |
| 5 | Install software | Install/uninstall with dependencies, registration and clean removal | Partial — `hbpkg` archives, no registration or removal |
| 6 | Use the desktop | Multi-app shell, taskbar, file manager, drag-drop, clipboard, associations | Partial — Burrows multi-app desktop, no shell namespace |
| 7 | Multitask hard | Preemptive, protected, per-process memory; a hung app cannot take the machine down | Yes |
| 8 | Network | TCP/IP, DHCP, DNS, a shared folder another machine mounts | Partial — TCP/IP and clients, no file sharing |
| 9 | Print | Spooler with queued, cancellable jobs | No |
| 10 | Detect hardware | ACPI, PnP enumeration, power states, sleep and soft-off | No — PIC/PIT only |
| 11 | Survive failure | Crash dump, safe mode, recovery console, filesystem repair, restore points | Partial — journal, `fsck`, fault injection |
| 12 | Develop for it | Documented driver model and SDK, shared libraries, a C runtime, samples | Partial — flat and ELF32 programs, `tcc`, no shared libraries |
| 13 | Administer remotely | Remote console, remote service and configuration management | No |
| 14 | Play media | Mixed audio from several programs, video playback, a media framework | Partial — one audio stream per open, AC'97 and SB16 |

Starting position: **1 pass, 7 partial, 6 fail**, reported by `make check-parity`
from the tree rather than written down by hand.

## Gates

A wave does not close until these are measured and reported by `make check`:

| Gate | Target |
|---|---|
| Isolation | A hostile program cannot read, write, signal or open anything it was not granted |
| Containment | A faulting user program is retired; the machine survives 24 hours of it |
| Concurrency | 4 vCPUs under mixed load for 24 hours with no panic and no lost wakeup |
| Durability | 10 000 power-cut iterations on a written volume, every one recoverable |
| Latency | syscall < 1 µs, context switch < 5 µs, fork+exec < 5 ms (i486-era target) |
| Footprint | < 16 MB RAM idle; < 5 s to the desktop under QEMU |
| Admin | Every setting in the machine is reachable from the management console and the store |
| Parity | `make check-parity` reports at least the baseline, and no row ever moves backwards |

## Where the tree stands

What the plan builds on, and must not regress:

- Preemptive four-level priority scheduler, `fork`/`exec`/`waitpid`, signals, pipes,
  shared memory, message queues, semaphores, `mmap`/`mprotect`, copy-on-write fork
  and per-task file descriptors — see [sched.inc](../kernel/sched.inc).
- Demand paging with file-backed mappings, a page cache with writeback and
  guarded demand-grown stacks — see [vm.inc](../kernel/vm.inc) and
  [paging.inc](../kernel/paging.inc).
- HBFS with a directory tree, extended attributes, permissions and a
  crash-consistent journal, behind a VFS with a block cache and page cache — see
  [hbfs.inc](../kernel/hbfs.inc), [vfs.inc](../kernel/vfs.inc) and
  [blkcache.inc](../kernel/blkcache.inc).
- TCP with a full state machine, ARP, ICMP, DHCP and DNS, sockets, `poll` and
  non-blocking connect — see [net.inc](../kernel/net.inc).
- ATA PIO and bus-master DMA, AHCI, virtio-blk, e1000 and rtl8139, AC'97 and SB16,
  PS/2 keyboard and pointer, VBE/BGA graphics, serial and PCI enumeration.
- The Burrows compositor desktop with surface IPC, damage rectangles, clipboard,
  file dialogs and notifications — see [burrows.inc](../kernel/burrows.inc).
- 187 syscalls, 93 shell built-ins, 264 programs, an in-OS C compiler and `hbpkg`.

## Waves

### M0 · Measurement, contract and the C foundation (3–5 weeks)

- The parity scorecard as an executable harness: `tools/parity_scorecard.py`
  computes the fourteen rows below from evidence in the tree and compares them
  with `docs/parity-baseline.json`. `make check-parity` runs it, `make check` and
  CI both fail when a row regresses, and `make check-parity-update` is the only
  way a row improves — reviewed with the change that earns it.
- Freeze the syscall ABI behind a versioned table. New calls get numbers from the
  table; old numbers keep working. `docs/syscalls.json` stays generated.
- **C toolchain bootstrap.** `tools/find_c_toolchain.sh` probes `i686-elf-gcc`,
  `i386-elf-gcc`, `gcc -m32` and `clang --target=i386-elf`, and proves each by
  compiling a probe and checking the object is 32-bit ELF and links with
  `ld -m elf_i386`. On a non-x86 host only a cross compiler will pass, so
  `make check-toolchain` reports what is available instead of failing a build
  that does not need C yet. Build integration, a linker script for C-built
  subsystems, and a freestanding runtime (`memcpy`, `memset`, `strlen`,
  `memmove`) land here.
- **Per-task FPU and SSE state.** *Done for x87:* each TCB carries an image and
  every switch path saves and restores it with `fnsave`/`frstor`, proved by
  `programs/fpu_test.asm` in `make check-runtime`. SSE state is not saved yet,
  which is why kernel C is still compiled `-mno-sse`; the area is 16-byte
  aligned so `fxsave`/`fxrstor` can replace it when a CPU with FXSR is the
  baseline.
- Normative specs written before the code that needs them: `docs/spec/ABI.md`
  (calling convention and structure layout at the asm/C boundary),
  `docs/spec/OBJECTS.md`, `docs/spec/DRIVERS.md`, `docs/spec/CONFIG.md`.

**Exit:** the scorecard runs in CI and reports the true 1/7/6 baseline, and a
row is proven to move only by adding evidence; a
C-built kernel module and a C-built user program both run; a floating-point
workload across a context switch is correct under `make check`.

### M1 · The executive: objects, handles, security (8–12 weeks)

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
  the owner case and the stranger case in `make check-runtime`. Directory write
  permission on create, `truncate` and `rename` are still open.

- **The conformance gate (landed).** `programs/hostile_test.asm` runs as an
  ordinary user and requires eleven refusals and two controls, and it has been
  seen to fail when an enforcement is disabled. Fault containment is the other
  half and already existed: `stack_overflow` must kill the offending task while
  the shell runs the next command. What remains for the full gate is signalling
  (no cross-user signal policy exists yet) and the object layer.

**Exit:** a hostile-program conformance suite where a deliberately misbehaving
program fails to read, write, signal or open anything it was not granted, and
cannot take the machine down trying.

### M2 · The machine: ACPI, APIC, SMP, PnP and drivers (14–20 weeks)

Where Mellivora trails a 2000 machine most visibly: it cannot use a second CPU,
sleep, or switch itself off.

- ACPI: RSDP/RSDT/XSDT discovery, MADT, FADT, the SCI interrupt, the power button,
  sleep states and soft-off, thermal and battery where present.
- Local APIC timer taking over the tick from the PIT; I/O APIC routing input;
  inter-processor interrupts.
- **SMP bring-up**: application-processor start, per-CPU state behind a segment
  base, scheduler and allocator locking, and lock-order validation that finds a
  deadlock before two CPUs do.
- **Loadable driver model**: modules with an export table, probe/attach/detach,
  a device tree, resource arbitration (I/O ports, MMIO, IRQ, DMA), and power
  callbacks. Every existing driver is converted with no behaviour change.
- Structured boot logging, so a machine that fails to enumerate says why.

**Exit:** four vCPUs under 24 hours of mixed load with no panic; sleep and soft-off
work from the shell and from the desktop; every driver loads as a module.

### M3 · Storage: HBFS v5 (12–18 weeks)

- On-disk v5: per-file access control lists, alternate streams, per-user quotas,
  transparent compression, encryption at rest, sparse files, hard links, and
  snapshots that back the restore points in M9.
- Volume manager: MBR and GPT, multiple volumes, mount points rather than drive
  letters, removable media with safe ejection, volume labels and health.
- Online repair plus an offline checker at parity with the current `fsck`, and a
  recovery path for a volume whose journal cannot be replayed.
- The redirector seam in the VFS that network shares plug into in M7.

**Exit:** 10 000 power-cut iterations clean; a quota and an access-control
conformance suite; a deliberately corrupted volume repaired from the recovery
console; an encrypted volume round-trips with its key held in the store.

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

### M5 · Runtime and binaries (16–22 weeks)

The largest single lift, and the one that makes "an operating system you develop
for" true.

- **PE/COFF loader** and shared libraries with a stable ABI: relocations, export
  and import tables, per-process module lists, load-time and run-time binding.
  The existing flat and ELF32 loaders stay, so all 264 programs keep working.
- **Unicode end to end**: UTF-16 at the ABI boundary, UTF-8 internally, locale and
  time-zone data, collation, and a scalable font stack for the desktop rather than
  the 5×7 VBE font.
- A C runtime: string, memory, formatted I/O, allocation, and the start-up code a
  normal C program expects.
- An interface-over-objects model — a COM-flavoured typed dispatch layer on top of
  the Warren — so applications call interfaces instead of raw syscall numbers.
- The POSIX personality stays as a compatibility layer for existing programs and
  future ports.

**Exit:** a multi-source application using shared libraries, a Unicode interface,
the Sett for configuration and Chronicle for logging is built with the SDK and
the in-OS toolchain, and runs.

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

### M7 · Networking, the 2000 kind (13–18 weeks)

- **File and printer sharing** over a protocol of our own on TCP: share permissions
  layered on file access control, browsing, and mapped shares in the shell
  namespace.
- Network logon against another Mellivora machine, in a workgroup model.
- A remote procedure call mechanism for remote service and configuration control —
  the foundation of remote administration.
- A firewall with a rules interface, connection sharing, and dial-up or VPN.
- Remote console and remote management: service control, configuration, event log
  and file transfer against a workstation, without a graphical session.

**Exit:** two virtual machines — share a folder, mount it on the other, log on
across the network, print to a shared printer, and restart a service on the other
machine from Den.

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
|---|---|
| The C boundary becomes two systems in one tree | One calling convention, one ABI document, one build entry point per module, and a lint gate that rejects a module mixing the two without an entry stub |
| Objects and security are retrofitted too late | They are wave M1, before any feature that would have to be reworked for them |
| SMP destabilises the tree | AP start-up is a separate gate; the machine stays bootable single-CPU, and the 24-hour soak is the entry condition for M3 |
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

The normative documents this plan depends on. Existing ones are linked; the rest
are written before the wave that needs them.

| Document | Covers | Needed by |
|---|---|---|
| [SECURITY.md](spec/SECURITY.md) | Threat model, privilege boundaries, exception justifications | M1 |
| [VMM.md](spec/VMM.md) | Address-space layout and mapping rules | M1 |
| `spec/ABI.md` | Calling convention, structure layout, syscall numbering at the asm/C boundary | M0 |
| `spec/OBJECTS.md` | Object types, handle rights, lifetime and waiting | M1 |
| `spec/DRIVERS.md` | Driver interface, device tree, power and PnP contracts | M2 |
| `spec/CONFIG.md` | The Sett's key space, typing, transaction rules and migration | M4 |
| `spec/STORAGE.md` | HBFS v5 on-disk format, access control and recovery | M3 |
