# Scheduling and resources (normative)

**Normative from:** M2. **Governs:** processors, run queues, priorities, timers,
waiting, preemption rules, job objects and accounting.

The tree today has one run queue, four priority levels, a 100 Hz tick and a
parked-application-processor SMP story. This document is what replaces it: every
processor schedules, priorities are a policy table rather than four constants,
and time is a deadline rather than a tick.

## 1. Units and processors

| Quantity | Unit | Resolution |
|---|---|---|
| Time | `mv_time` | 100 ns at the ABI ([ABI.md](ABI.md#3-types)), nanoseconds internally |
| Quantum | nanoseconds of CPU time | 1 ms minimum, 20 ms default at the normal priority |
| Accounting | nanoseconds per task, thread and job | 1 µs granularity, exact counters |

A **processor** is a `struct cpu` in `0xC9000000` ([VMM.md](VMM.md#22-kernel-space))
holding: its APIC id, its slot, its current and idle threads, its run queues, its
timer queue, its interrupt counters, its TLB-shootdown generation, and its
per-CPU stack. `MAX_CPUS` is **64** ([S1](README.md#decisions-this-set-records));
a machine reporting more starts 64 and says so.

## 2. Priority classes

| Class | Range | Policy | Quantum |
|---|---|---|---|
| Real-time | 0–15 | Strict priority, never preempted by a lower class, no automatic boost | Fixed 10 ms |
| Variable | 16–31 | Strict priority with decay and boosts within the class | 20 ms, halved per level |
| Idle | 32 | Runs only when nothing else can | None |

- Lower number is higher priority, matching the existing tree so a reader does
  not have to relearn it.
- A **boost** raises a thread by up to 8 levels when it is woken from a wait it
  was blocked on (I/O completion, a signalled object, input). A boost decays one
  level per full quantum consumed, so a chatty thread does not become
  permanently high-priority.
- A **starvation guard** raises any runnable thread that has not run for 2 s to
  the top of the variable class for one quantum, and records the event. Without
  it, a busy real-time-class program can make the machine look hung
  ([RELIABILITY.md](RELIABILITY.md#2-hung-task-detection)).
- Real-time threads may be created only by a token holding `SE_INCREASE_QUOTA`
  level privilege ([SECURITY.md](SECURITY.md#3-privileges)); an ordinary process
  asking for one gets the top of the variable class and a warning in the audit
  log.

## 3. Run queues and dispatch

Per processor, per priority: a FIFO with a bitmap of non-empty queues, so
choosing the next thread is a bit scan rather than a list walk.

```text
dispatch(processor):
  1. If the current thread is still runnable and nothing higher is ready, keep it.
  2. Pick the highest non-empty queue, take its head.
  3. If the pick is not the current thread, save the outgoing context and switch.
  4. If no queue is non-empty, enter the idle loop for this processor.
```

| Rule | Contract |
|---|---|
| Affinity | A thread has a 64-bit affinity mask; the default is "any processor", and a job may narrow it. A thread is never migrated off its mask, even by a starvation boost. |
| Balance | A wake, a priority change and a new thread all consider push migration to a less loaded processor, using runnable-thread count first and CPU utilisation second. Migration is never initiated by the *victim* processor's idle loop pulling work that has an affinity elsewhere. |
| Preemption | Immediate on a wake that outranks the current thread; deferred to the next quantum boundary otherwise. |
| Idle | The idle thread halts in the deepest C-state the platform permits ([PLATFORM.md](PLATFORM.md#6-power)), and the timer programs the next deadline rather than a periodic tick. |
| No lost wakeups | A wake that races with a switch is resolved by a per-processor reschedule IPI **and** a re-check after interrupts are enabled; the pair is the invariant a soak test hunts. |

## 4. Waiting and wait reasons

A blocked thread records **what it is waiting for**, in a fixed enumeration:
object handle and type, timer, I/O, page-in, suspended, user request, job
accounting, pagefile, executive resource, mutex. `ps -a` prints it, a stuck
machine prints all of them, and the 24-hour soak fails if any thread has been
blocked longer than the test's threshold with the same reason
([RELIABILITY.md](RELIABILITY.md#2-hung-task-detection)).

Dispatch levels, from the platform up:

| Level | Runs | May block | May take a spinlock | May page |
|---|---|---|---|---|
| `PASSIVE` | Ordinary kernel and user code | Yes | Yes (ranked) | Yes |
| `DISPATCH` (DPC) | Deferred work, timer callbacks, I/O completion | **No** | Yes (ranked) | **No** |
| `DIRQL` (ISR) | Interrupt service routines | **No** | Only its own device lock | **No** |

A call that blocks at `DISPATCH` or `DIRQL` is a bug and panics with the
offending symbol. The pool allocator refuses a paged allocation above `PASSIVE`
for the same reason ([VMM.md](VMM.md#7-the-two-allocators-and-why-there-are-two)).

## 5. Synchronisation

| Primitive | Where | Rule |
|---|---|---|
| Spinlock | Kernel, `DIRQL`..`PASSIVE` | Ranked; the rank gate refuses an inversion before it deadlocks ([as built](#8-as-built-the-gap)) |
| Mutant (kernel) | Kernel, `PASSIVE` only | Owns a thread; recursion counted; **priority inheritance** so a low-priority holder is boosted to its highest waiter |
| Push lock | Kernel, short non-blocking sections | Shareable/exclusive, no ownership |
| Executive resource | Kernel, long read-mostly sections | Reader/writer, supports wait and timeout |
| User primitives | `PASSIVE` | Mutant, semaphore, event objects ([OBJECTS.md](OBJECTS.md#6-waiting)) |

Every kernel lock declares its rank. The gate is the existing one: a processor
may only acquire a lock of strictly higher rank than any it holds, so an
inversion cannot be expressed. Priority inheritance covers the user-visible
case the rank gate cannot see, because a user mutant is taken at `PASSIVE` where
another thread may preempt the holder.

## 6. Timers

- The platform chooses one **clock of record**: HPET when present, otherwise the
  local APIC timer calibrated against the PIT, otherwise the PIT
  ([PLATFORM.md](PLATFORM.md#4-timers)). The clock never changes at run time; a
  later, better clock changes the *simulated* tick, not the source.
- Every processor has a **deadline queue** (a min-heap keyed by due time). The
  next deadline programs the hardware; there is no periodic tick when the system
  is idle.
- Deadline resolution: **1 µs** guaranteed, **100 ns** where the clock's
  calibration supports it, printed by `acpi` and by the boot log.
- A timer **object** ([OBJECTS.md](OBJECTS.md#6-waiting)) is one-shot or periodic,
  optionally signalling a port on expiry. A periodic timer is coalesced within
  1 ms by default so a thousand of them do not thrash the machine; the tolerance
  is per timer.
- Sleeping is a deadline wait, never a counted spin: `SYS_SLEEP` keeps working
  ([S3](README.md#decisions-this-set-records)) and is implemented over one.

## 7. Jobs

A **job** groups processes for policy and accounting:

| Limit | Meaning | Default |
|---|---|---|
| Processes | Maximum live processes | Derived from the token, not a constant |
| Memory | Committed bytes | Derived |
| CPU rate | Percentage of one processor over a window | No limit |
| Handles | Total handles across the job | Derived |
| UI | Whether the job may own a window, a clipboard, a session | Yes for interactive tokens |
| Security | Whether the job may change identity | No |

Exceeding a limit fails the operation that would cross it; it never kills blindly.
Accounting is hierarchical: a job's usage is the sum of its processes and their
threads, and a job's completion port receives a message when the last process
exits ([IPC.md](IPC.md)).

## 8. Accounting and observability

| Counter | Where | Test |
|---|---|---|
| CPU time per thread/process/job | ns, exact | Sum of threads equals the process total |
| Context switches per second | Per processor | Reported by `sched` |
| Wait time per reason | Per thread | Hung-task detection |
| Ready-queue length | Per priority per processor | Load balancing decisions |
| Boost count, starvation events | Global | Starvation guard fires and is recorded |

`sched` prints the table above. Nothing here is sampled or estimated: a counter
that is approximated cannot be used to prove a leak, and a leak is what the soak
gate looks for.

## 9. Latency targets

Measured on a Pentium-class model under QEMU, printed by the benchmark suite:

| Operation | Target |
|---|---|
| Syscall round trip | < 1 µs |
| Context switch | < 5 µs |
| Wake-to-run | < 20 µs |
| fork + exec | < 5 ms |
| Timer accuracy | within 1 µs of the deadline |
| Idle power draw | unmeasurable in QEMU; C-state entry is counted instead |

## 10. Conformance

M2 does not close until:

1. Four virtual processors run mixed load for **24 hours** with no panic and no
   lost wakeup (the existing soak gate, extended to every processor running
   work rather than parked).
2. `sched_test` proves priority order, the starvation boost, affinity and
   migration, and that a real-time thread cannot be starved out by a variable
   one.
3. `mutex_test` proves priority inheritance: a boosted holder finishes, and the
   waiter runs at its own priority afterwards.
4. `timer_test` proves deadline accuracy to 1 µs over 100 000 expiries, with
   coalescing bounded by the tolerance.
5. Accounting returns to zero for every counter after the suite, and the sum of
   thread time equals the process time to within 1 µs.
6. No thread is blocked longer than the threshold with the same wait reason
   during the soak.

## 11. As built (the gap)

| Contract | Tree today |
|---|---|
| 64 processors, all scheduling | 8 slots; application processors start and then park |
| 32 priorities in three classes | 4 constant levels |
| Per-processor run queues | One global queue |
| Deadline timers, tickless idle | 100 Hz tick from the PIT or the local APIC timer |
| Priority inheritance | None; rank-ordered spinlocks only |
| Jobs and quotas | None |
| Wait reasons, hung-task detection | None |
| Nanosecond accounting | Tick counts |

The lock-rank gate, the AP start-up path, the APIC timer and the SMP boot are
real and tested; this document says what they grow into.
