# Specifications

These are the normative documents. Code that disagrees with one of them is the
bug, except where a section is marked **as built** — that records what exists
today so the gap between the contract and the tree is a visible distance rather
than a surprise.

The rule that makes the set usable: **a specification is written before the wave
that needs it, and it describes the system we are building, not the one we
have.** No requirement here is constrained by a current buffer size, a current
structure layout or a current syscall number. Where today's tree has a ceiling
that exists only because something was sized by hand, the ceiling is gone from
the contract and the removal is listed in the table below.

## Status vocabulary

| Term | Meaning |
|---|---|
| **Normative from M*n*** | The wave cannot close until the tree satisfies it, and a conformance test fails before it does |
| **As built** | What the tree does today, recorded so the difference is auditable |
| **Open** | A decision this document deliberately leaves to the wave that needs it, with the constraint stated |

## The documents

| Document | Governs | Normative from | Conformance |
|---|---|---|---|
| [ABI.md](ABI.md) | The binary interface: calling convention, types, structure layout, status codes, syscall encoding, versioning | M0 (v1), M1 (v2) | `make lint`, `check-syscall-header`, `check-syscalls-json` |
| [OBJECTS.md](OBJECTS.md) | The object namespace: types, handles, rights, lifetime, waiting, names | M1 | `handle_test`, `hostile_test`, object suite |
| [SECURITY.md](SECURITY.md) | Threat model, tokens, ACLs, access checks, privileges, integrity, audit | M1 | `hostile_test`, `perm_test`, `dirperm_test`, audit suite |
| [VMM.md](VMM.md) | Address spaces, regions, page tables, fault rules, physical memory | M1 | `vmtest*`, `memprot_test`, `cow_test` |
| [SCHED.md](SCHED.md) | Processors, queues, priorities, timers, jobs, accounting | M2 | soak gate, `sched` suite |
| [PLATFORM.md](PLATFORM.md) | ACPI, interrupt controllers, timers, SMP, power, caching, CPU features | M2 | `acpi` cases, `locks`, 24 h soak |
| [DRIVERS.md](DRIVERS.md) | Driver model, modules, device tree, I/O requests, DMA, power, PnP | M2 | driver suite, unload soak |
| [STORAGE.md](STORAGE.md) | HBFS v5 on-disk format, volumes, cache, repair, snapshots | M3 | `fsck`, power-cut harness, quota/ACL suites |
| [CONFIG.md](CONFIG.md) | The Sett: key space, types, transactions, notifications, hives | M4 | transaction suite, migration tests |
| [SERVICES.md](SERVICES.md) | Service control, dependencies, recovery, logon, sessions, profiles | M4 | logon and service suites |
| [EXEC.md](EXEC.md) | Images, loader, shared libraries, TLS, ASLR, process creation | M5 | loader suite, `argv_test`, ASLR cases |
| [IPC.md](IPC.md) | Ports, messages, sections, waits, named pipes, quotas | M1 (sections), M5 (ports) | IPC suite, deadlock soak |
| [NETWORK.md](NETWORK.md) | Socket objects, stack contracts, shares, RPC, remote administration | M7 | net suite, share mount case |
| [MEDIA.md](MEDIA.md) | Audio graph, video, USB classes, print spooler | M8 | media suite |
| [SHELL.md](SHELL.md) | Shell namespace, associations, drag and drop, clipboard, notifications | M6 | desktop suite |
| [RELIABILITY.md](RELIABILITY.md) | Dumps, safe mode, recovery console, restore points, tracing, watchdogs | M9 | fault containment, 24 h soak |
| [ECOSYSTEM.md](ECOSYSTEM.md) | Packaging, registration, dependencies, SDK, ABI stability | M10 | package suite, SDK samples |

## The ceilings this set removes

Every row is a number that exists in the tree today because something was sized
by hand. The right-hand column is the contract; the tree catches up in the wave
named.

| Parameter | As built today | Contract | Wave |
|---|---|---|---|
| Live tasks | 128, from a fixed 656-byte static table | 65 536, TCB from the slab allocator, sized per task | M1 |
| Handles per process | 64 legacy + 128 VFS, one 4 KB page | 4 096 by default, growable to 65 536, paged handle table | M1 |
| Handle rights | read/write only | 16-bit rights mask plus generic mapping per type | M1 |
| Kernel virtual space | ~2 MB image window at 1 MB, 8 MB reserve | 1 GiB at `0xC0000000`: image, heaps, page tables, cache | M1 |
| User virtual space | ~1 MB program window plus a demand zone under 512 MB | 3 GiB per process, ASLR across the whole range | M1 |
| Physical memory | 128 MB tested; frames only below 448 MB | 64 GiB via PAE, PFN database over all of it | M1 |
| Syscalls | 187, flat, frozen by number | The same 187 as a compatibility layer forever, plus typed groups beyond 512 | M1 |
| Names | 63 bytes, 8-bit | 255 UTF-16 code units, resolved case-insensitively for the Windows-facing namespace | M1 |
| Directory entries | 224 per subdirectory, 2 048 per root | 65 535 per directory, B+tree directories | M3 |
| File size | 32-bit block numbers, 2 GB volume | 2^63 bytes per file, 2^62 bytes per volume, 64-bit block numbers | M3 |
| Program image | 1 MB flat binary | 3 GiB image, PE32 or native, shared libraries | M5 |
| Processors | 8 slots, APs parked | 64, every one scheduling | M2 |
| Interrupt lines | 16 legacy + IOAPIC GSIs | 256 GSIs per processor, MSI and MSI-X | M2 |
| Block cache | 8 MB fixed buffer | One eighth of physical RAM, clamped to [8 MB, 2 GiB] | M3 |
| Page-table levels | 2 (PAE: 3) | 3, with a PFN database rather than a bitmap | M1 |
| Time | 100 Hz PIT or LAPIC tick | 1 ns resolution, deadline-driven, per-CPU | M2 |
| Crash evidence | serial text | Dump file in a reserved area, symbolised, plus a trace ring | M9 |

## Decisions this set records

Numbered so a wave can cite the reason rather than re-litigate it. They extend
the D-series in [the roadmap](../ROADMAP.md#architectural-decisions).

| # | Decision | Rationale |
|---|---|---|
| S1 | **No accidental ceilings.** Any limit a caller can hit is either a documented policy limit (task count per job, quota) or derived from hardware. A compile-time array bound is not a limit, it is a bug with good manners. | Every ceiling above was found by hitting it: `/bin` at 224 entries dropped test programs silently, and a 2 MB kernel window is 95 % full. |
| S2 | **One object model, no second-class resources.** Files, processes, threads, sections, sockets, keys, devices, timers, jobs and ports are all objects with the same handle, rights, wait and naming rules. | A resource that is not an object cannot be secured, shared, waited on or remotely managed without special cases. |
| S3 | **The old ABI never breaks.** The 187 existing syscalls keep their numbers and meanings forever, implemented over objects. New capability arrives as new calls. | 2 000+ programs and every test in the tree call them; "monumental" must not mean "rewrite userland". |
| S4 | **Fixed-width types at every boundary.** `u8`/`u16`/`u32`/`u64` only; no `int`, no `long`, no pointer-sized field in a structure that is shared, stored or sent. | Keeps structures valid across a future 64-bit move without a flag day, and makes the ABI portable to a harness. |
| S5 | **32-bit is the target; PAE is the ceiling-raise.** x86-64 stays out of scope for parity; physical memory grows to 64 GiB through PAE and per-process virtual space stays 3 GiB. Every structure is written so the move is a recompile, not a redesign. | Windows 2000 parity is a 32-bit target; the work that makes 64-bit cheap is the same work that makes the ABI sane. |
| S6 | **Fail closed, and say why.** A missing right, a missing mapping, a checksum that does not match, a journal that will not replay: the operation is refused and the refusal names the check. | Silent acceptance is how corrupted data becomes the new normal. |
| S7 | **Every claim has a test that fails first.** A wave closes when a test that failed on the previous commit passes; a capability that cannot be tested is not built. | The parity scorecard proved this works; the specs use the same discipline. |
| S8 | **One implementation of each rule.** One access check, one mapping routine, one allocator, one name resolution path, one I/O path. | Every duplicate is a place where the second copy drifts and the security model develops a hole. |
