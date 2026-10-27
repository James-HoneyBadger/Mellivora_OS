# The object namespace (normative)

**Normative from:** M1. **Governs:** every resource a program can name, hold,
share, wait on or secure. This document is the spine of the system: from M1 on,
"the object manager" is not a subsystem beside the others, it is the thing the
others are made of ([S2](README.md#decisions-this-set-records)).

## 1. Why a namespace of objects

Today a resource is reached four different ways: a file descriptor number, a
task id, a socket number, a pointer. Each has its own rights story, its own
lifetime story and its own way of failing. That is why `chmod` could store a
mode nobody checked, why a handle could be written through after being opened
read-only, and why nothing can be shared with another process except by number.

One model instead:

- a resource is a **typed object** with a header;
- a program refers to it by a **handle** carrying **rights**;
- an object has a **name** in one namespace, a **security descriptor**, and a
  **signalled state** other objects can wait on;
- the **object manager** owns lookup, lifetime, access checks and waiting.

## 2. Object header

Every object begins with the same header. It is 64 bytes and 16-byte aligned so
that a per-type body follows at a fixed offset:

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | `type` | Index into the type table; immutable |
| 4 | 4 | `flags` | Permanent, session, kernel-only, inheritable, deleting |
| 8 | 4 | `refs` | Pointer references (handles, kernel users, pending IRPs) |
| 12 | 4 | `handles` | Open handle count, for accounting |
| 16 | 4 | `name_offset` | Offset into the object's name, 0 = unnamed |
| 20 | 2 | `name_length` | Code units, without terminator |
| 22 | 2 | `reserved` | Pads the name to a 4-byte boundary |
| 24 | 4 | `security` | Handle of the security descriptor ([SECURITY.md](SECURITY.md)) |
| 28 | 4 | `waiters` | Head of the wait list |
| 32 | 8 | `created` | `mv_time` |
| 40 | 4 | `owner_pid` | Creator, for quota and debugging |
| 44 | 4 | `quota_charge` | Pool bytes charged to the creator |
| 48 | 16 | `type_data` | Per-type word (signalled state, IRP queue head, …) |

The header is described by `struct mv_object_header` in
`c/include/mv/objects.h`, with every offset asserted. Nothing may be inserted
before offset 48; a type that needs more than 16 bytes of private header space
puts it in its body.

## 3. Types

| Type | Name in the namespace | Signalled when | Private body |
|---|---|---|---|
| `file` | under a volume | never (I/O completion goes to a port) | position, access mode, close policy |
| `section` | `\Sessions\<n>\BaseNamedObjects` | never | size, protection, backing file or page set |
| `process` | none (named via a debug object) | the process exits | address space, token, handle table, exit status |
| `thread` | none | the thread exits | context, priority, affinity, wait reason |
| `job` | `\KernelObjects` | every process in it exits | limits, accounting, completion port |
| `token` | none | never | SIDs, groups, privileges, integrity ([SECURITY.md](SECURITY.md)) |
| `event` | yes | manual or auto per creation | manual-reset flag, state |
| `mutant` | yes | unowned | owner thread, recursion count |
| `semaphore` | yes | count > 0 | current count |
| `timer` | yes | expiry, until reset | due time, period, callback port |
| `port` | `\RpcControl` | a message is queued | message queue, queue limits, owner |
| `key` | under `\Registry` | never | the Sett's node ([CONFIG.md](CONFIG.md)) |
| `device` | `\Devices` | never | driver, resources, power state ([DRIVERS.md](DRIVERS.md)) |
| `driver` | `\Drivers` | never | load address, dispatch table, references |
| `socket` | none (a port gives its name) | data or hangup | endpoint, protocol, options ([NETWORK.md](NETWORK.md)) |
| `eventlog` | `\Logs` | never | append-only records ([RELIABILITY.md](RELIABILITY.md)) |
| `service` | `\Services` | never | state machine ([SERVICES.md](SERVICES.md)) |
| `symlink` | yes | never | target path |

A **directory** object holds names. A **type** is a descriptor listing: its name,
the access rights it defines, the size of its body, its open/create/close and
signalled-state routines, and whether it can be permanent. The type table is a
data structure, not a switch statement, so a driver can register a type and the
object manager will name, secure, wait on and count its objects exactly like the
built-in ones.

## 4. Handles

A handle is a `u32` with a generation, per [ABI.md](ABI.md#6-handles). Each
process owns a **handle table**:

| Property | Contract |
|---|---|
| Default capacity | 4 096 entries |
| Maximum | 65 536 entries |
| Growth | By 4 096-entry pages, charged to the process, refused on quota rather than silently truncated |
| Entry | `{ object pointer, rights: u32, attributes: u8 }`, 12 bytes |
| Stale detection | The generation in the handle must match the entry |
| Inheritance | Only for handles created with `INHERIT`, and only across process creation, never across an exec that replaces the image |

Rights are a 16-bit mask per type, with the top half reserved for type-specific
rights and the bottom half generic:

| Bit | Generic right | Means |
|---|---|---|
| 0 | `QUERY_STATE` | Read the header and the type's status (no data access) |
| 1 | `SET_STATE` | Signal, reset, set the type's state (no data access) |
| 2 | `READ_DATA` | Read the object's contents |
| 3 | `WRITE_DATA` | Change the object's contents |
| 4 | `EXECUTE` | Use it as code, or invoke it as a target |
| 5 | `DELETE` | Remove the name |
| 6 | `READ_ATTRIBUTES` | Read the security descriptor and metadata |
| 7 | `WRITE_ATTRIBUTES` | Change the security descriptor and metadata |
| 8 | `SYNCHRONIZE` | Wait on it |
| 9–15 | type-specific | `FILE_APPEND`, `SECTION_MAP_WRITE`, `PROCESS_CREATE_THREAD`, … |

`MV_RIGHTS_ALL` in a create call is mapped by the type to the specific rights it
implies, so a caller never has to know the type's numbering. This mapping is the
one place it happens ([S8](README.md#decisions-this-set-records)).

`DuplicateHandle` produces a second handle with **no more** rights than the
first, and requires `WRITE_DATA` on the target process. Rights are never
amplified: a duplicated handle can only narrow.

## 5. The namespace

One tree, case-insensitive and case-preserving:

```text
\                                   root
├── Devices                         drivers' device objects
├── Drivers                         loaded modules
├── KernelObjects                   job, event and mutant namespace
├── Logs                            event logs
├── Registry                        the Sett's hives (CONFIG.md)
├── RpcControl                      port names — the IPC rendezvous
├── Services                        service objects
└── Sessions
    ├── 0
    │   ├── BaseNamedObjects        session 0: services only
    │   └── DosDevices              drive letters live here as symlinks
    └── 1
        ├── BaseNamedObjects        the logged-on user's objects
        └── DosDevices
```

| Requirement | Rule |
|---|---|
| Name length | 1..255 `char16`, terminator not counted ([ABI.md](ABI.md#8-text-and-names)) |
| Separator | `\` only; `/` is accepted on input at the file-system boundary and normalised there, once |
| Case | Folded with the fixed table in `c/rt/utf.c`; stored as given |
| Lookup | Longest matching named object wins; a directory is searched only if the lookup holds `TRAVERSE` on it |
| Symlink | Followed with a depth limit of 32 and a loop detector; `NO_FOLLOW` opens it as an object |
| Creation | `OPEN`, `CREATE`, `OPEN_IF`, `SUPERSEDE`, `OVERWRITE_IF` — the same five every type uses |
| Ownership of the name | Creating a name requires `WRITE_DATA` on the directory |
| Deletion | Immediate with `DELETE_ON_CLOSE`, or when the last handle closes |

A **session** is a namespace boundary as well as a security one: a process in
session 1 cannot see or address session 0's base-named objects unless it holds
`CAP_SESSION_CROSS`. This is what makes "switch user without a reboot" possible
later ([SERVICES.md](SERVICES.md)) rather than a rewrite.

## 6. Waiting

Any handle with `SYNCHRONIZE` can be waited on. This is the primary
synchronisation primitive of the system; a semaphore, an event, a mutant, a
process, a thread, a timer, a port and a file with pending I/O all use it.

| Call | Semantics |
|---|---|
| `WaitOne(handle, timeout, alertable)` | Returns when signalled, on timeout, or on an alert |
| `WaitAny(handles[], count, timeout, alertable)` | Returns the index that satisfied it, `MV_TIMEOUT`, or `MV_USER_APC` |
| `WaitAll(handles[], count, timeout, alertable)` | All must be signalled at once; a manual-reset event stays signalled, an auto-reset event is consumed only when the wait succeeds |

- `count` is limited to **64** per call; more than that is a queue of waits, not
  a bigger array, and the limit is documented policy rather than an array bound.
- Waiting is **fair**: a waiter queue is FIFO per object, and a wake always
  transfers to the processor that owns the waiter's affinity.
- `alertable` waits are the only way a blocking call can be interrupted; it
  returns `MV_USER_APC` after running the queued user callbacks. This is what
  lets a console program be cancelled without a signal ([IPC.md](IPC.md)).
- A wait on the current thread's own process is refused: it cannot be satisfied.
- The scheduler counts a wait reason, so a hung system can name what each task
  is waiting for ([RELIABILITY.md](RELIABILITY.md)).

## 7. Lifetime

Reference counting alone is not enough, because an object can be entered through
a pointer without a handle (an in-flight I/O, a wait list, a mapped section).
Three rules:

1. **A handle holds a reference.** Closing the last handle does not necessarily
   destroy: deletion is deferred until `refs` reaches zero.
2. **A pointer reference is taken before the object is used and released after**;
   a `_rundown` region marks the object as deleting so an attempt to take a new
   pointer reference fails instead of resurrecting it.
3. **Destruction is ordered by type.** A process tears down its threads, then its
   address space, then its handle table then its own header; a driver is
   unloaded only when its device objects have no references and no IRPs
   ([DRIVERS.md](DRIVERS.md)). The teardown order is part of the type
   descriptor, so shutdown is not a special case.

Two failure modes are detectable and tested rather than hoped for:

- **Underflow**: a release that would take `refs` below zero stops the machine
  with the object type and pointer, because the alternative is a use-after-free
  that shows up as corruption elsewhere.
- **Leak**: a handle table that is not empty when the process exits is reported
  by handle number and type; `programs/handle_test.asm` covers the rights half,
  and the object suite adds the leak half.

## 8. Accounting and limits

| Quantity | Limit | Enforced at |
|---|---|---|
| Handles per process | 4 096 default, 65 536 maximum | `CreateObject` and duplication |
| Handles system-wide | Derived from pool, charged per process | Allocation |
| Object name length | 255 code units | Create |
| Directory depth | 32 levels | Create |
| Section size | 2^47 bytes | Section creation |
| Port queue | 64 messages by default, configurable per port to 4 096 | Send |
| Wait set | 64 handles | Wait |

Every object charges its body to the creating process and, if that process is in
a job, to the job. `MV_NO_MEMORY` when the charge cannot be paid, never a
silent overrun. Quotas are policy and therefore belong in a table
([SCHED.md](SCHED.md), [SERVICES.md](SERVICES.md)), not in a compile-time
constant ([S1](README.md#decisions-this-set-records)).

## 9. Security hook

Every object has a security descriptor, and **every** way of obtaining a handle
runs the same access check ([SECURITY.md](SECURITY.md#5-the-access-check)):

| Path to a handle | Check |
|---|---|
| Open by name | `TRAVERSE` on each directory crossed, then the requested rights on the object |
| Create in a directory | `WRITE_DATA` on the directory, then the create rights on the new object |
| Duplicate from another process | `PROCESS_DUP_HANDLE` on the source process, then no amplification |
| Inherit across process creation | The child's token is checked against the object, not the parent's |
| Kernel-internal pointer use | No check — the kernel is trusted; the mode is recorded in the audit log for the object types that matter |

Rights that a handle carries are enforced at the operation, not only at open, so
`WRITE_DATA` on an event means the same thing as `WRITE_DATA` on a file.

## 10. Observability

`objects` lists every type, its live count, its peak count and its pool charge;
`handles <pid>` lists a process's handles with rights and type. Both are read-only
and available to a process holding `CAP_PROC_AUDIT`. The counts are how the 24-hour
soak gate detects a leak that a single test cannot ([RELIABILITY.md](RELIABILITY.md)).

## 11. Conformance

M1 does not close until all of these pass:

1. `handle_test` — a handle carries its rights: read-only cannot write, write-only
   cannot read, even as root.
2. `hostile_test` — eleven refusals, two controls, no crash, including a forged
   handle and a stale handle.
3. `object_test` (new) — every type names, opens, waits, duplicates and closes;
   rights never amplify; a stale handle is refused; a crossed-session name is
   refused without `CAP_SESSION_CROSS`.
4. `wait_test` (new) — `WaitAny`, `WaitAll`, timeout, alertable wake, FIFO
   fairness and the 64-handle limit.
5. Object counters return to their pre-test values after every suite.

## 12. As built

The executive exists. `kernel/object.inc` is the object manager, `kernel/dpc.inc`
the deferred-work queue, and `c/include/mv/objects.h` asserts every header
offset so the C and assembly views cannot drift.

| Contract | Tree today |
|---|---|
| Typed objects, one 512-byte body with the §2 header | Yes, for the nine types below. A type is a record in a table - name, rights mask, flags, close routine - not a switch |
| Handles with rights and generations | Yes: `(generation << 18) \| (index << 2)`, a per-process table grown from the PMM in 4096-entry steps, rights enforced at the operation |
| One namespace | `\`, `\KernelObjects`, `\Devices`, `\RpcControl`, `\Sessions\0` and `\Sessions\1` with `BaseNamedObjects` under each; case-insensitive longest-match lookup, traverse on every directory crossed, absolute symlinks with a depth limit |
| WaitOne / WaitAny / WaitAll | Yes, with poll (timeout 0) and blocking (timeout n), FIFO per object, a 64-handle limit, and auto-reset consumption |
| Reference counting and rundown | Yes: `obj_ref`/`obj_deref`, a deleting flag that refuses new references, and an underflow that stops the machine naming the type. Every descriptor table entry owns exactly one reference, which is why `fork` and task creation take one per inherited entry (`vfs_ref_all`) instead of copying a pointer |
| Deferred procedure calls | `dpc_queue`/`dpc_drain`; the timer's wait sweep is the first user, and a boot self-test proves order |
| Sessions | The namespace enforces the boundary; `SE_SESSION_CROSS` (uid 0) crosses it |

Types, and what each can do:

| Type | Waitable | Named | Notes |
|---|---|---|---|
| `Directory` | no | yes | the namespace's own container |
| `Event` | yes | yes | manual or auto reset, set at creation |
| `Mutant` | yes | yes | one owner, released by `SYS_MUTANT_RELEASE` |
| `Semaphore` | yes | yes | initial and maximum count from the create flags |
| `Timer` | yes | yes | a due tick; the periodic sweep is not wired to a callback port yet |
| `Token` | no | no | a view over the TCB's identity, so `setuid` cannot disagree with it |
| `Process` | yes | no | wraps the TCB; the type exists and is counted, and waiting on it is not yet wired |
| `Symlink` | no | yes | absolute targets only |
| `File` | no | yes | created by `SYS_OPEN` for every successful open, named after the path, released by `SYS_CLOSE` and by task exit; the frozen fd path is untouched, which is the compatibility alias §2 asks for |

The conformance §11 names: `handle_test` and `hostile_test` pass, `object_test`
covers §11's items 3 and 5 and adds the two security refusals, `wait_test`
covers item 4 inside one task, and `crosswait_test` covers it across tasks —
the waiter and the signaller are different tasks in both directions, and the
child finds the object by name because a fork starts with an empty handle
table. All four run in `make check-runtime`.

Waking a waiter in another task needed three fixes before it worked, none of
them visible while every wait was satisfied by the task that asked for it:

- `obj_satisfy_waiters` passed the *result of the evaluation* (the constant 1)
  to `obj_wait_consume` where a wait set belonged, so the consume walked the
  object array at address 1. The first signal that ever completed somebody
  else's wait faulted the kernel at `0x54F000FF`.
- `obj_yield_cpu` treated "no task is READY" as "nobody can ever signal me" and
  returned `WAIT_TIMEOUT` at once. A waiting task whose signaller is asleep -
  the ordinary case - was therefore told it had timed out microseconds after
  blocking. It now idles with interrupts enabled and re-scans, which is what a
  blocked task with a deadline should do; the immediate timeout is reserved for
  the one case where it is true, a machine with no other task at all.
- `sys_waitpid` blocked without clearing `TCB_WAKEUP`, so an expired deadline
  left over from an earlier `SYS_SLEEP` made the tick scan wake it on the next
  tick: the parent resumed with the `EAX` it blocked with (`92`, its own syscall
  number) instead of an exit code, and only when the task had slept first.
  Both ends now clear the field.

Handing an object to another task means the reference count follows the
handles, not the memory the handles live in. Two places had to learn that, both
found by the first test that forked with a file open: a task that exits never
switches out, so its private descriptor page still held the handles it closed
during its life and draining that stale page freed objects other tasks still
pointed at (`vfs_save_page` flushes the live tables into the page first), and a
new task inherits a copy of its creator's descriptors, so it owes a reference
per File object in the copy (`vfs_ref_all`).

Quotas are enforced: a process may hold `OBJ_QUOTA_PER_PROCESS` (256) live
objects, the create fails as a refusal rather than starving the slab, and a
close returns the charge - `objquota_test` proves the ceiling, the refusal, the
recovery and the return to baseline. The shell has the two commands the spec
asks for: `objects` prints live and peak counts per type with the ceiling, and
`handles` prints live handles per task.

Stated plainly, and to be removed as the waves land:
- **A file is an object, but only as far as the frozen fd path goes.** Every
  successful `SYS_OPEN` allocates a `File` object, named after the path and
  reference-counted by the descriptor, and task exit drops the ones it still
  holds. What is *not* done is the other direction: opening a file does not hand
  out a handle, and `SYS_READ`/`SYS_WRITE` still take the descriptor, because
  the handle-based file calls are the VFS wave's. A file object is therefore
  visible and accounted for, and cannot yet be waited on, duplicated into a
  handle, or secured per open.
- **The remaining nine types** in §3 - section, job, port, key, device, driver,
  socket, eventlog, service - arrive with the waves that own them.
- **`MV_RIGHTS_ALL` mapping is one table entry per type**, checked by
  `object_test` for events and by construction for the rest.
- **Alertable waits** return `MV_USER_APC` when a signal is pending; there are
  no queued user APCs to run yet, so that is the whole of the behaviour.
