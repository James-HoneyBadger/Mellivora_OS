# Reliability and diagnostics (normative)

**Normative from:** M9. **Governs:** what happens when something goes wrong —
a hung task, a faulting program, a crashing kernel, a failed update — and how the
machine is brought back without a reinstall.

The tree today contains a task that dies alone, a panic with a symbolised stack,
a serial fault report, an undo journal, `fsck`, and a fault-injection framework.
That is a good start and it is all local: there is no dump to read, no safe mode
to boot into, and no way to undo a bad change.

## 1. Failure classes

| Class | Example | Response |
|---|---|---|
| **Contained** | A user program faults | Kill the task, report, keep the session |
| **Contained with loss** | A driver returns an error | Fail the I/O, mark the device, keep the volume |
| **Degraded** | A device cannot start | Report, continue without it |
| **Fatal** | A kernel invariant breaks | Dump if configured, panic, restart |
| **Boot-fatal** | The system volume will not mount | Offer safe mode and the recovery console |

Every class has a defined response (§1), a visible report and an audit record
when it touches security. "It hung" is not a class.

## 2. Hung task detection

| Mechanism | Contract |
|---|---|
| Wait reasons | Every blocked thread records what it waits for ([SCHED.md](SCHED.md#4-waiting-and-wait-reasons)) |
| NMI watchdog | Each processor pings a counter from its timer; the bootstrap processor checks every 5 s and reports a processor that has stopped pinging, with its last EIP |
| Window watchdog | A window that has not pumped its message queue for 5 s is marked **not responding**; the compositor greys it and offers "wait" or "end" |
| Stuck-service detection | The SCM's interrogate timeout ([SERVICES.md](SERVICES.md#12-lifecycle)) |
| Deadlock detection | Spinlock ranks refuse an inversion before it happens ([as built](#9-as-built-the-gap)); a resource-ordering cycle at `PASSIVE` is detected by a cycle check on the wait graph, and the cycle is reported, not broken |

Reporting is the priority. A watchdog that silently kills things hides the bug
that caused it.

## 3. Fault containment

| Fault | Response |
|---|---|
| Ring-3 exception | Kill the task, record EIP/address/error code and the task name, notify the parent, keep the session |
| Ring-3 fault in a service | The SCM's recovery actions run ([SERVICES.md](SERVICES.md#13-recovery)) |
| Ring-0 fault with a symbolised site | Panic: dump, print a stack, restart |
| Ring-0 fault while holding a lock or at `DISPATCH` | Panic, naming the lock or level; continuing would be corruption |
| Double fault | IST stack, panic with both fault frames |
| Stack overflow in ring 0 | Guard page, panic naming the thread; in ring 3, the task dies |
| A driver that touches hardware after removal | Panic, naming the driver and the device |

The rule from [SECURITY.md](SECURITY.md#10-signals-crashes-and-refusals) is
unchanged: a supervisor fault is never survived on purpose.

## 4. Crash dumps

| Kind | Contents | Size |
|---|---|---|
| **Small** | Faulting context, loaded modules, the trace ring, thread list | < 256 KiB |
| **Kernel** | All kernel memory plus the small set | RAM-sized |
| **Full** | Everything, including user pages of the faulting process | RAM-sized |

- The dump is written to a **reserved area** chosen at boot (a dedicated
  partition, a preallocated file, or the pagefile), so a full volume cannot
  prevent the dump it needs.
- Written by a path that assumes the allocator and the file system may be
  broken: fixed buffers, no locks, a raw block writer.
- **Redaction**: pages in the credential store's range are replaced with a
  marker before the write ([SECURITY.md](SECURITY.md#9-secrets)).
- On the next boot the dump is symbolised against the running image and its
  hashes, and a mismatch is reported (a dump read against the wrong kernel is
  worse than no dump).
- `dump`, `dump list`, `dump show <n>` are the shell surface; the file is a
  documented format so an offline tool can read it.

## 5. Boot modes and the recovery console

| Mode | Providers | When |
|---|---|---|
| Normal | Everything | Default |
| **Safe mode** | Kernel, storage, input, display, the management console; no third-party drivers, no auto services | Chosen at the boot menu, or automatically after two failed boot attempts |
| **Recovery console** | Kernel, storage read-only, a small command set | Chosen at the boot menu |
| Last known good | A restore point ([§6](#6-restore-points)) | Chosen at the boot menu |

Recovery console verbs: `mount`, `chkdsk --repair`, `restore`, `restorepoint`,
`registry` (load/save a hive, reset a key), `services` (disable a service),
`drivers` (disable a module), `log` (read the last boot's trace), `dump`,
`reboot`, `poweroff`. It runs with a restricted token, audits everything, and
does not load third-party code.

The boot menu is reachable when a boot attempt fails twice in a row, and that
counter is stored outside the volume whose mounting may be the failure.

## 6. Restore points

| Piece | Contract |
|---|---|
| What it captures | A volume snapshot ([STORAGE.md](STORAGE.md#10-snapshots)) plus the machine hive's last-known-good generation ([CONFIG.md](CONFIG.md#7-schema-defaults-and-migration)) |
| Triggers | Before a package install or update, before a driver install, before a service configuration change, on a schedule, on demand |
| Cost | O(1) to create, space proportional to what changed, charged to the volume |
| Retention | Per volume and per count/age policy; the oldest is pruned, never the one a pending update created |
| Rollback | Mount the snapshot, restore the hive, mark the system for a reboot, and complete the swap during the next boot before the first service starts |
| Failure | A rollback that cannot complete leaves the system on the newer state and says so; it never leaves it in neither |
| Audit | Every creation and rollback is recorded with the reason |

## 7. Fault injection

The existing framework (`fi_*` gates and `fault_inject_test`) becomes a
first-class facility:

| Knob | Scope |
|---|---|
| Allocation failure at the *n*th call | Any allocator, with a deterministic seed |
| I/O failure with a status | Any device, per operation |
| Dropped or reordered frame | Any network interface |
| Corrupted block, checksum intact/flipped | Storage, at read or write |
| Delayed and cancelled IRP | Any driver |
| Clock jump | Timers |
| Power loss | Any transaction boundary |

Every suite runs a fault-injection pass in CI with a fixed seed, so a failure is
reproducible, and the seed is printed on failure.

## 8. Tracing

| Piece | Contract |
|---|---|
| Ring buffers | Per component, per processor; 64 KiB each by default, sized from the Sett |
| Event | `{timestamp, component, level, event id, task, cpu, args[4], name}` |
| Levels | Error, warning, information, verbose; a component's level is a Sett key |
| Reader | `log` (shell), the event log ([OBJECTS.md](OBJECTS.md#3-types)) for anything persistent, and a dump |
| Overhead | < 1 % when information-level tracing is off; a disabled provider costs a branch |
| Persistence | Security-relevant events go to the event log, chained and checksummed ([SECURITY.md](SECURITY.md#7-audit)); everything else is a ring that a dump carries |

## 9. The gates

These are the numbers a wave must hold, not aspirations:

| Gate | Target |
|---|---|
| Containment | 24 hours of a program that faults at random intervals: the session survives, every fault is reported |
| Concurrency | 4 processors, mixed load, 24 hours: no panic, no lost wakeup, no thread stuck with the same wait reason |
| Durability | 10 000 power cuts on a written volume, every one recovered |
| Memory | Committed pages, pool per tag, handles and IRPs return to their baselines after every suite |
| Performance | Syscall < 1 µs, context switch < 5 µs, fork+exec < 5 ms, frame at 60 Hz |
| Boot | < 5 s to the desktop, < 16 MB idle |
| Recursion | No kernel stack overflow in 24 hours of the suites |

A gate that fails is a release blocker, and its number is printed by
`make check` so a regression is a diff, not a mystery.

## 10. Conformance

M9 does not close until:

1. A forced panic produces a small dump, the next boot symbolises it, and
   `dump show` prints the faulting stack.
2. Two failed boot attempts produce the boot menu; safe mode boots with a
   third-party driver disabled; the recovery console repairs a corrupted volume.
3. A restore point created before a package install rolls the machine back to the
   pre-install state, including the hive.
4. The NMI watchdog reports a processor that stops pinging, and the hung-window
   path greys and recovers a stuck application.
5. Every fault-injection knob has a deterministic seed, and a seeded run
   reproduces its failure.
6. All seven gates in §9 pass on the reference image.

## 11. As built (the gap)

| Contract | Tree today |
|---|---|
| Crash dumps | Serial text only |
| Safe mode, boot menu, recovery console | None |
| Restore points | None |
| NMI watchdog, hung-window detection | None |
| Trace ring with levels and a reader | `dmesg`-style buffer |
| Fault injection | `fi_*` gates for OOM, disk and network, tested |
| Fault containment | Ring-3 faults kill the task; the session survives |

Fault containment, the panic path, the journal, `fsck` and fault injection are
real and tested; the dump, recovery and tracing layers are new in M9.
