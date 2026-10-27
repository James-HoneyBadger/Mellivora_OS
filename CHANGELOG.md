# Mellivora OS - Changelog

## Unreleased - M3: an ACL decides where the mode word cannot

The v3 permission word answers one question per file: owner, group, world. That
is not enough to hand one file to one other user without handing it to their
whole group, so a file may now carry an **access control list**, and where one is
present the ACL decides and the mode word is not consulted:

```text
acl set report.txt u2000 7    a named user may read and write
acl set report.txt other 0    nobody else may
acl set report.txt mask 7     and the mask does not narrow it
acl show report.txt
acl:u2000 = 7
acl:o = 0
acl:m = 7
```

- An ACE is one entry in the file's existing xattr block - `acl:u<uid>`,
  `acl:g<gid>`, `acl:m`, `acl:o`, each holding a permission digit - so no
  on-disk structure changed to add ACLs, and a file carrying one is still a v3
  file to a reader that knows nothing about them. `acl:o` is what makes an ACL
  present.
- `acl_perm` runs inside `hbfs_check_access` before the mode word, in POSIX's
  order: root, the owner (through their ACE when they have one, else the owner
  triplet), a named user, the caller's group, then `acl:o`; the mask narrows the
  named-user and group cases only. A corrupt ACE value denies rather than
  grants.
- `programs/acl_test.asm` is the conformance suite, and it has to fork: a
  process cannot setuid its way back up, so each question is asked by a child
  that drops to the uid in question. It proves a named user writes a 0600 file
  the mode word makes read-only for them, that the mask narrows the grant
  without touching the ACE, that the owner is held to their own ACE, that a
  stranger falls to `acl:o`, and that root still passes.

Inheritance and group-class unions are the ACL work that remains.

## Unreleased - the shell learns that a path is a name plus a place

`touch /overq` was fixed to stop creating an entry whose *name* was the whole
path. Probing the rest of the family showed the same mistake in more places, and
a worse one behind it:

- `mkdir /dir/name` created an entry named `/dir/name` - findable by nothing;
- `ls`/`dir` ignored their argument and always listed the current directory, so
  the only way to look inside a directory was to `cd` into it;
- `cd /dir` could not enter a directory that `mkdir /dir` had made, because the
  name it had made was not the name `cd` was looking for;
- `del` matched names in the current directory only, and `del -v name` deleted
  nothing at all: `parse_flags` records the flags but restores its input
  pointer, so the first word it saw was still the flag.

One path splitter (`path_split`) now separates a path into its directory part
and its final component; `path_enter`/`path_leave` wrap a command in a change of
directory and always restore it, and `skip_flag_words` steps over flags for the
commands that also take an argument. `touch`, `mkdir`, `ls`/`dir` and `del`
(`-v` and wildcards included) all go through it.

Two bugs surfaced on the way:

- a failed directory read left the block cache's tag naming the directory it had
  just failed to read, so the *next* load "hit" and returned an empty directory -
  every file appeared to have vanished. The tag is now invalidated on both error
  paths;
- entering a virtual directory (`/tmp`, `/proc`, `/dev`) and touching an entry
  there meant a block read at LBA `0xFFFFFFFF`. Those locations are read-only and
  now say so: `mkdir /tmp/x` answers "Virtual directories are read-only".

`tests/test_runtime.sh` covers each of these: a create through a path, a listing
of one, a flagged delete through one, the virtual-directory refusal, and a
volume that is still intact after that refusal.

## Unreleased - the durability gate was failing on a correct volume

`make check` started failing at `check-durability`, intermittently, at
`fs_mkdir_test`. The report was an undo journal doing its job:

```text
ERROR block 127 (/crashme.tmp) is referenced but not marked allocated
WARN  journal is dirty (state 1, pointer 0); mount will replay it
```

A crash in the middle of a transaction *is* a half-written volume on the disk -
that is what the journal is for - and the old harness treated fsck's verdict on
that half-written state as the assertion, so it failed whenever the crash landed
inside a transaction. It now asserts the two things that are actually true:

- **before replay**, a failed fsck is acceptable only if the volume is marked for
  replay. Damage with a clean journal is the failure that matters, because no
  replay can repair it - a write that landed without its undo record;
- **after replay**, fsck must pass. That is the assertion.

`touch` also learned that a path is not a name: `touch /overq` used to create an
entry whose *name* was "/overq" - a file no other command could find, and an fsck
warning that would have survived every rebuild. It now places the last component
in the current directory (and, once the path splitter landed, in the directory
the path names).

## Unreleased - M2 closes: the SMP stall was a job that went missing

`make check-smp` used to stall intermittently: a round of `smpstress` would wait
for jobs that never arrived, with every application processor halted. It was a
data race, and it took three bugs to get to it.

- **The queue returned jobs through a static slot.** `smp_work_pop` wrote the
  job it had taken into a per-function static, released the lock, and then read
  the slot back. Two processors popping at the same moment therefore overwrote
  each other's result: one job ran twice, the other was lost, `smp_jobs_run`
  stopped rising and the waiting processor waited forever. The job now travels
  in a register, which is the only thing a reentrant function can safely use.
- **`smp_delay_ms` read the shared PIT from every processor.** The latch command
  and the two count bytes belong to the whole machine, so four processors
  reading them at once could each get half of somebody else's reading - and the
  diagnostic that fires after "no progress" was measuring that instead of a
  stall. Delays inside jobs now use the per-processor time-stamp counter
  (`smp_delay_tsc`); the PIT stays for the boot path, where the caller is alone.
- **The AP idle loop lost wakeups** between the empty-queue check and `hlt`.
  Already fixed when the queue was written; it is listed here because the stall
  could not be reproduced reliably until all three were gone.

With those out of the way the gates hold: `make check-smp` - four processors,
kernel jobs on the APs and user programs on the BSP, one minute - is part of
`make check`, and soaks of one, two, three and six minutes completed with every
round finishing and the machine still answering. `SOAK_MINUTES=1440 make soak`
is the 24-hour exit run, and has not been performed; the wave closes on the
compressed gate plus the recorded long-run command.

## Unreleased - M3: the disk has a partition table, and quotas that cannot drift

- **The volume manager.** HBFS was assumed to start at LBA 4096 and the volume
  holding it was never looked at. `kernel/volume.inc` reads the table instead -
  the MBR's four primary entries, or the GPT header and entry array behind a
  protective MBR - and records each volume's start, length, type, GUID and
  UTF-16 name. The filesystem is *probed*, not assumed: the superblock LBA is
  checked to fall inside a volume before a sector is read, so a probe cannot
  touch somebody else's sectors. `volumes` prints the table and says which
  volume the filesystem came from. On the profile image: one MBR partition at
  LBA 1, 131 071 sectors, type 0x83, with HBFS at +4096.
  Worth knowing before the v5 work: `populate.py` writes **v3** volumes, so the
  v4 reader path is exercised by tests that format one, not by the image.
- **Per-user quotas, derived rather than stored.** Blocks are accounted per uid
  with a ceiling (0 = unlimited), checked before a write allocates anything so a
  refusal leaves no half-created file, charged when the blocks become the file's,
  and returned to the file's *owner* on delete rather than to whoever deleted it.
  The ledger is recomputed from the tree at every mount and by `quota rebuild`,
  which is why a power cut cannot leave the numbers disagreeing with the disk:
  there is no second copy to disagree with.
  Threshold: `quota set 0 1` then `touch /overq` fails with "Write error!",
  `quota` counts the refusal, `quota set 0 4000` lets the same write through, and
  `del` returns the block - all four are runtime cases.
- **Two bugs the work found:** the first walk read subdirectories at their raw
  entry block number, which is *data-area relative*, so it walked the boot sector
  as if it were a directory and filled the ledger with nonsense; and the ledger's
  own first build ran only on the v4 mount path, so a v3 volume reported nothing.

## Unreleased - M2: the machine answers for itself

- **ACPI is taken over, and the machine can be stopped.** The FADT is parsed -
  PM1a/PM1b event and control blocks, PM_TMR, SCI_INT, SMI_CMD, RESET_REG, FACS -
  and the kernel asks the firmware to hand ACPI over by writing `ACPI_ENABLE` to
  `SMI_CMD` and polling `SCI_EN`, rather than assuming. `shutdown` writes the
  machine's own S5 encoding to PM1a control, so it powers off instead of halting
  with the message "ACPI could not power the machine off"; `reboot` uses the
  FADT's reset register first and falls back to the keyboard controller.
- **The S5 encoding comes out of the DSDT, not out of a table of guesses.** It is
  per-machine, and the only place it is written down is AML: `Name (_S5,
  Package (0x02) { 0x00, 0x00 })`. A scanner that understands `NameOp`,
  `PackageOp`, the length field and the five integer encodings decodes it - 120
  lines, no interpreter. On the profile machine it reports `S5 sleep type: 0`,
  which is what the write to port 0x604 then uses.
- **The power button works.** The SCI from the FADT is routed through whichever
  interrupt controller is active, `PWRBTN_EN` is set in PM1 enable, and a press
  acknowledges the fixed event, queues deferred work, syncs the filesystem and
  powers off. `programs/`-free test: the harness presses the button with
  `system_powerdown` and requires QEMU itself to exit.
- **The desktop shuts down.** "Shut Down..." opens the Windows 2000 dialog -
  Shut down, Restart, Command prompt, Cancel - and the first two go through the
  same ACPI paths as the shell. Ctrl+Esc opens the Start menu, arrows and Enter
  work through it, and a fly-out no longer traps the arrow keys (which is what
  made the last item unreachable from the keyboard).
- **A boot log, because a machine that fails to enumerate has to say why.** A
  256-record ring of level, processor, tick and text, mirrored to COM1 once the
  serial port is up, readable with `bootlog`. It takes no blocking lock: a
  writer that finds the ring busy drops the record and counts it, because a
  diagnostic that can deadlock the machine it is diagnosing is worse than one
  that loses a line.
- **The driver model.** Drivers are records with a match table, attach, detach,
  power and an order, registered through a validated module header, matched
  against a device tree built from PCI enumeration and the platform, with
  resource arbitration (I/O, memory windows, interrupt lines), two binding
  phases around the frame allocator and PCI enumeration, and a `drivers` command
  that says which driver owns which device and what it was refused. All thirteen
  in-tree drivers are behind it; nothing in the boot path calls a driver
  directly any more.
- **Per-CPU state, and application processors that work.** Each processor gets a
  block behind GS (`IA32_GS_BASE`) with its index, idle flag, job count and
  interrupt count; APs load the kernel's IDT, enable their own local APIC and
  leave the old park loop for a work loop fed by a ranked-lock queue and an
  inter-processor interrupt. `smpstress` queues allocator work and reports what
  each processor ran: 32 jobs spread 11/11/10 over three APs, 14 195 frames
  churned through the shared allocator, zero errors. The block cache gained its
  ranked lock, which makes the "static local" scratch slots in its read path safe
  again.
- **Three SMP bugs, all of them real, all found by the harness:**
  - the driver record was declared 48 bytes and had 40, so every record after
    the first was misaligned - a triple fault on the first boot with the model;
  - PCI interrupt lines were refused as duplicates, which would have starved
    three of four drivers sharing IRQ 11. A level-triggered PCI pin is shared by
    design; an ISA edge is not;
  - the AP idle loop lost wakeups: an IPI arriving between "queue is empty" and
    `hlt` was consumed by a handler that did nothing, leaving a halted processor
    with work queued behind it. The check and the halt are now indivisible
    (`cli` around the check, then the `sti; hlt` pair).
- **Known gap, recorded rather than closed:** `make check-smp` (the one-minute
  soak) still stalls intermittently - a round of `smpstress` occasionally waits
  for jobs that never arrive, with every AP halted. A `SMP_WORK_STALL` line names
  the queue state when it happens. M2 is not marked closed until that gate is
  green and the 24-hour soak has run.

## Unreleased - M1: objects are counted, capped and visible

- **A process may hold 256 live objects, and the refusal is a refusal.** The
  slab had one global size and no accounting, so one task in a loop could take
  every descriptor in the machine and every later create failed with an
  unrelated status. `obj_alloc` now charges the creating process and `obj_destroy`
  returns the charge; at the ceiling the create fails cleanly and the slab entry
  it had already taken is returned first, so a refusal costs nothing.
  `programs/objquota_test.asm` proves the ceiling, the refusal, and that closing
  brings the count back. 256 rather than 4096 because a *named* object costs
  about 90 ms to create (a linear insert into the namespace) - that is a defect
  of the namespace, recorded in [OBJECTS.md](docs/spec/OBJECTS.md#12-as-built),
  not a reason to make the gate a four-minute test.
- **`objects` and `handles`, the two commands §12 asked for.** `objects` prints
  live and peak counts per type plus the per-process ceiling, `handles` prints
  live handles per task and a total. Both read counters the kernel already kept.
- **A file is an object.** Every successful `SYS_OPEN` now allocates a `File`
  object named after the path, and the descriptor owns the reference; a
  `SYS_CLOSE` drops it and task exit drops the rest. The frozen fd path is
  untouched - `SYS_READ`/`SYS_WRITE` still take a descriptor - which is the
  compatibility alias the objects spec describes. `programs/fileobj_test.asm`
  is the gate.
- **The reference count follows the handle, not the memory the handle lives in**,
  and the first test to fork with a file open found both places it did not:
  - A task that exits never switches out, so its private descriptor page still
    held the handles it had closed during its life. Draining that stale page
    dropped references for handles already gone and freed objects another task
    still held - which surfaced as `OBJ_UNDERFLOW` in the parent's close, and
    would have been a use-after-free anywhere objects were shared. `vfs_save_page`
    flushes the live tables into the page before the exit path drains it.
  - A new task is created with a copy of its creator's descriptor tables, so it
    owes a reference per File object in the copy. `vfs_ref_all` takes them, and
    `fork` calls it too; without it a child closing an inherited descriptor
    destroyed the parent's file.
  - `fd_isolate_test` now passes: the parent's descriptor survives the child
    closing the same number, and the object is destroyed exactly once.

## Unreleased - M1: a wait can be completed by another task

- **The cross-task wake works, after three fixes it exposed.** This was the one
  part of the waiting contract M1 could not claim: a signal released a waiter in
  *the same* task and nothing ever tried the other case. `programs/crosswait_test.asm`
  now does, in both directions, with the child finding the object by name
  because a fork starts with an empty handle table. What it found:
  - `obj_satisfy_waiters` passed the evaluation result (the constant 1) to
    `obj_wait_consume` where the wait set belonged, so the consume walked the
    object array at address 1 and the kernel faulted at `0x54F000FF`.
  - `obj_yield_cpu` treated "no task is READY" as "nobody can ever signal me"
    and returned `WAIT_TIMEOUT` immediately. A waiter whose signaller is asleep
    was told it had timed out microseconds after blocking. It now idles with
    interrupts enabled and re-scans; the immediate timeout survives only for a
    machine with no other task at all.
  - `sys_waitpid` blocked without clearing `TCB_WAKEUP`, so an expired deadline
    from an earlier `SYS_SLEEP` made the tick scan wake it on the next tick and
    the parent returned the `EAX` it blocked with - 92, its own syscall number -
    instead of an exit code. `sched_wake_sleepers` now also clears the deadline
    when it wakes a task, so the stale-deadline class is gone.
- `crosswait_test` joins `make check-runtime`; `wait_test` keeps the
  single-task cases and says so.

## Unreleased - M0 measured the machine, not just the build

- **A guest benchmark, because the Latency and Footprint gates named numbers
  nothing measured.** `make bench` times host make targets. `programs/perf_test.asm`
  now runs inside the guest and reports TSC cycles for a syscall round trip
  (~480), a yield with a second task ready (~32 000) and fork+exec+waitpid
  (~72 000 000), plus the free-frame counts that give the boot footprint
  (8 320 KB reserved against the 16 MB gate). `tests/bench_guest.sh`
  (`make check-perf`, part of `make check`) compares them with
  `benchmark/guest-<profile>.txt` and takes the best of three samples, because a
  single sample on a shared host measured up to 3x slow through no fault of the
  tree. Units are cycles rather than microseconds on purpose: the TSC rate is
  not architectural, nothing in the guest publishes it yet, and measuring it
  against a tick gave 163-6427 cycles/us on one machine. Microseconds arrive
  with M2's clock of record.
  The first measurements already say something uncomfortable: fork+exec+waitpid
  is ~72 000 000 cycles, nowhere near the roadmap's 5 ms target, and that is now
  a recorded number instead of an assumption.
- **`io_print_u32` prints numbers through `SYS_PRINT`.** Formatting a digit at a
  time with `SYS_PUTCHAR` put numbers on the screen while their labels went to
  COM1, so no transcript of a program was readable. `SYS_PRINT` also writes to
  the console now, not only to COM1, so a program's text is visible where it
  prints.
- **The toolchain probe compiles with the flags the build uses**
  (`-march=pentium4 -mtune=pentium4 -mno-sse ...`). At the i486 floor the
  default flags were a subset and the probe proved nothing about the interesting
  half; at the Pentium 4 floor a compiler that rejects `-march=pentium4` would
  have passed the probe and failed a minute into the build.
- **Docs corrected where M0 had drifted**: the FPU bullet no longer describes an
  `fnsave` path that is gone, the ceilings table's as-built cells now match the
  tree (201 syscalls, a 704-byte TCB, the handle table, the demand zone), the
  "specs still owed" line is retired because all three were written, and ABI.md
  declares v2 with 187-200 frozen by number.

## Unreleased - the platform is a Pentium 4

### Machine and CPU floor

- **The kernel now requires a Pentium 4 and says so.** `kernel/cpu.inc` reads
  CPUID properly - vendor, family/model/stepping through the extended rules,
  the feature bits, and the 48-byte brand string from leaf `80000004H` - and
  `cpu_require_pentium4` stops the boot with one legible line when FXSR, SSE or
  SSE2 is missing:

  ```
  CPU_UNSUPPORTED: Mellivora needs a Pentium 4 or later (fxsr+sse+sse2); found family 4 model 8 - halting
  ```

  On an accepted part the first thing on COM1 is
  `CPU_P4 Intel(R) Pentium(R) 4 CPU 2.40GHz family=15 model=2 step=7 fxsr=1 sse=1 sse2=1`,
  and `tests/test_boot.sh` requires it.
- **The x87-only fallback is gone.** `fnsave`/`frstor` and the
  `cpu_has_fxsr` branch in `sched.inc` are deleted; every task image is a
  512-byte `fxsave`, and `CR4.OSFXSR`/`OSXMMEXCPT` are set unconditionally. Two
  save formats that have to agree was a bug class, not a feature — the boot gate
  now requires `FPU_SELFTEST_OK`, and the round trip really runs on every boot
  instead of printing `FPU_SELFTEST_SKIP`.
- **Kernel C is compiled for the Pentium 4** (`-march=pentium4 -mtune=pentium4`)
  while keeping `-mno-sse -mno-mmx -msoft-float`: XMM state belongs to tasks,
  and a ring-0 C ABI that also used it would give the per-task image a second
  owner.
- **One definition of the machine.** `tools/machine_profiles.py` (with
  `tools/machine_profile.sh` for shell callers) defines the CPU, RAM, chipset and
  guest devices, and the Makefile, both shell test suites, the desktop test, the
  QEMU drivers, the GDB launcher and the installers all read it from there.
  Three profiles: `p4` (default — 512 MB, AC'97, USB 1.1, RTL8139), `p4-noapic`
  (the same part with APIC removed, for the 8259 path) and `i486` (legacy; must
  be refused at boot). `MV_PROFILE` selects one, so `make run` and `make check`
  cannot drift apart.
- **Every frame is identity-mapped, so 512 MB works.** `paging_init` now fills
  `ceil(mem_top / 4 MB)` page tables 1:1 (256 tables, 1 MB, `0x411000..0x511000`)
  instead of the first 32, and the kernel no longer asks the fault handler to map
  its own RAM. This closes a real bug: above 128 MB the buddy allocator wrote
  free-list links into unmapped frames, the fault handler called the allocator for
  a page table, and the allocator wrote into another unmapped frame — a triple
  fault whose first victim was an application processor's start-up. The PMM bitmap
  and tag array moved up to make room (`0x620000`/`0x640000`), the user demand zone
  runs `512 MB..1 GB`, and `USER_DEMAND_MAX`/`USER_DEMAND_BASE_MAX` follow.
- **`mmap` treats its hint as a hint.** An address below the demand zone (which
  moves with the amount of RAM), unaligned, out of range or already occupied now
  falls back to the allocator instead of failing the call — there is no `MAP_FIXED`
  to promise anything. `vmtest2` discovers a usable address first, so the VMM gate
  passes at 128, 192, 256 and 512 MB.
- **Architecture strings follow the machine**: `uname` reports `i686`,
  `sysinfo` prints the CPU brand and family/model it actually detected, and the
  banners, `neofetch`, `bsysmon`, `pkginfo` and the desktop's about box say
  Pentium 4 rather than i486.

## Unreleased - the executive: objects, handles, waits and security

### Kernel (roadmap M1)

- **A resource is now a typed object with a header.** `kernel/object.inc` gives
  every object the 64-byte header and 512-byte body [OBJECTS.md](docs/spec/OBJECTS.md)
  fixes, and a type is a record in a table - name, rights mask, flags, close
  routine - so naming, securing, counting and closing read data rather than
  branching. Eight types exist: directory, event, mutant, semaphore, timer,
  token, process and symlink. `c/include/mv/objects.h` asserts every offset, so
  a field inserted in the middle of the header is a compile error in the C
  module rather than a kernel reading one field as another.
- **A handle carries rights and a generation.** A handle is
  `(generation << 18) | (index << 2)`; the low two bits are zero so a handle is
  never a small integer, the index is 16 bits for the 65 536 ceiling, and a
  closed slot remembers its generation so its old number is refused instead of
  reaching whatever took the slot next. Rights are enforced at the operation,
  not only at open, and a duplicate may only narrow.
- **One namespace.** `\`, `\KernelObjects`, `\Devices`, `\RpcControl` and
  `\Sessions\{0,1}\BaseNamedObjects`, looked up one segment at a time with
  case folding, traverse required on every directory crossed, and a session
  boundary at `\Sessions\<n>` that `SE_SESSION_CROSS` crosses.
- **Wait sets.** `WaitOne`, `WaitAny` and `WaitAll` over up to 64 handles, with
  a timeout of nought meaning poll and a longer one blocking, FIFO queueing per
  object, and an auto-reset signal consumed by exactly one waiter. A timeout is
  delivered as a *result* by the timer's sweep, so a woken task never returns
  the value it blocked with.
- **Tokens, descriptors and one access check.** `obj_access` walks a DACL in
  canonical order with deny before allow, evaluates the mandatory no-write-up
  rule before the DACL, maps `MV_RIGHTS_ALL` per type, and consults the bypass
  privilege. Identity comes from the TCB that `setuid` already maintains, so
  there is no second copy to disagree with the frozen syscalls.
- **A deferred-work queue.** `kernel/dpc.inc` is a FIFO of routines drained at
  the point where running kernel code is safe; the timer's wait sweep is its
  first user, and a boot self-test proves the order and the empty drain.
- **Fourteen new syscalls (187-200).** Object create/open/close/dup/query, the
  three waits, the operations that signal a waitable object, and the two
  read-only views a test needs. A failure returns -1 in EAX and an `mv_status`
  in EDX, which is the one place a new caller learns *why*.

### Tests

- `osmoke` - the object manager answers: create by name, reopen by name, signal
  through one handle and wait through the other.
- `object_test` - rights never amplify, a stale handle and a forged handle are
  refused, the type counters come back to where they started, another session's
  namespace is refused, and a write into a SYSTEM-integrity directory is
  refused *even though its DACL allows it* - which is how the mandatory rule is
  shown to be the rule that refused.
- `wait_test` - poll versus block, `WaitAny` by index, `WaitAll`, an auto-reset
  signal consumed by one waiter, and the 64-handle limit.

### Found and fixed while building it

Four bugs in the new code, all of the same two shapes, and worth recording
because they are what a hand-written assembly ABI makes easy:

- `xor eax, eax` after `stc` clears the carry flag, so `ht_entry`'s failure path
  reported *success* with a null entry, and the caller read address zero.
- A result computed inside a `pushad` frame is thrown away by `popad`. Both the
  handle from `ht_open` and the index from `obj_wait_eval` were returned that
  way, and both callers got their own argument back instead.
- `HE_GEN` is a word at offset 6 of an 8-byte entry; comparing it as a dword
  made every handle look stale, because the high half was the next entry's
  object pointer.
- Initialised data placed below `bss_start` is zeroed at boot. The namespace's
  strings and type table landed there and read as empty strings, so the first
  named object could not be created - a boot-time failure with no message
  because the message was in the same region.

## Unreleased - the kernel carries a module written in C

### Kernel (roadmap M0)

- **A freestanding C toolchain is part of the build.** `tools/find_c_toolchain.sh`
  now writes make assignments (`--make`) as well as a report, and the Makefile
  reads them: `C_CC` and `C_LD` come from a probe that compiles a 32-bit object
  and links it, so a native compiler on a 64-bit host is rejected rather than
  trusted. The build stops with the probe's own guidance when neither is found;
  `make check-toolchain` prints what was found. CI installs `clang`, `lld` and
  `binutils`.
- **A C-built kernel module runs.** `c/kmod/kmod.c` is linked as a flat blob and
  assembled into the image at an address the assembler measures: `kernel.bin` is
  assembled once with `-DCMOD_ABSENT` to find the slot, the module is linked at
  that address, and the image is assembled again with `-DCMOD_ADDR`, which pads
  to the slot, `incbin`s the blob and refuses to assemble if the address has
  gone stale. The slot sits after the BSS and is capped at 32 KB by an
  assembler assertion. New `cmod` built-in runs its self-test — header check,
  host table check, the freestanding runtime and a call back into assembly —
  and prints `CMOD_OK`; `make check-cmodule` reads the image back and requires
  the `CMOD` header where the build says it is.
- **A C-built user program runs.** `c/prog/cprog.c` is linked at
  `PROGRAM_BASE` with `--oformat binary`, entry first and `.bss` folded into a
  PROGBITS section so the reused program window cannot hand it the last
  program's leftovers. It links the same runtime, checks it, checks compiled
  arithmetic and the syscall path, and prints `C_PROG_OK` — the "C user
  program" case in `make check-runtime`.
- **The freestanding runtime** is `c/rt/string.c`: `memcpy`, `memmove`,
  `memset`, `memcmp`, `strlen`, `strcmp`, `strncmp`. These are not optional —
  with `-O2` the compiler emits calls to the first four for structure copies.
- **`c/include/mv/syscall_numbers.h`** is generated from `programs/syscalls.inc`
  by `tools/generate_syscall_header.py` and checked by
  `make check-syscall-header` (in `make lint`), so a C program and an assembly
  program cannot disagree about a syscall number.
- **Per-task SSE state.** Each TCB now carries a 512-byte image instead of a
  128-byte one, and every switch path uses `fxsave`/`fxrstor` where `CPUID`
  advertises FXSR and `fnsave`/`frstor` where it does not. `kernel/cpu.inc`
  probes CPUID (guarded by the EFLAGS.ID bit, which is not on every 486) and
  sets `CR4.OSFXSR`/`OSXMMEXCPT` only where the state exists; the initial image
  `sched_init` captures is in the same format, because a zeroed `fxsave` area
  would unmask every SSE exception. `TCB_SIZE` is 656 bytes, still 16-byte
  aligned.
- **`fpu` proves the image round-trips.** Two distinct x87+SSE states are saved,
  the live registers disturbed, and each loaded back and compared; prints
  `FPU_SELFTEST_OK`, `FPU_SELFTEST_FAIL` or `FPU_SELFTEST_SKIP` (no FXSR). The
  boot path reports a failure on COM1 and stays quiet otherwise. New
  `programs/sse_test.asm` is the per-task half, alongside the existing
  `programs/fpu_test.asm`.
- **Kernel C stays `-mno-sse -mno-mmx -msoft-float`,** now because the CPU
  floor is an i486 rather than because nothing saves the state: the kernel has
  to run on a machine that has no SSE at all. `docs/spec/ABI.md` §5 says so, and
  documents the module and program build paths.
- `docs/TECHNICAL_REFERENCE.md` gains a "Freestanding C Subsystems" section, the
  TCB table now shows the 512-byte image, and the memory map shows the module
  slot at the end of the kernel image.

### Documentation

- **The specification set is rewritten as a contract for the system being built,
  not a description of the one we have.** `docs/spec/README.md` is the index: the
  status vocabulary, the document map, the table of ceilings each wave removes,
  and the eight decisions (S1–S8) the rest of the set cites instead of
  re-litigating.
- **Every ceiling a user can hit is gone from the contract.** Live tasks 128 →
  65 536, handles per process 64+128 → 4 096 growing to 65 536, kernel virtual
  space 2 MB → 1 GiB, user virtual space ~1 MB → 3 GiB, physical memory 128 MB →
  64 GiB over PAE, directory entries 224 → 65 535, file size 2 GB → 2^63, block
  numbers 32-bit → 64-bit, program image 1 MB → 3 GiB, processors 8 parked → 64
  scheduling, syscalls 187 → 187 frozen plus a typed object-manager space.
- **New specifications:** `OBJECTS.md` (the Warren: typed objects, generation-
  checked handles with 16-bit rights, one namespace, wait-many, lifetime),
  `SCHED.md`, `PLATFORM.md`, `DRIVERS.md`, `STORAGE.md` (HBFS v5),
  `CONFIG.md` (the Sett), `SERVICES.md`, `EXEC.md`, `IPC.md`, `NETWORK.md`,
  `MEDIA.md`, `SHELL.md`, `RELIABILITY.md`, `ECOSYSTEM.md`.
- **Rewritten specifications:** `ABI.md` (fixed-width boundary types, 8-aligned
  `u64`, versioned structures, an `mv_status` severity space, generated handles,
  255-code-unit UTF-16 names, parameter blocks, a number-space map), `VMM.md`
  (3 GiB/1 GiB split over PAE, a PFN database, a bounded physical map window
  instead of an identity map, reserve/commit, region trees, sections) and
  `SECURITY.md` (SIDs, tokens, privileges, integrity levels with no-write-up,
  ACL and ACE formats, the one access-check algorithm, inheritance, audit).
- **Each document ends with a conformance section naming the tests that must
  fail first**, and an **as built** section that states the distance from the
  tree — so "not done yet" is a table, not an assumption.
- The roadmap gains D9–D12 (the 3 GiB/1 GiB split over PAE, no accidental
  ceilings, the 187 syscalls as a permanent compatibility layer, and specs that
  describe the target) and links every wave's contract.

### Fixed

- **`populate.py` no longer hides a program it could not install.** A program
  that did not fit `/bin` was recorded in the cache as copied, so the next run
  read the gap as a user deletion and tombstoned a program that had never been
  there. `add_file` now reports whether the entry was written and the cache
  records only what landed.
- **Conformance fixtures install at the volume root.** Every program named
  `*_test` now installs at the root rather than in `/bin`, which is at the
  224-entry limit of an HBFS subdirectory (roadmap M3 grows it). Without this
  the alphabetical tail of `/bin` — `fpu_test`, `fd_frag_test`, the v4 test
  programs — silently never made it into the image.
- **A binary built from C is not an orphan.** `populate.py` skipped any `.bin`
  with no `.asm` beside it; a program built from `c/prog/*.c` now counts as
  sourced.
- **`docs/spec/ABI.md` did not describe the tree it governs** — it promised
  `-mno-sse` "until per-task FPU state lands" and described the module boundary
  as a syscall stub. Both are corrected.

### Fixed: two tasks could not run

- **`pmm_alloc_page` did not keep the registers it promised to.** The allocator's
  lock landed in `4ac3a6f` as `push edi … pop edi`, which dropped the `push ecx`
  / `pop ecx` the routine had used to save `ECX` across its own `xor ecx, ecx`.
  Every function in the file documents "All other registers preserved" and
  callers rely on it: `paging_clone_pd` holds the source page-table address in
  `ECX` across the call, so it copied 4 KB **from physical address 0** into the
  child's page table. The result was an address space with no kernel mapped — a
  triple fault on the next `mov cr3`, in `sched_apply_cr3`, in every forked
  task, and in every case that needed a second task. 24 runtime cases were red
  on `4ac3a6f` for this, including `cow_test`, `fpu_test`, `fork + exec`, the
  mmap/vm cases and the background-launch path.

  Found by instrumenting the clone and the switch rather than by reading: the
  tracer printed the new directory's `PDE[0]`, its `PTE` for `0x101000` and the
  master's for the same page, which showed a freshly built table of zeros
  (`07FD3000 07FD4027 00000000 00000000 00101063`) while the master's was
  present. That named the copy, the copy named the clobbered register, and the
  register named the lock's save list.

  The fix is `pushad`/`popad` around the body with the result left in the saved
  `EAX` slot: a hand-written save list is what drifted, so the routine no longer
  keeps one. `pmm_alloc_pages`, `pmm_free_page` and `pmm_free_pages` were
  audited for the same class of bug and are correctly balanced.

- **The contract is now checked at boot, not assumed.** `pmm_selftest` puts a
  distinct value in `ECX`, `EDX`, `EBX`, `ESI` and `EDI`, calls each of the four
  allocation and free entry points, and requires every one of them back — plus
  the free-page count returning to where it started. It prints
  `PMM_SELFTEST_OK` on COM1, and `make check-boot` now fails when any boot
  self-test (`LOCK_SELFTEST_OK`, `PMM_SELFTEST_OK`, `FPU_SELFTEST_OK|SKIP`) does
  not report, so the check cannot be forgotten.

  With this in place the three cases M0's third exit criterion depends on pass:
  `cow_test` (`COW_OK`), `fpu_test` (`FPU_OK`) and `sse_test` (`SSE_OK`).

## Unreleased - a volume is verified before it is trusted

### Filesystem

- **HBFS refuses a volume instead of formatting it.** `hbfs_init` used to format on a
  failed superblock read or an unknown magic, so a failing disk, a cable, or a newer
  filesystem (HBFS v4 uses the `'HBF4'` magic) was destroyed on the next boot. It now
  validates the magic, version (1–3), block size and layout starts, block and free
  counts, and the feature word, then loads the root directory. Any failure prints the
  reason and leaves the volume untouched; `format` is the only way to create one.
- **Nothing reads or writes an unmounted volume.** A new `hbfs_mounted` gate covers the
  journal, metadata writer, directory load/save, allocator, create, mkdir, delete,
  rmdir, truncate, xattr set and defrag, so a refused volume cannot be parsed by `dir`
  or modified by a later command. The explicit, confirmed `format` command is the only
  writer outside that gate, and it reads its new superblock back before reporting the
  volume mounted.
- **Allocation follows the mounted volume.** `hbfs_alloc_blocks` and defrag use
  `hbfs_total_blocks` from the superblock rather than the build's 2 GB default, so a
  smaller image no longer hands out blocks past its end. `format` resets the working
  directory, directory cache and allocation limit along with the new superblock.
- **HBFS v4 is recognised and verified.** The kernel validates the `'HBF4'` superblock's
  version, feature word, geometry and 64-bit counter high halves, and verifies the
  superblock's CRC-32C plus the checksum on the bitmap and on every loaded directory run.
  It mounts writable when the volume declares the journal feature (replaying a dirty log
  first) and read-only when it does not. `populate.py` takes `--version 4`, and
  `make check-hbfs-v4` builds a v4 volume, verifies it offline and boots it.
- **A checksum failure discards the directory buffer.** The read happens before the
  checksum is checked, so a rejected run used to stay in `hbfs_dir_buf` and `dir` would
  still list it. The buffer is cleared on the refusal path.
- **v4 extent files read back.** A file whose v4 entry sets `HBFS4_DE_EXTENTS` stores
  `start` as a block of `{start, count}` runs. `hbfs_map_block` translates a logical
  block through that list, reading the extent block once, verifying its CRC-32C and
  caching it, and `hbfs_read_file` uses it for both path forms — so `cat` returns a
  fragmented file byte for byte. `tests/test_v4_mount.py` adds a three-run file and reads
  it back.
- **The fd layer and the program loader walk extent runs.** `hbfs_map_extent` takes the
  extent block and a logical block and needs no directory entry, so `fd_open` records the
  extent block in the spare fd-table slot and `fd_read` maps each position through it.
  The PATH program loader copies an extent executable block by block into the program
  window, so a fragmented program runs. The new `fd_frag_test` program opens the 18 KB
  three-run file, reads it through `SYS_READ` and checks the trailing marker;
  `tests/test_v4_mount.py` runs it both inline and as an extent-loaded copy.
- **Fixed: descriptor reads and writes of HBFS files used the wrong registers.**
  `vfs_hbfs_read` and `vfs_hbfs_write` moved the buffer into `ESI` and the length into
  `ECX` before calling `fd_read`/`fd_write`, which take `ECX=buffer` and `EDX=length`.
  Every `SYS_READ` of an HBFS file therefore copied `len` bytes to the address `len` in
  low memory and returned success while the caller's buffer stayed empty, and every
  `SYS_WRITE` copied from there into the file. The backends now pass the registers
  straight through; the fd test fails on the old behaviour and passes on the new one.
- **`fd_frag_test`** is a new program; `make image-rebuild` is needed to add it to an
  existing image.
- **HBFS v4 is writable when it declares the journal feature.** Every superblock write
  now goes through `hbfs_write_superblock`, which restamps the CRC first — so the journal
  state word is inside the checksum. `hbfs_meta_write` restamps the bitmap and, when the
  caller's range stops short of it, journals and writes the sector holding the trailer;
  `hbfs_save_root_dir` stamps the directory run before diffing, so the trailer's sector
  is written with the entries that changed. A new subdirectory is 64 KB of zeros, so its
  trailer is the constant `HBFS4_EMPTY_DIR_CRC` (CRC-32C of 65 536 zero bytes) stamped
  into the last block. A dirty v4 journal is replayed at mount like v3. A volume without
  the journal feature still mounts read-only.
- **Directory checksums are verified on every load, not just read-only mounts.** The
  verification was keyed on `hbfs_read_only`, so making v4 writable had silently stopped
  checking the root — the corrupted-root test caught it.
- **Extent files are created, truncated and deleted by the kernel.** When no contiguous
  run fits, `hbfs_extent_alloc` allocates the largest run it can repeatedly, records the
  list, allocates and stamps an extent block, and `hbfs_extent_write` writes the data run
  by run. `hbfs_truncate_extent` keeps whole runs, frees the tail of the run it stops in
  and everything after it, rebuilds the list in place and restamps it. Deleting walks the
  list and frees the extent block. `fragmake` fills a small volume with one-block files,
  deletes every other one and forces the extent path; `v4trunc_test` truncates the
  three-run file to 5000 bytes. The suite checks the extent block, the run list and the
  checksum audit each time.
- **`format` writes a v4 volume.** It used to write version 2 with no feature word, so a
  volume created inside the OS was older than the shipped image, carried no checksums and
  had to be upgraded before permissions worked. It now writes the v4 superblock (extents
  and journal features), a checksummed bitmap with the journal area reserved, a zeroed
  journal and a checksummed empty root, then reads the magic back before reporting
  success. `tests/test_v4_mount.py` reformats a volume, verifies its checksums and creates
  a directory on it.
- **Deleting an extent file walks its run list.** `hbfs_free_extent` reads and verifies
  the extent block, frees every run, then frees the extent block itself, inside the
  caller's transaction; freeing `start` as one contiguous range would have freed the
  extent block and unrelated blocks. Truncating an extent file is still refused until the
  run list can be rewritten and restamped. `v4del_test` deletes the three-run file and the
  suite requires the checksum audit to pass afterwards.
- **`v4write_test`** is a new program that creates a file and a directory on a v4 volume
  and reads the file back; the suite requires the host checker to find every checksum
  valid afterwards.

### Tests

- **Test programs no longer create malformed names.** `perm_test`, `handle_test` and
  `hostile_test` wrote fixtures named `/perm_own.txt`, `/handle_test.txt` and friends; the
  filesystem stored the leading slash, and `fsck` reported every one as a malformed
  entry. They now use bare names, and a filesystem checked after the full suite is clean.
- **`make image-rebuild`** discards the image and the populate cache and builds every file
  again. `make full` preserves an existing volume by design, which is how a damaged image
  survived rebuild after rebuild.

## Unreleased - Burrows wears Windows 2000

### Desktop

- **Windows 2000 shell chrome.** The default `Win2000` theme now drives a teal
  desktop, grey 3-D window frames and controls, and title bars that use the
  Windows 2000 horizontal gradient (dark navy at the left edge to pale blue at
  the right). Inactive titles pick black or light text from the title colour's
  brightness, so the Dark theme stays readable.
- **Start menu hierarchy.** The flat app list is replaced by the Windows 2000
  menu: **Programs** (all GUI applications), **Documents**, and **Settings**
  (Control Panel, Date/Time) open fly-out submenus; **Search** and **Run...**
  open the launcher; **Help** shows About; **Shut Down...** leaves the desktop.
  The vertical `Windows 2000` banner is now drawn rotated 90°, bottom-to-top.
  The menu is keyboard-navigable with the arrow keys, Enter and ESC.
- **Taskbar.** Task buttons stretch to fill the space (up to 160 px) and shrink
  as windows are added; they only show windows on the active workspace. The
  notification tray has a gripper, a workspace box and a 12-hour clock; clicking
  the clock opens Date/Time. The Start flag is the waving four-colour flag.
- **Desktop commands.** Right-clicking the desktop opens a 3-D menu with Open
  BTerm, Open BHive, Refresh, Properties... and Exit Desktop. Desktop icons are
  now hit-tested only when no window covers them.
- **Window manager.** Double-clicking the title bar maximizes/restores a window;
  dragging any edge or corner resizes it (left and top were previously fixed).
  Title-bar drags and edge resizes keep running while a focused application owns
  the frame loop, so windows resize smoothly with an app in front.
- **Dialogs.** Settings is a Windows 2000-style Display Properties view with 3-D
  theme buttons and desktop-colour swatches; About has a separator and an OK
  button; the Clock's Set/Apply/Cancel buttons and the notification toast use
  classic 3-D/balloon styling.

### Tests

- The desktop regression test still covers launch, crash containment, Alt+Tab,
  workspaces and teardown; window geometry (drag, all-edge resize,
  maximize/restore) was verified through the QEMU GDB stub during development.

## Unreleased - locks, and a gate that refuses to deadlock

### Kernel

- **`kernel/lock.inc`**: spinlocks, built on `lock cmpxchg`, with a record of
  which processor holds what. Two processors taking the same pair of locks in
  opposite orders is the classic deadlock, so every lock carries a **rank** and
  a processor may only take a lock of strictly higher rank than any it already
  holds: the inversion becomes impossible to express rather than unlikely to
  happen. Taking a lock of equal rank is refused too - that is a processor
  waiting for itself under another name.
- **The gate is armed, not advisory.** An out-of-order acquisition names both
  locks and stops the machine, because a deadlock found at the point of the
  mistake has a stack trace and one found later has none. Depth limits and
  unbalanced releases are reported the same way.
- **`locks`** runs the self-test on demand, and it also runs once at boot:
  two test locks, taken in the wrong order and refused, then in the right order
  and granted, then a self-acquisition refused, then a release that matches
  nothing refused. It prints `LOCK_SELFTEST_OK` only when every case holds.
  With the order check temporarily bypassed it prints `LOCK_SELFTEST_FAIL`.
- The processor index a lock records comes from the MADT's list matched against
  the local APIC ID, and is 0 on a machine with no APIC - one processor, and the
  local APIC's page is not even mapped there.

### Tests

- `lock order is enforced` in `make check-runtime`.

## Unreleased - the other processors wake up

### Kernel

- **Application processors are started** (`kernel/smp.inc`). Every processor the
  MADT describes except this one gets INIT-SIPI-SIPI, addressed to the APIC ID
  the table gave it. A processor starts in real mode knowing nothing about the
  kernel, so a small 16-bit blob waits at 0x8000: it loads a flat GDT, enters
  protected mode, loads the page directory, and calls into the kernel proper.
  Everything absolute inside the blob is expressed as its run address plus an
  offset, because it was assembled in the kernel image and executes elsewhere -
  a relative `call` there once sent an AP into the middle of low memory.
- **They park.** An AP takes a stack of its own (chosen by APIC ID), counts
  itself in, and halts with interrupts off. Joining the scheduler needs per-CPU
  state, locking on everything shared, and a lock-order gate to prove the
  locking - each a separate increment. A parked AP proves the hard part
  (delivery, the mode change, the shared address space) without putting a
  second thread through code written assuming it is alone.
- **Bounded**: the wait for arrivals is a timeout, so a processor that never
  answers cannot hang the boot. `acpi` reports `processors online: N of M`, and
  the `ACPI_BOOT` line on COM1 carries `online=N`.

### Tests

- `every described CPU comes online`: four vCPUs, all four must check in.

## Unreleased - the tick moves to the APIC timer

### Kernel

- **The local APIC timer drives the tick** on machines that have one, and the
  PIT keeps it everywhere else. Its frequency is not discoverable - it derives
  from the processor's bus clock, which no table states - so it is measured
  against the PIT, whose 1.193 MHz is a constant of the machine: start the timer
  at its largest count, let ten milliseconds of PIT elapse, and see how far it
  fell. QEMU's Pentium measures 8.4 MHz.
- **The measurement is checked before it is trusted.** A rate below 100 kHz, or
  a timer that does not move at all, keeps the PIT rather than running a clock
  nobody can vouch for. Interrupts are off across the measurement, because the
  tick is changing hands and a PIT interrupt arriving mid-way would be counted
  by nobody.
- The PIT's line is masked once the APIC timer takes over, so one tick is one
  tick: two clocks would double every delay in the system.
- `acpi` and the `ACPI_BOOT` serial line now name the clock (`tick=lapic` or
  `tick=pit`), and `acpi` prints the measured rate.

### Tests

- Two more `make check-runtime` cases on a two-CPU Pentium: the tick clock is
  reported as the local APIC timer, and a program that sleeps on tick counts
  and waits for its children still finishes - a clock that never fires would
  hang there rather than fail.

## Unreleased - interrupts follow the hardware

### Kernel

- **`kernel/apic.inc`**: the local APIC and the I/O APIC now carry the
  interrupts on machines that have them. The controller is chosen by probe, not
  by table: the local APIC is enabled and read back, and only when the write
  sticks are the 8259s masked and the redirection entries programmed - a MADT
  describes a machine, it does not guarantee one, and QEMU's 486 advertises a
  local APIC it does not have. A machine without a usable APIC stays on the
  8259, unchanged.
- **Routing comes from the MADT's interrupt source overrides**, which is why the
  PIT arrives on I/O APIC pin 2 rather than pin 0. Lines the table does not
  describe are ISA edge/active-high, except the chipset's PCI link lines (GSI
  10/11), taken as level/active-low until _PRT parsing replaces that rule.
- **`irq_enable` / `irq_disable` / `irq_eoi`** are now the only interrupt-control
  calls in the kernel; the 8259 mask pokes in `pic.inc`, `mouse.inc`, `net.inc`
  and `sb16.inc` all went through them, so both modes agree on which lines are
  live. Drivers run before paging is up, so they record a request and
  `apic_init` applies it.
- **An SMP machine is usable.** With more than one vCPU the firmware routes
  interrupts through the I/O APIC; the kernel listened on the 8259 alone, so
  nothing woke the shell's halt and no key was ever read. Two CPUs on a Pentium
  now boot, take input, run a program and serve DHCP/DNS.
- The boot line and `ACPI_BOOT` on COM1 now say which controller is in charge
  (`irq=apic` or `irq=pic`), and `acpi` prints it as a sentence.
- The APIC's two MMIO windows are mapped through a page table that lives in the
  kernel image. Mapping them the ordinary way - letting `paging_map_page`
  allocate the table - made the high-memory tests fault in data with 192 MB of
  RAM, because the allocated table's frame then travelled through the identity
  mapping the copy-on-write path walks. It is a latent paging bug, recorded in
  `docs/ROADMAP.md`; the APIC window sidesteps it rather than depending on it.

### Tests

- `tools/qemu_drive.py --cpu MODEL`, so a test can choose a machine with a
  local APIC.
- Four cases in `make check-runtime`: the CPU count read from the firmware, the
  APIC path taken on a Pentium, an SMP machine accepting input, and an SMP
  machine serving the network through an I/O APIC entry.

## Unreleased - the machine describes itself

### Kernel

- **ACPI table discovery** (`kernel/acpi.inc`). The RSDP is found in the first
  kilobyte of the EBDA or in the BIOS area, on a 16-byte boundary, with its
  checksum verified; the RSDT is walked, or the XSDT when the RSDP reports
  revision 2 (64-bit entries, low half used - everything below 4 GB). Tables
  above the identity map are mapped supervisor-only as they are read.
- **MADT parsed** for the processor count, the local APIC address (including a
  local-APIC-address-override entry) and the I/O APICs. Nothing is programmed
  yet: this is the inventory the LAPIC, IOAPIC and PnP work builds on.
- **`acpi` command**: RSDP and root-table addresses, revision, the table of
  contents, and the CPU/APIC line.
- **Boot line and `ACPI_BOOT` on COM1** report the same facts. The serial line
  exists because the capability banner fills the screen, and because a machine
  with more than one CPU cannot be typed at yet.

### Found

- **An SMP machine takes no input.** With `-smp 2` or more, the firmware routes
  interrupts through the I/O APIC while this kernel still listens on the 8259,
  so the timer and keyboard go quiet. Confirmed by booting the same image with
  `--smp 1/2/4`: the reported count follows the machine (1, 2, 4) while the
  shell stops echoing. This makes LAPIC-timer and IOAPIC work the prerequisite
  for using an SMP machine at all, not an optimisation.
- Two bugs found by the same work before it ever shipped: the table lookup
  compared the root table's *entry* (a pointer) against a signature instead of
  following it, and the MADT walk started at the 36-byte table header instead of
  the 44-byte MADT header, reading the local APIC address as a zero-length
  entry.

### Tests

- `tools/qemu_drive.py --smp N`, so a test can boot a different machine.
- `ACPI tables are read` and `ACPI counts the CPUs` (2 vCPUs, asserted on the
  serial line) in `make check-runtime`.

## Unreleased - a name belongs to the directory that holds it

### Security

- **Creating in a directory is a write to the directory.** `hbfs_create_file`
  and `hbfs_mkdir` now require the directory's write bit, checked before the
  transaction opens so a refusal journals nothing. Owning a directory is not
  enough when its write bit is clear; root still bypasses, as everywhere else.
  The directory's own mode and owner are read from the entry that names it in
  its parent - the directory stack records the parent - and the root directory
  uses a root-owned `0755` stand-in, since nothing names it.
- **`rename`, `truncate` and `rmdir` had no permission check at all.** Any task
  could rename or empty another user's file, and delete another user's
  directory. `rename` now requires write on the file and on the directory,
  `truncate` write on the file, `rmdir` write on the directory.
- **`programs/dirperm_test.asm`** is the gate. As uid 2000 it owns both test
  directories, so the refusal it demands cannot be a permission accident: a
  create is refused in the `0555` one and accepted in the `0755` one, and a
  file of uid 1000's sitting in its own writable directory cannot be renamed or
  truncated. Forcing the directory check to allow makes it print
  `DIRPERM_FAIL`.

## Unreleased - a handle is not a number, it is a right

### Security

- **Handles carry their rights.** A descriptor opened read-only can no longer be
  written back, and one opened write-only can no longer be read - not even by
  root, for whom the file's mode would allow either. Each VFD entry stores the
  rights its open asked for (`O_RDONLY`/`O_WRONLY`/`O_RDWR`), stamped once in
  `vfs_open`, and `vfs_read`/`vfs_write` check them before any backend runs.
  Sockets get both rights; a pipe's ends get the right that matches the end.
  This is the first piece of the Warren's object model.
- **`programs/handle_test.asm`** is the gate for it. It runs as root on purpose:
  every operation it makes is permitted by the file's mode, so a refusal can
  only come from the handle. Seven checks, `HANDLE_OK` only when each holds.

### Kernel

- **`SYS_SEEK` on a file killed the machine.** The HBFS branch of `vfs_seek`
  ended a comment line with `push ebx`, so the push never assembled: the routine
  popped one slot too many and `ret` jumped into whatever sat above the return
  address (the report read `EIP: 0x00000003`). Nothing had ever seeked a file
  over the VFS before, so the branch had never run. Rewritten with every path
  popping exactly what it pushed, and `.vsk_fail` no longer over-pops when the
  socket check sends it there.
- **`fd_write` and `fd_close` tested the open mode for equality.** A descriptor
  opened `O_RDWR` holds both flag bits, so `cmp ... FD_FLAG_WRITE` refused it:
  a read-write handle could not write, and its size update was skipped on close.
  Both are bit tests now.
- **`vfstest` asked for the wrong flags.** It passed the internal `FD_FLAG_*`
  numbers to `SYS_OPEN`, which speaks the POSIX `O_*` ones; `1` therefore meant
  write-only and its `/dev/zero` and `/proc/meminfo` reads had been refused.
  The test uses `O_RDONLY`/`O_WRONLY` now, and passes.

## Unreleased - signals follow the user, and sleep stops hanging

### Security

- **`kill` has a uid policy.** It delivered to any pid, so a program running as
  one user could terminate another user's task - the one part of M1's exit gate
  that was still missing. A sender may now signal itself, a task of the same
  effective user, or anything when it is root. Every match is checked before
  anything is delivered, so a process-group signal that includes a task the
  sender does not own is refused whole rather than half-delivered.
- **The hostile gate covers both directions:** as uid 1000 it must fail to
  signal a uid-2000 task and must succeed signalling its own child. Sabotaging
  the check makes it print `HOSTILE_FAIL`.

### Kernel

- **`SYS_SLEEP` could hang.** When no other task was ready, the fallback waited
  for `tick_count + EBX` ticks - but the scan that looks for a ready task had
  overwritten `EBX` with a task-control-block pointer, so a parent sleeping
  while its children were blocked waited for millions of ticks. The count is
  kept in the routine's own word now.

## Unreleased - a hostile program, and the gate that refuses it

### Security

- **M1's conformance gate.** `programs/hostile_test.asm` runs as uid 1000 and
  attempts everything it is not entitled to: reading, writing, `access`-ing,
  removing and replacing a file owned by somebody else; changing that file's
  mode; taking ownership of it; and passing a kernel address where a user
  pointer belongs. It requires all eleven checks to fail and two controls to
  succeed, so the gate cannot pass by refusing everything. It is a
  `make check-runtime` case, and the machine must still reach the next prompt.
- **The gate has been seen to say no.** With the access check in `fd_open`
  forced to succeed, the program fails on its first attempt; that sabotage was
  reverted and the run repeated. A gate that has never failed is a decoration.
- **Fault containment is the other half, and already existed:**
  `stack_overflow` writes through a guard page and the task dies while the shell
  runs the next command. Signalling across users is the one part of the exit
  gate still unimplemented - `kill` has no uid policy yet - and is recorded as
  open rather than claimed.

## Unreleased - the permission bits deny

### Security

- **`open` enforces the mode.** HBFS has stored an owner and an rwx mode since
  v3, but `open` never looked at them: a 0444 file could be written by anyone.
  `fd_open` now checks the caller's effective uid/gid against the owner, group
  and world triplets; `SYS_FWRITE` over an existing file checks the write bit
  before replacing it and `unlink` checks it before removing it.
  `programs/perm_test.asm` covers root bypass, the owner and a stranger, and is
  a `make check-runtime` case.
- **`setuid`/`setgid` move the effective ID.** They wrote only the real ID, so a
  program that dropped to uid 1000 kept passing every check as root - the
  enforcement above would have been decorative for exactly the case it exists
  for.
- **`chown` is root's, `chmod` is the owner's.** Any process could chown a
  root-owned file to itself and then write it.
- **The VFS dropped every open flag.** `vfs_hbfs_open` recorded every file as
  `FD_FLAG_READ` whatever was asked for, so a write open looked like a read one
  to the fd layer. `SYS_OPEN` now maps `O_RDONLY`/`O_WRONLY`/`O_RDWR` to the fd
  bits once, at the syscall boundary.

### Build

- **`make full` shows its stages.** Boot sector, kernel and symbols, stage 2,
  user programs, disk image and filesystem are announced in order; a stage with
  work prints its commands, one with nothing to do is silent, and a failure
  prints that stage's whole log. It used to print only the final banner when the
  tree was current.

### Known limits

- `/bin` holds 227 programs and is full, so a new program added for a test is
  installed at the volume root (`ROOT_PROGRAMS` in `populate.py`) until the HBFS
  subdirectory grows in M3.

## Unreleased - a directory name is a directory, not a program

### Shell

- **Typing a directory's name enters it.** `docs` at the prompt is now `cd docs`,
  the way `hello` runs the program. The rule is the same one the shell already
  used for programs: a built-in wins first, then a program in the current
  directory, then one on PATH, and only then the directory. A name with
  arguments is treated as a command that does not exist, so `docs foo` still
  reports an unknown command rather than changing directory.
- **A directory named like a program was executed as one.** `cmd_exec_program`
  read whatever `hbfs_read_file` returned without checking the entry type, so a
  directory loaded as an empty image and jumped to `0x00300000`, faulting with
  `addr=0x00000000`. The entry type is checked before loading now, both in the
  current directory and in each PATH component.

## Unreleased - floating point survives a context switch

### Kernel

- **Two tasks using the FPU no longer corrupt each other.** The x87 register
  stack belongs to the CPU, not to a task, and the scheduler saved nothing
  across a switch: a program that did floating-point work, yielded, and read its
  value back got whatever the task that ran in between had left there. Nothing
  reported it - the arithmetic simply produced the wrong number, and the more
  tasks ran, the more often. Every TCB now carries a 128-byte image at
  `TCB_FPU_AREA` and every switch path calls `sched_swap_fpu` with the same
  `EDI`/`EBX` convention as the fd-table swap, including the timer preemption,
  the waitpid/`pause`/`sigsuspend`/`sleep`/yield paths, the fault teardown and
  every return to the shell, which owns an image of its own.
- **`fnsave`/`frstor`, not `fxsave`/`fxrstor`.** The target is i486-and-up and
  QEMU's 486 model has no FXSR; the area is 16-byte aligned so the wider
  instruction can replace it when a CPU with FXSR becomes the baseline. New
  tasks start from an image captured once after `fninit`, so a task that never
  touches the FPU still has a valid control word.
- **`programs/fpu_test.asm` proves it.** It forks, has both tasks push their own
  value on the x87 stack, yield 64 times and require the exact value back, and
  is a case in `make check-runtime`. Against the previous kernel it printed
  `FPU_FAIL`; with the save in place, `FPU_OK`.

## Unreleased - the parity scorecard, and what "comparable to Windows 2000" means

### Planning and gates

- **A roadmap measured in user jobs.** `docs/ROADMAP.md` now defines the target
  as fourteen things a person does on a 2000-era workstation - log on, run
  services, install software, share a folder, print - each with a pass criterion
  and a wave that delivers it. Server-only features are out of scope, and so is
  compatibility with Microsoft's formats: parity is functional, through
  protocols of our own.
- **`tools/parity_scorecard.py` computes the baseline from the tree.**
  `make check-parity` reports the fourteen rows and fails when one regresses, so
  a capability cannot be lost quietly; `--json` emits the table for CI, and the
  baseline in `docs/parity-baseline.json` is reviewed with the change that moves
  a row. Today: 1 pass (multitasking), 7 partial, 6 fail (logon, services, the
  configuration store, printing, hardware detection, remote administration).
- **`tests/test_parity_scorecard.py` proves the gate is not decorative.** It
  requires the baseline to agree with the tree, and requires a baseline that
  overstates the tree - or omits a row, or is missing - to fail the check.
- **`tools/find_c_toolchain.sh`** proves a compiler can build 32-bit freestanding
  C and link it with `ld -m elf_i386` before new subsystems depend on it; on a
  non-x86 host only a cross compiler passes, so `make check-toolchain` reports
  that instead of failing a build that does not need C yet.
- **`docs/spec/ABI.md`** states the NASM/C contract the coming waves rely on:
  cdecl, fixed-width structure layout with compile-time size and offset
  assertions, the unchanged `int 0x80` register contract, and the reason kernel
  C is compiled `-mno-sse -msoft-float` until per-task FPU state exists.

## Unreleased - a bad pointer is an error, not a restart

### Kernel

- **Every syscall that takes a caller pointer now checks it first.** A ring-3
  program calling `rename(0xD0000000, name)` made the kernel dereference the
  argument itself: the fault arrived in ring 0 with `[supervisor]` in the error
  code, the fault handler treated it as unrecoverable, and the shell restarted -
  taking every task in the system with it. One instruction from any program was
  enough. `sys_rename`, `sys_rmdir`, `sys_truncate`, `sys_symlink`,
  `sys_readlink`, `sys_getmtime`, `sys_setmtime`, `sys_mkdir`, `sys_fread`,
  `sys_taskname`, `sys_setdate`, `sys_clipboard_copy`/`paste`, `sys_pipe_read`/
  `write`, `sys_audio_rec_read` and the VFS path entry points `open`, `delete`,
  `stat` and `chdir` now validate the pointer (and the range, when a length is
  involved) and return their failure value instead.
- **`select`, `getsockopt` and `getsockname` check their buffers too.**
  `select` read the fd_set through a caller pointer before any check; it now
  validates all four buffers (the fd_set is one dword for `FD_MAX` = 8).
- **`getsockopt` corrupted the socket it was asked about.** It computed the
  socket pointer into `EDI` and then wrote the returned length through `EDI`,
  so `getsockopt(fd, SOL_SOCKET, SO_TYPE, ...)` set `SOCK_TYPE` to 4 and broke
  the socket. The length pointer is saved before `EDI` is reused, and both
  output pointers are validated.
- **`sys_readlink` no longer copies an on-disk length into a caller buffer.**
  The size came from the directory entry and was passed straight to `rep movsb`
  as the count, so a link whose size field exceeded one block read past the
  kernel's block buffer and wrote that many bytes into the destination. The copy
  is clamped to a link target's maximum and the destination range is checked.
- **`sys_symlink` measures the target with a cap**, so a string without a NUL
  cannot walk out of user space.
- **Ring-0 callers are exempt, deliberately.** The shell is kernel code and
  passes pointers to its own buffers, which live below `PROGRAM_BASE`, so
  `validate_user_ptr`/`user_range_ok` consult `syscall_ring` - recorded from the
  caller's CS at the one `INT 0x80` entry - and skip the window check for ring 0.
- **`badptr_test` proves it.** It calls every one of those syscalls with an
  unmapped address, requires each to return its documented failure, and then
  does a clipboard round trip through valid pointers to show the checks are a
  guard rather than a wall. It is a `make check` runtime case and rejects
  `PAGE FAULT`.

### Filesystem

- **`chmod` and `chown` never worked.** Both searched `hbfs_dir_buf` for the
  name without loading the directory first, so they examined whatever an earlier
  command had left in the buffer and answered "file not found" for files that
  were right there - from the shell and from a program alike. Both now load the
  directory before searching it. Verified end to end: `chmod 777 script.bat`
  prints "Permissions updated." and a program calling `SYS_CHMOD` gets 0.
- The boot banner, `sysinfo` line and version string said 186 syscalls;
  `programs/syscalls.inc` declares 187 (0-186). `validate_syscalls.py` now also
  checks `kernel/data.inc`, so the text on screen cannot drift again.

## Unreleased - the disc proves itself, and the build stops lying

### Installer disc

- **`make iso ISO_IMAGE_MB=64` did nothing.** The payload image was rebuilt only
  when its sources changed, so with an existing `mellivora-install.img` the
  variable was ignored and a 256 MB image went onto the disc that asked for 64.
  A stamp in `.build/` now records the size the image was built at, and the
  image is rebuilt when it differs. Both advertised spellings work, and the
  disc's own size is what `build_iso.sh` verifies.
- **The installer can now find a file in a directory larger than one sector.**
  A zero record length means "no more records in this sector", not "no more
  records in this directory", but `find_entry` treated it as the end and read
  only the first 2048 bytes. The disc's root directory is exactly 2048 bytes
  today, with the last record ending at byte 1194, so the bug was invisible:
  three sectors of root directory and `MELLIVORA.IMG` in the second one was
  enough to make the installer report that the image was not on the CD. The
  walker now follows the sector chain up to `dir_size`.
- **The read-back covers the whole image, chunk by chunk.** `verify_first`
  printed "Verifying the first sector" and compared 256 of its 512 bytes. Now
  every 32 KB chunk is read back from the target as it is written and compared
  with the bytes that came off the CD; a mismatch is rewritten once, and if it
  still differs the install stops with `BIOS status 0xFF` instead of declaring
  success on a disk that is already wrong. `tests/test_installer_verify.py`
  builds a disc whose writes land one sector late and requires the installer to
  reject it; with the verification stubbed out, the same disc reports
  "Installation complete." over a corrupt target, so the test is not vacuous.
- **The disc is reproducible.** With `SOURCE_DATE_EPOCH` set, `build_iso.sh`
  passes `--modification-date` and `--set_all_file_dates` to xorriso, so the
  same sources produce the same bytes and a published checksum means something.
  Without it the volume keeps the current time, as before.
- **The verifier compares the whole image.** `autoinstall.py` used to compare
  the first 4 MB of the target with the ISO; a truncated copy in the middle
  passed. It now streams the image and compares all of it, and reports
  `image_matches` instead of `first_bytes_match`.
- **`make test-iso` does what its description says.** It ran the structure-only
  check; the end-to-end install now lives there, and `make iso-check` is the
  fast structure check. `tests/test_iso.py` reads the ISO extent by extent
  instead of loading 258 MB into memory.
- **`make clean` removes the discs' payloads** (`mellivora-install.img`,
  `mellivora-install-lite.img`, the QEMU target disk), not just the ISOs.
- **`run_iso.sh` runs the emulator it found.** It accepted
  `qemu-system-x86_64` or `qemu-system-i386` and then always executed the
  latter.
- `install/boot/*.asm` join the NASM lint. The build and install docs name
  `make iso` where they meant it, and the disc's `README.txt` names the HBFS
  superblock at sector 4096 instead of calling the first data block the
  filesystem.

### CI and releases

- **A CI job builds, verifies and installs the disc** (`installer-iso`), which
  nothing did before: `make iso`, `make iso-verify`, the structure checks, the
  unattended install, the boot of the installed disk and the 64 MB variant.
- **Releases carry the disc.** `release-iso.yml` rebuilds both discs from the
  tag with `SOURCE_DATE_EPOCH` set to the tag's commit, gzips them (the 258 MB
  disc is 99.7% zeros and compresses to under a megabyte), writes
  `SHA256SUMS` and attaches all five files. `v13.0.0` shipped no assets at all.
- The workflows move to `actions/checkout@v7` and
  `actions/upload-artifact@v7`, and the Docker actions to their current majors;
  `ci.yml` and the nightly run with `permissions: contents: read`.
- **The container ships a right-sized image.** The publish job built `make full`
  and copied the 2 GB development image into a layer; it now builds the same
  256 MiB volume the installer disc writes, which holds the identical
  filesystem. The `fdisk`/`dosfstools` it never used are gone from that job.

### Documentation

- **The syscall count in prose was one behind.** `README.md`,
  `docs/TECHNICAL_REFERENCE.md` and `docs/PROGRAMMING_GUIDE.md` said 186;
  `programs/syscalls.inc` declares numbers 0-186, and the generated
  `docs/syscalls.md` already said 187. `validate_syscalls.py` now checks every
  "N syscalls" and the "(0-N, no gaps)" range in those files against the header,
  so the next syscall cannot leave the prose behind.

## Unreleased - an installer disc that installs

### Installer

- **The ISO is an installer now.** `mellivora.iso` boots a 2 KB real-mode stub
  (`install/boot/stage1.asm`) whose only job is to read the Boot Record Volume
  Descriptor at CD sector 17, follow the boot catalog to its own load LBA, and
  pull the 64 KB payload (`install/boot/installer.asm`) to `0x10000`. The payload
  walks the ISO9660 tree for `MELLIVORA.IMG`, enumerates BIOS hard disks with
  INT 13h `AH=41h`/`AH=48h`, asks which one to use, refuses a disk smaller than
  the image, writes it in 32 KB chunks with `AH=42h`/`AH=43h`, then reads sector
  0 back. The installed disk boots on its own; no CD, no second disk.
- **Why the old disc could never work.** `Experimental/` put the 2 GB raw image in
  the El Torito boot entry and declared 3649 sectors to preload - far past the 640
  KB of conventional memory; the kernel has no ATAPI support, so a CD-only boot
  had no HBFS volume; and `iso-lite` truncated the image to 64 MB, which broke the
  superblock's block count. It only booted in QEMU with the same image attached as
  a separate hard disk, which is not what install media do.
- **`Experimental/` is gone.** The installer lives in `install/` with its own
  `README.md`, `tools/build_iso.sh`, `tools/run_iso.sh` and an unattended driver,
  `tools/autoinstall.py`. `tools/packet_fuzz.py` moved to `tools/`.
- **The disc ships a right-sized image.** `make iso` builds
  `mellivora-install.img` at `ISO_IMAGE_MB` (default 256) and `make iso-lite`
  builds it at 64 MB; `populate.py` derives the HBFS block count from the file it
  populates, so both are fsck-clean. `make iso-verify` checks the El Torito record
  and the payload, and `make test-iso` installs the disc onto a blank disk in
  QEMU, compares the bytes and boots the result.
- **Rock Ridge names.** `mkisofs` at ISO level 1 truncates `MELLIVORA.IMG` to
  `MELLIVOR.IMG`, so the installer reads the `NM` entry in the SUSP system-use
  area and falls back to the plain identifier; the disc is built at `-iso-level 3`
  as well, so either name works.

## Unreleased - fault injection for the error paths

### Reliability

- **OOM, disk-full and dropped-frame injection** (`kernel/faultinject.inc`). Three
  counters, armed by any task through `SYS_FAULTINJECT` (186), make the kernel fail one
  thing on purpose: `pmm_alloc_buddy_order` returns 0, `ata_write_sectors` returns
  `CF=1`, `net_send_frame` drops the frame. Each hook is a `call`/`jc` in front of the
  one choke point every path goes through, and each gate preserves every register, so
  no caller changed.
- The point is the *error path*, not the fault: `programs/fault_inject_test.asm` forks
  with 1..12 page allocations failing (kernel stack, area table, page directory, COW
  share - a different failure point for each prefix) and requires `fork` to report
  failure **and the free-page count to come back to its baseline every time**; creates a
  file while every write fails and requires `-1` and a working create afterwards; and
  drops one frame during a DNS lookup, which the retry has to absorb.
- `make check-faults` runs the fixture repeatedly (a failure path that leaks only some
  of the time cannot hide), and `check-runtime` runs it once after `dhcp`.
- `SYS_FAULTINJECT` reports what is armed (`FI_OP_STATE`) and how many faults it has
  actually injected (`FI_OP_TOTAL`), so a test that silently armed nothing fails instead
  of passing. It is a test facility with no privilege check - P6 is where that changes.

### Tooling

- `docs/syscalls.md` / `docs/syscalls.json` regenerated for the new syscall (187 total).

## Unreleased - no stale bytes in a new program window

### Memory

- **A new program no longer inherits the previous one's memory.** A task's window
  starts as a copy of the shell's window, and nothing cleared it between launches, so
  whatever the last program left above its own image - its heap, its `mmap` scratch and
  its **whole user stack** - was mapped and readable by the next program. `SYS_EXEC`
  clears `[image end, PROGRAM_END_ADDR)` in the shell's window before the trampoline is
  written and before the task is created, so the image the loader just wrote is
  untouched, the break still starts where `set_program_brk_init` put it, and the stack
  window a task maps is zeros. This was the last open item of the P1 exit criteria ("no
  stale-data leaks between processes").
- exec in place is exempt: the task *is* the process that owns what is there, and its
  stack is live. Only a launch that creates a task clears the window, which means a GUI
  launch too (it also gets a private copy of the cleared window).
- `programs/stale_write.asm` fills the image tail, the heap area and its own stack
  window with a marker; `programs/stale_probe.asm` looks for it at a fixed address and
  across its stack window. The pair is a `check-runtime` case, and it fails on the
  previous kernel with `STALE_LEAK 00340000`.

## Unreleased - randomised stack top

### Security

- **The user stack now starts at a randomised address.** `vm_random_stack_top` picks a
  page-granular offset inside the stack region for every launch, so the top - and with it
  the trampoline and the first ESP - moves between launches instead of always sitting at
  `PROGRAM_EXIT_ADDR - 4`. The chosen top is threaded through both `sched_create_task`
  call sites and the legacy IRET path, the trampoline is written at the new top, and
  `paging_setup_user_stack` now unmaps everything outside `[top - 16 pages, top)` - both
  the growth area below it and, new, the gap a smaller top leaves above it.
- `pf_try_stack_growth` finds its frontier by scanning down from the *task's own* top
  (`TCB_USTACK + 4`) rather than from the top of the region, because the pages above a
  randomised top are unmapped and are not growth area. It also refuses a top outside the
  stack region instead of wrapping.
- **The mapping randomiser is seeded from the clock, not from zero.** `vm_init` ran
  before interrupts were enabled, so it seeded `vm_rng` from `tick_count` - always 0 -
  and every boot laid memory out identically. It now mixes the RTC, the live PIT counter
  and `tick_count` through an avalanche step, so the first mmap and the first stack of a
  boot differ between boots as well as between launches.
- **exec in place keeps its stack top.** A task that `execve`s already holds a stack
  window at its current top and holds nothing outside it, so re-randomising would place
  the trampoline in a page that no longer exists in its directory. `cmd_exec_program`
  reuses `TCB_USTACK` when a task is already running, and only a fresh launch draws a
  new top. (This was a real fault: `argv_test` page-faulted at `0x003FFFF0`, a supervisor
  write to the new trampoline.)
- **A forked task's window stack is no longer shared twice.** `cow_fork_share` re-shared
  the 64 KB below `TCB_USTACK` even when the stack sits inside the program window, which
  the window range had already shared - double-counting those frames' COW references and
  walking below `PROGRAM_BASE` into kernel identity pages. A randomised top made that
  overlap up to 16 pages wide; the stack block is now skipped when its top is inside the
  window.
- Background launches (`hello &`) are unchanged: their stack sits at the top of its own
  64 KB block, which `cow_fork_share` shares as `[top - 64 KB, top)`. Randomising that
  top would push the shared range below the block into whatever was allocated next.

### Tooling

- `programs/stack_aslr_test.asm` prints its own ESP, and `tests/test_runtime.sh` runs it
  four times in one boot (`run_varying_case`) and fails if every launch reports the same
  address - a single launch cannot show that an address is random.
- `tools/qemu_drive.py --then` is repeatable, so one boot can run a command several times.

## Unreleased - kernel slab allocator

### Kernel

- **A slab allocator for fixed-size kernel objects** (`kernel/kmem.inc`). A cache carves
  4 KB pages from the PMM on demand and threads the free objects through their own first
  dword, so nothing is stored outside the objects; pages are kept and reused rather than
  returned. Every object is zeroed before it is handed out, because a task slot is reused
  the moment its occupant exits.
- **Per-task tables come from it.** A task's `vm_area` table (896 bytes) and its argv
  snapshot (512 bytes) were `MAX_TASKS`-sized static arrays - 128 copies of each, most of
  them for tasks that never existed. The image drops from 2,015,651 to 1,840,601 bytes,
  so the headroom under the 2 MB kernel limit grows from 81 KB to 256 KB, and a running
  system now pays only for its live tasks.
- The area table is allocated with the task (`vm_alloc_table`) and freed by every teardown
  path (`vm_free_table`); `vm_area_base` returns 0 for a slot without one, so the fault
  handler fails closed instead of dereferencing a null table. `vm_clear_table` is gone -
  a fresh, zeroed object cannot carry the previous occupant's mappings.
- A forked child copies its parent's argv snapshot into its own object, so `SYS_GETARGS`
  reports the inherited arguments instead of whatever the recycled slot last held.

## Unreleased - task teardown returns what it took

### Memory

- **A task's page tables are released with its directory** — `paging_free_pd` freed only
  the 4 KB directory page, so every table a directory had privatised for a 4 MB region
  was leaked when the task exited: one page per fork (the window table), plus one for
  each demand-zone region an mmap touched. It now walks the 1024 PDEs and frees the
  tables whose address differs from the master's for that slot — the master's template
  and tables `paging_clone_pd` already copied per child belong to somebody else. Every
  "free this task's CR3" site now goes through it instead of calling `pmm_free_page`.
- **`paging_setup_user_stack` releases the frames it drops.** It unmaps all but the top
  16 of the 64 stack pages of the image it replaces, but the release step had been
  replaced by a leftover "EXPERIMENT: leak instead of releasing" `nop`, so 48 frames
  (192 KB) stayed allocated for the life of the boot. The frames are private to the
  directory being set up, so they are given back through `cow_release_frame`.
- **A task killed by a signal now returns its memory.** `sched_signal_task` tears the
  target down itself — the task never runs `sched_exit_task` — and it only freed the
  kernel stack, fd table, signal table and directory page. Everything the task ever
  mapped stayed allocated: an 8 MB heap leaked 8 MB per kill. The kill path now releases
  the private window frames, a separate user stack, and every mmap area with its
  page-cache references, exactly as an exit does.

### Processes

- **A signal-killed task now leaves a zombie its parent can collect.** The kill path used
  to free the slot outright, so `waitpid` never found the child, blocked the parent and
  handed the console back to the shell. Both ways a task can die now go through
  `sched_publish_death`: status stored, TCB left in `TASK_ZOMBIE`, waiters blocked in
  `waitpid` woken with that status, `SIGCHLD` queued, and a shell-launched task (nobody
  can wait for it) reaped immediately. A killed task reports `0x80000000 | signal`;
  `0x80000000` alone is still a contained fault. Killing an existing zombie is a no-op
  instead of a second teardown.

### Tooling

- `programs/stack_leak_test.asm` runs sixteen fork+exec cycles and checks the free-page
  count came back (it fails at ~1 page per launch on the old kernel);
  `programs/kill_leak_test.asm` kills a child holding an 8 MB mapping, waits for it,
  checks the status names `SIGKILL`, and checks the pages came back (it failed with 2054
  pages before the fix). Both run in `make check-runtime`.

## Unreleased - shared file mappings are coherent

### Memory and storage

- **A page cache sits in front of shared file mappings** — `kernel/pagecache.inc` keeps
  one frame per HBFS block and every `MF_SHARED` mapping of that block maps the same
  frame, so a write through one mapping is visible through every other one, in the same
  task or in another. Before this, each mapping kept private frames: two mappings of one
  file disagreed with each other and the last release won, which the VMM spec recorded
  as a known limitation.
- **`fork` keeps sharing a shared mapping** instead of marking it copy-on-write, which
  is what `MAP_SHARED` promises: parent and child write the same pages. Each inherited
  page counts as one more reference in the cache.
- **Releases hand the frame back to the cache, not the PMM.** `munmap`, `free`, `exec`
  and task exit still write the dirty pages back first; they now drop a cache reference
  afterwards. An entry with references is never evicted, so a block cannot be recycled
  under a mapping that still uses it.
- Dirty state needs no new bookkeeping: a write sets the dirty bit of the PTE that
  performed it, and that mapping's release flushes the block before dropping its
  reference, so an entry is clean by the time its count reaches zero.
- The cache is keyed by HBFS block number counted from `HBFS_DATA_START`: a file's
  first data sector is block-aligned within the file, but the data area does not begin
  on a block boundary of the disk.
- Every disk write drops cached blocks that nobody maps (`pc_invalidate`, next to the
  existing `blk_cache_flush`), so a block freed and reused by another file cannot serve
  its old bytes to the next mapping of it. A block that is still mapped is never
  dropped - its owner wrote it deliberately and flushes it when the mapping goes.

### Tooling

- `programs/vmtest5.asm` maps one file shared twice and checks that writes cross both
  ways, that a forked child writes the same pages, that both releases write back, and
  that a fresh mapping reads the written file. It is in `make check-runtime`, including
  a 384 MB run, and its failure report names the byte offset that differed.

## Unreleased - memory above 128 MB works

### Memory

- **The page-fault handler gives the interrupted code back its registers** — it used
  to save only EAX/ECX/EDX, but the helpers it calls (`vm_find_area`,
  `cow_break_page`, `paging_map_page_all`, `paging_privatize_pde`, `pmm_alloc_page`)
  clobber EBX/ESI/EDI/EBP as well. Above 128 MB that was quietly fatal: physical RAM
  is identity-mapped from 128 MB upward only on demand, so the buddy allocator's
  free-list walk — whose cursor lives in EBX — took the fault the first time it read a
  block above the boot identity map and came back with EBX holding the faulting page
  number. The chain then continued at page 0, through the interrupt vector table. The
  handler now saves the full register set on entry and restores it on every path that
  resumes the faulting instruction.
- **The handler survives faulting on itself** — it does exactly that: zeroing or
  filling a freshly allocated frame above the identity map re-enters the
  identity-demand path. The `pf_addr`/`pf_eip`/`pf_errcode`/`pf_demand_frame` scratch
  is now saved and restored around the handler, so a nested fault can no longer
  redirect the outer mapping. Before this, every demand page backed by a frame above
  128 MB was mapped at the frame's physical address instead of the address the program
  touched, and the task died on the next access.
- **`cow_ref` covers every frame the allocator can hand out** — the table stopped at
  128 MB, so a forked share above it was counted by neither side: `cow_put` reported
  each mapping as the sole owner, the frame was freed twice, and the buddy allocator
  then served the same page to two callers. A fork/exit cycle above 128 MB hung the
  system. The table size is now derived from `USER_DEMAND_BASE_MAX` (448 MB), the
  ceiling `pmm_init` already enforces, instead of being a separate literal.

### Tooling

- `tests/test_runtime.sh` takes an optional guest RAM size per case and gained four
  high-memory cases (`vmtest`, `vmtest2`, `cow_test`, `heaptest`) that boot a 192 MB
  or 384 MB guest. They fail on the previous kernel: `heaptest` at 384 MB printed
  `HEAPTEST_FAIL stage 1` and `vmtest` stopped after the second forked child.

## Unreleased - 100 MB heap workload and a demand base above RAM

### Memory

- **The user demand zone now starts above RAM** — the kernel addresses physical
  memory 1:1, so with more than 128 MB of RAM a *frame* and a *user address* could be
  the same number. A task holding an area there would have the kernel's own frame
  mapped as user scratch (and, after the supervisor-demand change, the fault handler
  would treat a frame access as a user page). `user_demand_base` is now computed at
  boot as the first 4 MB boundary at or above the top of RAM: 128 MB on a 128 MB
  machine (the historical layout), higher on bigger ones. The allocator is capped at
  `0x1C000000` so a frame can never reach user space.
- **`make check-heap`** — the P1 exit gate: `programs/heapbig.asm` allocates twenty
  5 MB blocks (100 MB), writes a marker into every page, reads them all back, frees
  them, checks the free-page count returned to where it started and allocates a 16 MB
  block afterwards. It runs in `make check` and in CI.
- `tools/qemu_drive.py` grew `--mem MB` so a case can choose the guest's RAM.

### Fixed (pre-existing)

- Allocations on a guest with **more than 128 MB** of RAM were unreliable, and this
  predated the change above: `tools/qemu_drive.py` hard-coded `-m 128` until it grew
  `--mem`, so no earlier test ever ran a real allocation above 128 MB. With 192 MB or
  more, `heaptest` failed inside the buddy allocator: a free-list node's "next"
  pointer was 0 where the NIL sentinel belongs, the chain walked to page 0 and
  followed the interrupt vector table into a page above the top of RAM. See "memory
  above 128 MB works" above for the three faults behind it.

## Unreleased - shared file mappings

### Memory and storage

- **`SYS_MMAP_FILE` can write back** — OR `MF_SHARED` (0x10) into the protection mask
  and the mapping becomes shared: every page the CPU marked dirty is written to the
  file when the mapping is released (`munmap`, `free`, or the task exiting), and
  before `mprotect` re-maps a page (re-mapping would lose the dirty bit). Pages the
  program only read keep their file bytes, the file's length never changes, and a
  page past the end of the file is scratch space that is never pushed back — writing
  it would land on whichever file owns the next block.
- Dirty tracking needs no bookkeeping of its own: the PTE dirty bit that x86 sets on
  the first write decides what goes back.

### Known limitation

- Two mappings of the same file each keep their own frames, so they do not see each
  other's writes until a file page cache lands; each writes back its own changes and
  the last release wins. Private mappings are unaffected and still never write
  through.

### Tooling

- `programs/vmtest4.asm` covers a shared mapping's write-back, the untouched-page and
  past-EOF cases, the file length and the private-mapping contrast. It runs in
  `tests/test_runtime.sh`.

## Unreleased - user heap on the VMM

### Memory and isolation

- **`SYS_MALLOC` is anonymous demand-zero memory** — it used to hand out raw PMM
  pages mapped at their identity addresses, which meant a fresh block contained
  whatever the previous owner had left in RAM (a cross-process information leak),
  `SYS_FREE` released the frames but left the mapping in place (so the block still
  aliased memory the allocator could hand to someone else), and a heap block could
  never exceed the 1 MB program window. Blocks are now `vm_area`s in the demand
  zone: zeroed on first touch, private, reclaimed on `free`, and independent of the
  window. `SYS_REALLOC` allocates, copies and releases the same way.
- **`SYS_FREE` rejects anything that is not a block base** — `malloc` always returns
  an area base, so a pointer into the middle of a block returns `-1` instead of
  partially unmapping someone's memory.
- **Supervisor faults can service the task's own areas** — a page fault raised by the
  *kernel* while touching one of the current task's demand pages (a syscall copying
  into an untouched buffer, `realloc` copying into a fresh block) now maps the page
  with the user's protection. Previously every supervisor fault went straight to the
  panic path, so the first kernel access to an unfaulted user page killed the system.

### Audio

- **`SYS_AUDIO_PLAY` stages demand-zone buffers** — the SB16 reads system memory by
  physical address, and a demand-mapped buffer has no single physical address. Such
  buffers are copied into a 64 KB kernel staging range first (one single-cycle DMA
  transfer cannot be longer anyway); buffers in the identity region are played in
  place as before.

### Tooling

- `programs/heaptest.asm` covers zero-filled blocks, a 4 MB allocation, `realloc`
  data preservation, freed-block faults, re-allocation zeroing, `free` validation and
  playing a demand-zone buffer. It runs in `tests/test_runtime.sh`.

## Unreleased - block cache in front of the disk

### Storage

- **Unified block cache** (`kernel/blkcache.inc`) — a 32-entry, 4 KB block cache
  (128 KB of BSS) sits between the HBFS data path and the ATA driver. Every
  whole-block data read is served from it: opening a program, `cat`, `fd_read` and
  the demand fault of a file-backed mapping all hit the same cache, so a block read
  twice costs one disk access. Reads that are not whole and block-aligned (the
  metadata and journal paths) pass straight through, unchanged.
- **Coherence by construction** — `ata_write_sectors` flushes the whole cache after
  every successful write, so no block can be served stale. With one writer and no
  write-back dirty data, per-block invalidation would add bookkeeping without
  buying anything.

## Unreleased - file-backed mappings

### Memory and isolation

- **`SYS_MMAP_FILE` (185)** — `mmap_file(path, len, prot, offset)` maps an HBFS file
  into the address space. It is private and demand-paged: `mmap_file` reads nothing,
  the first touch of each page reads that page's block into a fresh zeroed frame, and
  nothing is ever written back, so a writable mapping cannot corrupt the file. A
  page-aligned `offset` maps from that point in the file; pages past the end of the
  file read as zero instead of faulting. tmpfs/procfs/devfs paths return `-1`.
- **Splitting a file area keeps its file position** — the area record now carries the
  file's first data sector and the number of blocks it can back, so `munmap` and
  `mprotect` of a sub-range hand the right-hand piece the sector that matches its
  first page. Without that, a split file mapping would serve page N from block 0.

### Fixed

- **`hbfs_read_file` overran the caller's buffer** — the whole-file read copied every
  block of the file straight into the destination, but callers size their buffer from
  the *reported* file size, so any file whose length is not a multiple of 4 KB
  scribbled up to one block (4 KB) of block padding past the end of the buffer. Each
  block now lands in the bounce buffer and only the bytes the file actually contains
  are copied out.
- **`programs/vmtest2.asm` buffers aliased the code** — a `section .bss` directive in a
  flat binary restarts the address counter at zero, so the test's variables overlapped
  the program's own first instructions and only worked because they were written after
  those instructions had run. The buffers now follow the code in the default section.

### Tooling

- `programs/vmtest3.asm` covers file mappings: lazy read, exact contents, zero fill
  past EOF, page-aligned offsets, private writes, read-only protection, split-position
  correctness and unmap. It is wired into `tests/test_runtime.sh`.

## Unreleased - demand-zero mappings and partial unmapping

### Memory and isolation

- **Anonymous `mmap` is demand-zero** — `SYS_MMAP` now only records the mapping; the
  first access to each page allocates and zeroes a frame. Reserving memory costs no
  RAM until it is used, and a fresh page still never exposes stale bytes.
- **Demand faults honour the mapping's protection** — the handler maps the new page
  with the area's `PROT_*` instead of unconditional read/write, refuses a write to a
  read-only mapping, and refuses instruction fetch from a non-executable one. The
  4 MB page table is privatised before the PTE is written, so a mapping can no longer
  appear in every address space that shares the master's demand template.
- **Syscall buffers may be demand-zero** — a supervisor access to a user address that
  belongs to the current task's own area faults the page in with user access instead of
  identity-mapping physical memory over the mapping (which would read the wrong bytes
  and could leak kernel memory to ring 3).
- **Partial `munmap` and `mprotect`** — both accept any page-aligned sub-range of one
  area. The area is split into exact pieces; `munmap` releases only the frames in the
  requested range, `mprotect` changes only its protection. `PROT_NONE` keeps the frame
  mapped supervisor-only, so data survives a later `mprotect(PROT_READ|PROT_WRITE)`
  instead of coming back as zeros.
- **`fork` shares `mmap` areas copy-on-write** — the child inherits the parent's
  `vm_area` records and its writable pages are marked read-only in both address
  spaces. Previously the cloned page tables pointed at the same frames and both
  processes could write them.
- **`exit` and `exec` release every mapping** — frames are dropped through their COW
  reference counts and the per-slot records are cleared, so a task's `mmap` frames no
  longer leak and a recycled PID cannot inherit the previous occupant's mappings.
- **Protection changes detach COW shares** — `mprotect` copies a shared frame before
  changing its protection, so one process's `mprotect(PROT_READ)` cannot be undone by
  the other's write fault, and a later `mprotect(PROT_WRITE)` never hands both
  processes the same writable page.
- **COW bookkeeping distinguishes read-only shares** — the per-frame reference byte now
  uses bit 7 as "writable when shared". A frame that was already read-only at fork is
  counted (so the last owner frees it) but is not breakable: a write to it remains a
  protection fault.

### Tooling

- `programs/vmtest2.asm` covers demand-zero allocation, partial `munmap`/`mprotect`,
  `PROT_NONE` data preservation, fork isolation of `mmap` pages and frame reclamation
  when a child exits. It is wired into `tests/test_runtime.sh`, so `make check` fails if
  any of it regresses.

## Unreleased - guarded, demand-grown user stacks

### Memory and isolation

- **Stack guard page** — the top of the program window is now a reserved 256 KB stack
  region (`USER_STACK_BASE` 0x003C0000 .. 0x00400000) with a permanent unmapped guard
  page at its base. Only the top 64 KB are mapped when a task starts, so a runaway stack
  now faults instead of silently overwriting the heap: the offending task is killed with
  a one-line report and the shell (or desktop) keeps running.
- **Demand-grown stacks** — a not-present user fault in the region is serviced by
  `pf_try_stack_growth`, which finds the lowest mapped stack page by walking the window's
  page table (the page tables are the state, so forked and resumed tasks need no
  bookkeeping), accepts the fault only within `STACK_GROWTH_SLACK` (64 KB) of that
  frontier, and maps the missing pages with freshly zeroed frames.
- **Heap cannot reach the stack** — `SYS_SBRK` and `set_program_brk_init` cap the break at
  `USER_STACK_BASE`, so the heap and stack can never meet.
- **Kernel identity demand above 128 MB** — a supervisor not-present fault in the
  identity-managed zone maps that page identity (bounded by the firmware-reported memory
  top), so the buddy allocator can use RAM above the initial 128 MB identity map instead
  of faulting the first time it touches it. Machines with more than 128 MB now work.

### Fixed

- **`paging_clone_pd` shared page tables with the parent** — a forked child inherited the
  parent's page tables, so a copy-on-write break in one address space rewrote the other's
  PTE: the child started observing the parent's private pages. This was latent because
  the window table was always shared with the master until a task privatised it, and it
  became visible as soon as the stack guard privatised it per task (the `pgrp_test`
  process-group suite caught it: both children saw the parent's `is_child2` flag and sent
  the same signal). A new directory now copies every page table the source owns, keeping
  only the master-owned template tables shared.

### Tooling

- `programs/stack_test.asm` grows a stack through 44 fault-driven pages, checks every
  grown page, and proves the heap cannot enter the stack region.
- `programs/stack_overflow.asm` writes to the guard page and asserts the task dies alone.
- Both are wired into `tests/test_runtime.sh`, so `make check` fails if stack containment
  regresses.

## Unreleased - Burrows 3.0 multi-app desktop

### Overview

Burrows is no longer a single-app shell. Any number of applications run side by side,
each in its own task, each drawing into a private window buffer, with keyboard input
routed by focus and the desktop never blocking on a child program.

### Desktop

- **Per-window backing buffers** — every window owns one, allocated at creation and
  grown on resize/maximize. `SYS_GUI` drawing calls retarget the drawing primitives at
  the caller's buffer, so coordinates are window-relative and one app can no longer
  paint over another or over the desktop chrome. If a buffer cannot be grown, the old
  one is kept and drawing stays clipped instead of corrupting memory.
- **Window ownership** — windows record the PID that created them (kernel applets use
  −1). Non-owners cannot draw into or destroy a window, and `gui_cleanup_pid` tears a
  process's windows down on exit, Ctrl+C abort and kill.
- **Concurrent apps** — `SYS_GUI/GUI_LAUNCH` (30) starts a program as a new ready GUI
  task and returns its id, so the launcher stays responsive. `cmd_exec_program` gained
  a GUI path that creates the task, isolates its program window and returns.
- **Cooperative frame yield** — `GUI_FLIP` marks the caller ready and round-robins the
  ready queue, so GUI apps that busy-poll syscalls still share the CPU. The timer only
  preempts ring 3, so this is what keeps N apps alive at once.
- **Focus-routed input** — the focused window's owner receives key events; desktop
  shortcuts (`F1` launcher, `F2` terminal, `Alt+Tab`, `Ctrl+Alt+1..4`) are handled by
  the kernel even when nothing has focus, so a workspace switch can never strand the
  user.
- **Alt+Tab switcher overlay** — cycling focus briefly shows a centred panel listing the
  windows on the current workspace with the newly focused one highlighted.
- **Persisted desktop theme** — `GUI_SET_THEME`, the Settings applet and the built-in
  theme buttons write `burrows.cfg` (a 52-byte record: magic + theme colours) through
  HBFS. `burrows_init` reloads it at boot, so a chosen theme survives a reboot; a
  missing or malformed file falls back to the built-in default.
- **Theme-driven wallpaper** — the desktop gradient is derived from the theme's desktop
  colour instead of hard-coded blues, so switching themes repaints the whole workspace.
- **Keyboard window management** — `Alt+Left`/`Alt+Right` tile the focused window to the
  left or right half, `Alt+Up` maximizes, `Alt+Down` restores the saved geometry, and
  `Alt+F4` delivers `EVT_CLOSE` to the focused window's owner. Maximize and tile save
  the previous geometry, so restore always returns a window where it was, and a grown
  buffer is filled with the app's own background colour instead of black.
- **Virtual workspaces** — four workspaces with a taskbar indicator; switching focuses
  the topmost window on the destination workspace.
- **Crash resilience** — a ring 3 fault in a GUI application no longer takes the
  machine down. The kernel checks the faulting CS, tears down only that task, posts a
  toast naming the program and the faulting EIP, and returns to the desktop with the
  remaining apps intact. Kernel faults, and faults with no desktop running, still take
  the reporting path that returns to the shell.
- **Focus handoff** — destroying the focused window hands input to the topmost window
  on the workspace instead of leaving the desktop with no keyboard owner, so closing an
  app (or losing it to a fault) never makes the desktop deaf.
- **Notification centre** — every toast is kept in an 8-entry history ring; `F3` opens a
  panel listing recent notifications newest-first with the colour each was posted with,
  even after the toast itself has expired.
- **16 KB clipboard** — `SYS_CLIPBOARD_COPY`/`PASTE` moved from 4 KB to 16 KB, enough to
  move a source file or a large log between applications.
- **Leaving the desktop is safe** — `SYS_GUI/GUI_EXIT_DESKTOP` (31) returns to the shell
  and `sched_kill_all_tasks` frees every remaining task. Previously surviving GUI tasks
  kept drawing over the restored text console, leaving a green screen.
- **Polish** — gradient wallpaper, taskbar title truncation, launcher singleton via
  title match, redesigned launcher (search box with caret, list panel, footer hints,
  visible Exit Desktop) that closes after launching.

### Programs

- **BTerm window title** — the title bar tracks the shell's working directory
  (`BTerm: /docs`) using a new `SYS_GUI/GUI_SET_TITLE` (32) sub-function, and the same
  call is available to any application for its own windows.
- **BTerm input fix** — the input buffer was never re-terminated after an edit, so the
  previous command's trailing characters showed up at the end of the next line and
  commands like `help` were reported as unknown. The buffer is now terminated on every
  insert.

- **BEdit undo/redo** — `Ctrl+Z` undoes and `Ctrl+Y` redoes up to four edits. Each edit
  snapshots the whole document, so undo is correct for character inserts, backspaces and
  line splits alike; a new edit clears the redo ring and opening a file clears both. The
  editor's status bar advertises the new keys.
- **Crash Resilience Demo** (`bfault`, listed as "Crash Resilience Demo") — a diagnostic
  application that deliberately faults in ring 3. Launching it shows the fault toast and
  the desktop carrying on, so the containment path can be demonstrated and tested rather
  than only described.

### Kernel

- **User/kernel isolation** — the identity map marked all RAM 0–128 MB `PG_USER`, so any
  ring 3 program could read and write the kernel image, page tables and allocator state.
  Kernel memory is now supervisor-only; user access is granted per page to exactly the
  things a process needs: its program window, the legacy VGA text aperture, the VBE
  shadow framebuffer, IPC surface pixels and shared memory (`paging_map_phys_user`).
- **Real `mmap`/`munmap`/`mprotect`** — `kernel/vm.inc` adds a per-task `vm_area` table,
  anonymous mappings in the demand zone with randomized placement, page-level
  protection changes, frame release on `munmap`, and W^X enforcement (writable and
  executable never co-exist unless `MAP_JIT` is requested). Demand faults now **fail
  closed**: an address is only serviced if it belongs to one of the task's mappings, so
  a stray pointer or a just-freed range faults instead of silently allocating.
- **`SYS_MALLOC` published to ring 3** — legacy page allocations are mapped
  user-accessible, so `tcc`, `audiodemo` and `soundviz` keep working under the new
  supervisor-only kernel mapping.
- **Contained console faults** — a ring 3 fault now kills only the offending task instead
  of resetting the session: the shell, the file system state and every other process
  survive. Contained faults print a one-line report (`[fault] task killed, eip=… addr=…`);
  supervisor faults still produce the full register dump and restart the shell.
- **`SYS_SBRK` actually works** — the break was initialised to the *end* of the program
  window, above the user stack, so every growth request failed and a program that
  trusted the returned pointer could write into identity-mapped kernel memory. The
  loader now records where each image ends (`program_brk_init`, page-aligned, clamped
  below the stack), `sched_create_task` seeds `TCB_BRK` from it, in-place exec refreshes
  it, and ELF images report the end of their highest segment. `SYS_SBRK` keeps the break
  inside the window, zeroes newly exposed memory, and returns the old break so callers
  get usable memory.
- **`SYS_READDIR` index corruption** — the handler validated the name buffer by loading
  `HBFS_MAX_FILENAME + 1` into ECX, destroying the caller's entry index; every call then
  read the same out-of-range slot, so `ls` inside BTerm listed one entry forever and hung
  the terminal. The index now survives validation.

### Testing

- **`vmtest`** — asserts `mmap` alignment/zero-fill/writability, W^X rejection,
  `mprotect` enforcement (child faults on a read-only page), `munmap` enforcement (child
  faults on a released range) and non-overlapping placements.
- **`memprot_test`** — forks a child that writes to kernel memory and asserts the child
  dies while the parent (and the shell) survive; also checks that the program window and
  the heap are readable and writable. Wired into `tests/test_runtime.sh`.
- Full-suite run after the change: **2093/2093** unit tests plus boot, runtime, desktop
  and fsck.


- **New runtime case `sbrk heap growth`** — `sbrk_test` now grows the heap by 128 KB,
  writes and verifies both ends, checks the fresh pages read as zeroes, and shrinks back
  to the original break.

- **Automated desktop regression test** — `tests/test_desktop.py` (wired into `make check`
  as `check-desktop`) boots QEMU headless, enters Burrows, launches a terminal and a
  second app, exercises `Alt+Tab`, `F3`, `Ctrl+Alt+1..4` and Exit Desktop, and asserts on
  kernel COM1 markers, screendump video modes and the absence of faults. It also
  launches the Crash Resilience Demo and asserts the fault is contained and the desktop
  still renders.

## Unreleased - Foreground process model

### Overview

Foreground programs are now real scheduler tasks instead of a one-off IRET from the
shell, and an exec inside a task gets a private copy of the program window so it cannot
overwrite the image another process is running.

### Kernel

- **Per-task framebuffer mapping** — `paging_map_page` only ever edited the master
  directory, so once programs ran as tasks with private page directories every VBE game
  page-faulted on its first framebuffer write (address `0xFD000000`, supervisor write).
  Added `paging_map_page_at` (choose a directory), `paging_map_page_here` (active
  directory, used by demand paging) and `paging_map_page_all` (master + active). The VBE
  mode setter and the sigreturn trampoline use the latter.
- **`vbe_fill_triangle` divide-by-zero** — the two short-edge scanline divisions loaded
  the divisor into EDX and then executed `cdq`, which overwrites EDX with the sign of the
  dividend; every triangle with a positive intermediate hit `#DE` (asteroids crashed as
  soon as it drew its ship). The divisor now lives in ECX.
- **Exception visibility in graphics modes** — the generic exception handlers restored
  text mode only for the Burrows desktop, so a fault inside a full-screen VBE program
  printed its report to an invisible text buffer. They now also check `vbe_active`.

- **Per-task VFD table** — the VFS virtual-fd table is now swapped per task, just like
  the legacy HBFS table. Both live in the task's 4 KB fd page (legacy at offset 0, VFDs
  at 256) and are copied in/out on every context switch. A forked child closing a
  descriptor no longer closes it for its parent; `programs/fd_isolate_test.asm` covers it.
- **Unified fd namespace** — sockets now occupy VFDs (`VFS_FS_SOCK`) instead of their own
  0–31 space. `SYS_SOCKET`/`SYS_ACCEPT` return VFDs, every socket syscall translates with
  `vfd_to_sock`, and `close()` shuts the socket down (TCP FIN) and frees the slot;
  `SYS_SOCKCLOSE` is now an alias for that path. `read`/`write` on a socket return −1
  (use `send`/`recv`).
- **fd bounds checks** — `vfs_read`, `vfs_write`, `vfs_close` and `vfs_seek` reject
  fds ≥ `VFS_MAX_FDS`; previously an out-of-range fd indexed past `vfs_fd_table`.
- **procfs fixes** — `str_suffix_match_ebx` pushed a stack slot per `/` in a path and
  never popped it, so `/proc/version` returned to whatever was on the stack (this made
  every procfs open fault); it is now a correct last-component matcher. `vfs_proc_open`
  wrote the fs type to `VFS_MT_TYPE` (offset 16) instead of `VFD_FS_TYPE` (offset 4), so
  proc reads never dispatched. `meminfo` called `pmm_free_pages` by mistake, never wrote
  its numbers, and left a NUL between the label and the value — it now reports
  `MemTotal`/`MemFree` in kB and no longer frees random pages.
- **Foreground tasks** — the prompt launches programs through `sched_create_task` +
  `sched_start_task`, so each run owns a TCB, kernel stack, page directory, fd table page,
  `errno` and program break. `SYS_EXIT`, Ctrl+C and fork-child exits all tear the task down
  (`sched_exit_task`) and restore the shell's fd view and master directory, so interrupted
  programs no longer leak TCBs, stacks, fd pages or directories.
- **fd inheritance** — a new task's fd page starts as a copy of the launcher's view, so
  redirections active at launch are visible while later changes stay isolated.
- **exec isolation** — `paging_isolate_program_window` privatises the PDE covering
  `PROGRAM_BASE` and copies the 1 MB window into task-owned frames before a task execs
  (`paging_privatize_pde`, `paging_copy_window_private`,
  `paging_free_window_private`).
- **fork** — `sys_fork` clones the parent's page directory (`paging_clone_pd`) instead of
  the master, so a child of an exec'd parent inherits the right image.
- **PPID sentinel** — "no parent" is now `-1`, freeing PID 0 to be a real parent; `getppid`
  still reports 0 for shell-launched tasks.

### Job control (signals)

- **`kill` takes a signal** — `SYS_KILL` (53) is now `kill(pid, sig)` in EBX/ECX and
  accepts a negative PID to signal a whole process group (`kill(-pgid, sig)`. The previous
  ABI only read EBX and destroyed the target outright; callers that leave ECX unset still
  get SIGTERM and the same visible result.
- **`sched_signal_task`** — one place decides what a signal means for a task. A task with
  an installed handler is queued (`TCB_SIG_PEND`), and made runnable if it was blocked;
  a task with `SIG_DFL` (or no handler table at all) is still torn down immediately, so
  `kill` stays decisive for programs that never handle signals. A RUNNING target is always
  queued — it is the caller, and freeing the kernel stack we are standing on would be
  fatal.
- **Ctrl+C is SIGINT** — the keyboard handler no longer hard-aborts unconditionally. If
  the foreground program installed a SIGINT handler, the signal is queued and the program
  keeps running; `SIG_IGN` is honoured; `SIG_DFL` (or no handler) still aborts to the shell.
- **SIG_DFL Ctrl+C aborts immediately** — a default-action signal aimed at the running
  foreground task is no longer queued. `sched_kbd_signal` checks the disposition and
  reports the context as gone, so the keyboard handler switches to the shared kernel stack
  and tears the task down through `sched_exit_task`. Previously the SIGINT stayed pending
  until the timer delivered it from inside the very stack it then freed.
- **self-delivery keeps its kernel stack** — `sched_deliver_signals` now knows when it is
  delivering to the task whose frame it was called with (`sched_sig_self`). A default
  terminate then leaves `TCB_KSTACK` allocated; the caller leaves through
  `sched_self_leave`, which switches stacks, frees the page and restores the shell. A
  stopped task keeps its stack, fd view and address space for `fg`, so Ctrl+Z behaves as
  documented.
- **`sys_sigaction` lost the first handler** — the zero-fill of a freshly allocated handler
  table used `rep stosd`, which consumes ECX, and ECX held the handler argument. The first
  `sigaction` call of every task therefore installed `SIG_DFL`, so a task could never catch
  its first signal. ECX is now preserved across the fill.
- **Delivery needs the target's address space** — the signal frame is written onto the
  receiving task's user stack, so `sys_yield`, `sys_waitpid` and the timer switch now
  install the incoming task's page directory and deliver *before* committing anything
  (fd-swap, state change, `TCB_ESP` load). Previously the frame was built through the
  outgoing task's mapping, which corrupted the wrong stack; a candidate killed by a
  default action is now skipped instead of being resumed.
- **`SIGKILL`/`SIGCONT`/`SIGTSTP`** — `sched_signal_task` handles these before the handler
  lookup: SIGKILL bypasses handlers, SIGCONT wakes a stopped task, SIGTSTP stops one (a
  RUNNING target is always queued, never torn down, since it is the caller).
- **`SYS_SIGNAL` is `kill`** — the shell's `kill`/`bg`/`fg` builtins went through a second,
  duplicated implementation that ignored handlers and leaked the fd page, the page
  directory count and the handler table. `sys_signal` is now `jmp sys_kill`.
- **Delivery in `sys_yield`** — a program that yields in a tight loop spends most of its
  time inside the syscall (ring 0), where the timer never advances its quantum, so a
  pending signal was never delivered. Cooperative yields now dispatch pending signals
  themselves for both the running task and the task they switch to.
- **handler table page** — the 4 KB handler table allocated by `sigaction` was never
  freed; `sched_exit_task`, `sched_signal_task` and the default-action path now release it.
- **`sigterm_test`** — new regression program: forks, the child installs a SIGTERM
  handler and spins, the parent puts it in its own process group and sends
  `kill(-pgid, SIGTERM)`; the handler runs and exits 42, and the parent's `waitpid` sees it.
- **`timeout`** — passes SIGTERM explicitly now that the syscall takes a signal.

### Terminal foreground process group

- **`SYS_TCSETPGRP` (183) / `SYS_TCGETPGRP` (184)** — the console now names the process
  group that owns it (`tty_fg_pgid`, `-1` when none). `tcsetpgrp` rejects a group no live
  task belongs to, so a typo cannot leave the terminal permanently unable to deliver
  Ctrl+C. The sentinel is `-1`, not 0: PGIDs are task PIDs and the task in slot 0 really
  has PGID 0.
- **Ctrl+C/Ctrl+Z signal the job, not "whatever is running"** — `sched_kbd_signal` sends
  the signal to the terminal's foreground group (falling back to the running task when no
  group is set) and reports whether the interrupted context survives; the keyboard handler
  returns to it, or restores the shell's fds, address space and TSS when the job died or
  was stopped.
- **Ctrl+Z is `SIGTSTP`** — a stopped job keeps its TCB, fd page and address space, so
  `fg <pid>` (`SIGCONT` + `tcsetpgrp`) and `bg <pid>` (`SIGCONT`) can resume it.
- **shell** — a foreground launch makes the console follow the new task's group; program
  exit clears it, and `fg` re-claims it for the resumed job.
- **`SIGCHLD` default action is "ignore"** — `sched_exit_task` queues SIGCHLD on every
  parent, and `sched_deliver_signals` treated every non-CONT/TSTP default as "terminate".
  Any parent that yielded after a child exited was therefore destroyed (it showed up as a
  process-group signal test hanging, not as a crash). Both the delivery path and
  `sched_signal_task` now ignore SIGCHLD unless a handler is installed.
- **yield with a single task** — `sys_yield`'s "nothing to switch to" fast path returned
  without dispatching pending signals, so the last task in the system could never receive
  one: the timer only advances the quantum when it interrupted ring 3. The fast path now
  delivers before returning.
- **`SYS_GETPID`/`SYS_GETPPID` clobbered `ECX`/`EDI`** — both walked `task_table` with
  those registers and returned them to user space, breaking the rule that `int 0x80`
  reports only in `EAX`. A program that loaded a signal number into `ECX` before
  `SYS_GETPPID` (the obvious way to write `kill(getppid(), SIGUSR1)`) actually called
  `kill(ppid, 1)`; `SIGHUP` is queued for a RUNNING caller and then terminates it at the
  next switch point, so the process simply vanished. Both syscalls now save and restore
  them.
- **`sys_yield` stored a clobbered `ESI` into `task_current`** — `sched_deliver_signals`
  uses `ESI` as scratch while it builds the signal frame, and `.yld_switch` wrote `ESI`
  (the *incoming slot index*) into `task_current` after the call. The signal handler then
  ran with `task_current` pointing at a saved `EIP3` value, and `sys_sigreturn` dereferenced
  it (`#PF` at a wild address that moves with the program). `ESI` now survives the call.
- **`sigret_test`** — new regression program: a handler that *returns* (sigreturn
  trampoline) rather than exiting, which is the shape job control needs.
- **`pgrp_test`** — new regression program: parent + two children in one group,
  `tcsetpgrp`/`tcgetpgrp` round-trip, `kill(-pgid, SIGINT)` reaching every member including
  the caller, and both children exiting through their handler. Each child announces
  readiness with a *different* signal (`SIGUSR1`/`SIGUSR2`): pending signals are a bitmask,
  so two children sending the same one before the parent ran collapsed into a single
  delivery and the parent waited for a second that never came.

### Copy-on-write fork

- **fork() shares frames instead of copying** — `cow_fork_share` privatises the page
  tables covering the shared regions in both directories, clears `PG_WRITABLE` on every
  present PTE and records an extra owner per frame (`cow_ref`, one byte per 4 KB of
  identity-mapped RAM, 0 = not shared). The 64 KB eager stack copy is gone; the child runs
  on the parent's stack until either one writes.
- **`#PF` breaks the share** — `cow_break_page` handles a present + write + user fault:
  two or more owners → copy the frame, drop the old reference, remap writable; exactly one
  owner → the other side already copied, so just re-enable writing; no recorded owners →
  a genuine protection violation (the read-only sigreturn trampoline still faults).
- **Refcount-aware teardown** — `paging_free_window_private` now compares *frames* rather
  than whole PTEs (a COW share has the same frame as the master but different flags) and
  releases through `cow_put`; `sched_exit_task` releases the user stack for every task
  exit, not just background ones (foreground and forked exits used to leak it);
  `paging_copy_window_private` (exec isolation) forces `PG_WRITABLE` on the copies it
  makes and releases the references it replaces.
- **fork inherits the parent's user stack top** — `sys_fork` still copied
  `fork_child_ustop` into `TCB_USTACK`, but that global lost its writer when the eager
  stack allocation was removed, so every child ended up with a zero stack top. `sbrk` /
  `malloc` then failed in every forked child, and the child's stack COW references were
  never released on exit. The child now keeps the parent value the TCB clone inherited.
- **`cow_test`** — new regression program: the child sees the parent's data and stack
  values, its writes to both stay private, and the parent's values are intact afterwards.

### Networking

- **broadcast UDP checksums** — `net_rx_checksum_ok` built its pseudo-header destination
  from `[net_ip]`, so a DHCP OFFER/ACK addressed to 255.255.255.255 failed validation
  before the DHCP handler could run. `ip_handle` now records the destination from the IP
  header (`net_recv_dst_ip`) and the checksum uses it, so broadcast and not-yet-assigned
  destinations validate correctly.

### Storage (HBFS) — crash-consistent metadata

Every metadata mutation now runs inside a write-ahead transaction, and the two paths that
wrote metadata to the wrong place are fixed.

- **The journal never worked** — `hbfs_journal_log_sector` read the sector it was supposed
  to preserve with the destination in `ESI`, but `ata_read_sectors` takes its destination
  in **EDI**; EDI still held the *journal LBA*. The pre-image was therefore read from
  absolute address ~0x1180 (low memory) and journalled as garbage. Replay had the mirror
  bug (`mov edi, target` then `lea esi, buf` before a read): it wrote an unfilled buffer
  to the target LBA. Net effect: replaying a bitmap record replaced the bitmap with a
  journal header, i.e. *every block became free*. Both paths now keep the target LBA in a
  local variable and use EDI/ESI correctly.
- **Metadata is journaled, and only dirty sectors are written** — new
  `hbfs_meta_write(EAX = LBA, ESI = buf, ECX = sectors)` logs each sector's pre-image and
  then writes it. `hbfs_create_file`, `hbfs_delete_file_entry`, `hbfs_mkdir`,
  `hbfs_alloc_blocks`, `hbfs_free_blocks`, `hbfs_save_root_dir`, chmod and chown all go
  through it. Transactions nest (`hbfs_journal_depth`), so "allocate blocks + write the
  directory entry" is one atomic unit: a crash can no longer leave allocated-but-orphaned
  blocks or an entry pointing at unallocated space.
- **A full journal aborts the operation** — `hbfs_journal_log_sector` used to skip the
  record when the log was full, which silently left an un-undoable mutation on disk. It now
  returns CF=1; `hbfs_meta_write` calls the new `hbfs_journal_abort`, which seals the log
  with a commit marker, replays the pre-images it just wrote (undo) and clears the state.
- **Journal epochs** — the log is circular, so after a crash mid-transaction the records of
  the *previous*, already-committed transaction were still sitting behind the new ones with
  valid magic. Recovery replayed them too, rolling the filesystem back to an older state
  (a file entry survived while its block was marked free). Every transaction now increments
  `HBFS_SB_JRNL_EPOCH` (superblock offset 56) and stamps it into every record; recovery stops
  at the first record from a different epoch.
- **A sector is logged once per transaction** — replacing a file writes the bitmap sector
  and the directory sector twice (free then allocate). The log used to keep both
  pre-images, so an undo could restore the *intermediate* state and leave the entry and
  the bitmap disagreeing. `hbfs_journal_logged` remembers the sectors already logged by the
  open transaction and skips repeats.
- **`hbfs_flush_dir_entry` wrote into the root directory regardless of the current
  directory** — it added `HBFS_ROOT_DIR_START` instead of `current_dir_lba`, so changing
  the mode of a file inside a subdirectory overwrote whichever *root* entry happened to
  live at the same sector offset. It now delegates to the diffing writer, which uses the
  real directory LBA.
- **A one-entry metadata change no longer rewrites 128 KB** — `hbfs_save_root_dir` diffs
  `hbfs_dir_buf` against a shadow copy of what is on disk and writes only the 512-byte
  sectors that changed. (`ls`/`chmod`/`create` used to rewrite the whole directory on every
  operation, which was also why it could not be journaled at all.)
- **The allocation bitmap is resident and written by range** — allocations read the whole
  64 KB bitmap from disk every time and wrote all of it back. It is now cached
  (`hbfs_bitmap_valid`) and only the one or two bitmap sectors covering the changed block
  range are written.
- **`fs_meta_test`** — new regression program: writes a file in the root and one in a
  subdirectory, chmods both, and verifies the *other* file is intact after each step
  (this is exactly what the `HBFS_ROOT_DIR_START` bug corrupted), then deletes and
  recreates one of them. Prints the failing stage number.
- **`fs_mkdir_test`** — metadata churn workload (create/chmod/delete × 40) used as the
  crash-injection target.
- **`tools/crash_inject.py` + `make crash-test`** — boots a *copy* of the image, types the
  churn command, `SIGKILL`s QEMU at a random point, then requires `fsck` to pass both
  immediately and again after the kernel has replayed the journal. This is what found the
  replay bug above.
- **`tools/fsck_hbfs.py`** now validates the log: record magic, type, target LBA inside the
  filesystem, and the dirty-state/commit-marker combinations.

### Storage (HBFS) — tooling

- **`tools/qemu_drive.py --wait N`** — the driver used to sample the screen once, ~3 s
  after typing a command, so any program doing real work was reported as a failure. It now
  polls for `--expect` text for up to `--wait` seconds (default 6).

### Programs

- **`breakout`** — the HUD passed column/row to `SYS_FRAMEBUF/3`, which takes pixel
  coordinates, so the score and lives strings overlapped; they now use pixels.
- **`fonttest`** — new diagnostic: renders the kernel's captured VGA font as a
  character sheet in VBE mode (press any key to quit).
- **`curl`** — now works against the current socket ABI: it creates a TCP socket,
  passes `connect(fd, ip, port)` in the documented registers, uses `send`/`recv` with
  buffer/length in ECX/EDX, and treats DNS failure as a 0 return. Previously it never
  created a socket at all, so every request failed with "connection failed".
- **`timeout`** — reaps the child with `SYS_WAIT` (WNOHANG semantics) instead of
  `waitpid`, whose return value cannot distinguish "still running" from "exited with 0".
- **`argv_test`** — new regression program: prints its own arguments, forks, execve's
  itself with different arguments, and waits. Proves per-task argv and `SYS_EXECVE`.

### Syscalls

- **`SYS_EXECVE` (182)** — `execve(path, argv, envp)`. `argv[1..]` is flattened into the
  loader's command line; `SYS_EXEC` and `SYS_EXECVE` share `exec_kernel_line`, so the
  isolation and parent bookkeeping live in one place. `envp` is accepted for POSIX shape
  (the environment stays global for now).
- **per-task argv** — launches snapshot the argument buffer into
  `task_argv_table[PID]`; `SYS_GETARGS` reads the caller's own copy, so a long-running
  task no longer sees another program's arguments.
- **task naming** — the shell names each task after the program it launches, so `ps`
  shows `hello` instead of `-`.

### Testing

- **`make -j check` cannot test an unpopulated image** — the QEMU and fsck targets now
  depend on `.build/populated.stamp`, produced by the populate step and dependent on the
  kernel plus every program binary, instead of only on `$(IMAGE)`. Parallel `check` runs
  no longer race `populate.py`.

- **`qemu_drive.py --screendump FILE`** — captures the framebuffer through the QEMU
  monitor and converts the PPM to PNG in-process, so graphics programs can be verified
  visually even though they never write to the VGA text buffer.
- Added `tests/test_runtime.sh` and `make check-runtime`: a QEMU matrix covering a flat
  binary, the time syscalls, the syscall validation suite, fork + exec + waitpid,
  execve argument passing, VFS files + procfs (`vfstest`), the unified fd table
  (`sockfd_test`), per-task fd views (`fd_isolate_test`), signal delivery through
  `kill` and process groups (`sigterm_test`), a signal handler that returns
  (`sigret_test`), the terminal foreground process group (`pgrp_test`), copy-on-write
  fork (`cow_test`), background-job reaping, and DHCP + DNS. Every case rejects CPU
  exceptions on COM1. `make check` now runs it (skipping cleanly on hosts without QEMU).

## v13.0.0 - Quality gates, benchmark observability, app manifests, new demos

### Overview

This release promotes the project to v13 with a repository-wide consistency
pass plus stronger release engineering. The main additions are version- and
API-consistency validators, benchmark regression/trend tooling, a first pass
at vNext desktop app-manifest infrastructure, and three new demo programs.

### New Programs (`programs/`)

- **`notify_demo.asm`** — Beginner service-API demo for `SYS_NOTIFY` and
  `SYS_CLIPBOARD_COPY` / `SYS_CLIPBOARD_PASTE`. Shows notification posting,
  clipboard round-trip, and simple interactive flow in text mode.

- **`filepick.asm`** — Intermediate dialog-service demo for
  `SYS_FILE_OPEN_DLG` and `SYS_FILE_SAVE_DLG`. Opens a file chooser, reads the
  selected file, previews its first bytes in hex, and writes a saved copy.

- **`netgraph.asm`** — Advanced live network latency visualizer. Repeatedly
  pings a target IP, records RTT samples, and renders a scrolling VGA text-mode
  bar graph of latency and timeouts.

### Infrastructure

- Added version-consistency validation so runtime strings, docs, OCI metadata,
  and bundled first-party manifest versions can be checked mechanically during
  `make lint`.

- Added benchmark tooling in `tests/bench.sh` plus benchmark regression and
  trend analysis helpers. CI now publishes benchmark summaries and raw trend
  artifacts for review.

- Added generated syscall contract artifacts (`docs/syscalls.json` and
  `docs/syscalls.md`) and lint checks that fail when generated API references
  drift from `programs/syscalls.inc`.

- Added app manifest schema and example bundled-app manifests to establish the
  vNext launcher/permission metadata contract.

### Other Changes

- Boot/runtime version strings, documentation current-version references, and
  OCI package metadata have all been aligned to v13.0.0.

## v12.4.0 - Conway's Life 3D, DFT analyser, JSON pretty-printer, C/ASM samples

### Overview

Three new VBE and text-mode programs plus two new C samples completing the
Phase 6 demo application batch.

### New Programs (`programs/`)

- **`life3d.asm`** — Conway's Game of Life on a 40×20 isometric diamond
  tilemap (640×480 VBE). Live cells rendered as raised shadow-glow tiles;
  dead cells as flat dark diamonds. Depth-sorted painter's-algorithm draw
  order. Wrapping toroidal boundary. Press ESC to quit.

- **`fft.asm`** — 16-point DFT spectrum analyser (640×480 VBE bar chart).
  Implements direct-DFT O(N²) with integer fixed-point twiddle tables
  (scale=256). Three test signals selectable at runtime: cosine (energy at
  bins 2 and 14), square wave (odd harmonics), triangle wave (steeper
  roll-off). Bar height scaled to max magnitude; top-pixel highlight per bar.
  Keys 1/2/3 switch signal; ESC quits.

- **`json.asm`** — Single-pass JSON pretty-printer (text mode). State machine
  parses compact JSON and re-emits it with 2-space indentation. Handles
  strings (including backslash escapes), numbers, booleans, null, nested
  objects and arrays. Accepts optional filename argument; falls back to a
  hardcoded demo object.

### New Samples (`samples/`)

- **`json_parse.c`** — Minimal JSON tokenizer in C. Tokenizes a hardcoded
  JSON string and prints each token's type name and value. Handles all
  structural tokens, strings with escape sequences, integers, floats,
  `true`/`false`/`null`. All-global-variable TCC convention; output via
  `putchar()`.
  Compile: `tcc json_parse.c json_parse`

- **`echo_server.c`** — TCP echo server. Binds to port 8080, accepts one
  client at a time, echoes every received byte back, and closes the session
  on an empty line. Uses `__asm__ volatile` inline syscall wrappers for
  `SYS_SOCKET`/`SYS_BIND`/`SYS_LISTEN`/`SYS_ACCEPT`/`SYS_RECV`/`SYS_SEND`.
  Compile: `tcc echo_server.c echo_server` · Test: `chat 127.0.0.1 8080`

---

## v12.3.0 - CHIP-8 emulator, SHA-256, matrix multiply, Mandelbrot C, HTTP Perl

### Overview

New programs and samples: a complete CHIP-8 emulator (`programs/chip8.asm`),
SHA-256 C implementation, 4×4 matrix multiplication, fixed-point Mandelbrot,
and an HTTP/1.1 request-builder Perl demo. Boot banner updated to v12.2.0.

### New Programs (`programs/`)

- **`chip8.asm`** — Full CHIP-8 emulator (1977 Cosmac VIP ISA, all 35 opcodes).
  Loads `.ch8` ROM files; falls back to built-in digits demo. Displays 64×32
  at 8× scale (512×256) centred on a 640×480 VBE framebuffer. Full keypad
  mapping (1-4/Q-R/A-F/Z-V → keys 1-C/4-D/7-E/A-F). ESC to quit.

### New Samples (`samples/`)

- **`sha256.c`** — SHA-256 of the hardcoded string `"Hello, Mellivora!"`.
  Pure C, no stdlib. Implements full message schedule expansion and all 64
  compression rounds using global variables (TCC Mellivora convention).
  Compile: `tcc sha256.c sha256`

- **`matrix_mul.c`** — 4×4 integer matrix multiplication (O(n³) standard
  algorithm). Prints matrices A, B, and C=A×B in a formatted grid.
  Compile: `tcc matrix_mul.c matrix_mul`

- **`mandelbrot_fp.c`** — ASCII Mandelbrot set (70×22) using integer
  fixed-point arithmetic (scale=256, no floating point). Character palette
  maps iteration depth to density: `" .:-=+*#@"` (space is the first character).
  Compile: `tcc mandelbrot_fp.c mandelbrot_fp`

- **`http_get.pl`** — HTTP/1.1 request builder and response parser demo in
  Perl. URL parsing (`http://host:port/path`), request header formatting,
  status-line / header parsing, and chunked transfer-encoding decoding.
  Run: `perl http_get.pl`

### Other Changes

- `kernel/data.inc`: boot banner and `version_text` updated from v12.1.0 to v12.2.0.

---

## v12.2.0 - Phase 1 kernel fixes, pipe expansion, new samples

### Overview

Kernel correctness patch: background-task exit codes are now propagated correctly, SIGCHLD
is delivered to parent tasks on child exit, and the IPC pipe limit is raised from 8 to 32.
Six new sample files are added across C, BASIC, and Perl.

### SIGCHLD delivery on child exit (`kernel/sched.inc`, `kernel/syscall.inc`, `kernel/data.inc`)

Previously `sched_exit_task` marked the dying task as TASK_ZOMBIE and woke any `sys_waitpid`
waiter, but never signalled the parent. Two bugs are fixed together:

- **Exit code was always 0**: `sched_exit_task` stored `mov dword [edi+TCB_EXIT_CODE], 0`
  unconditionally. The real code from `EBX` at `sys_exit` time was already clobbered by the
  page-freeing loop. Fix: `sys_exit` now saves `EBX` to `bg_exit_code` (new BSS dword in
  `kernel/data.inc`) before the first push, and `sched_exit_task` reads it back.
  `sys_waitpid` wake path also fills the waiter's EAX frame slot with the real exit code.

- **SIGCHLD never delivered**: after the waitpid wake scan, `sched_exit_task` now reads
  `TCB_PPID`, scans the task table for the parent, and sets bit 17 (`SIGCHLD`) in
  `TCB_SIG_PEND` — unless the parent explicitly set `SIG_IGN` for SIGCHLD. If the parent is
  TASK_BLOCKED (in `pause`/`sigsuspend`, not `waitpid`), it is also woken.

### Pipe count raised to 32 (`kernel/ipc.inc`)

`IPC_MAX_PIPES` raised from 8 to 32. All bounds checks already use the constant, so no other
code changed. BSS grows by ~384 KB (32 × 16 400-byte pipe structs ≈ 512 KB total). Unblocks
complex shell pipelines with many concurrent stages.

### New samples

#### C (`samples/`)

- **`sort.c`** — selection sort and bubble sort on a 16-element array; demonstrates sorting
  algorithms with the Mellivora TCC C environment.

#### BASIC (`samples/`)

- **`mandelbrot.bas`** — ASCII Mandelbrot set using integer fixed-point arithmetic
  (scale factor 100); 60×22 character grid.
- **`snake.bas`** — interactive snake game using `LOCATE`, `INKEY$`, `DIM`, `SLEEP`, and
  `COLOR`; adjustable-size board; score display.
- **`blackjack.bas`** — blackjack card game with Fisher-Yates shuffle, ace reduction,
  and dealer hit-on-16 rules.

#### Perl (`samples/`)

- **`csv.pl`** — CSV parser: parse, column-extract, filter by score, and sort by column.
- **`regex_demo.pl`** — 14 regex examples: basic match, anchors, capture groups, named
  captures, substitution, trimming, grep/filter, split on regex.

---

## v12.1.0 - Architectural correctness sprint: per-task fd isolation, blocking waitpid, W^X trampoline, errno, bug fixes

### Overview

Corrects eleven architectural and correctness issues identified after the v12.0 POSIX sprint.
No new user-visible syscalls except `SYS_GETERRNO` (181). Build remains a flat NASM binary.

### Per-task file descriptor isolation (kernel/sched.inc, kernel/data.inc, kernel/isr.inc, kernel/syscall.inc)

Previously the global `fd_table` was shared by all 128 tasks, so concurrent tasks could
corrupt each other's open file state. Each task now has a private 4 KB fd-table page.

- **`TCB_FD_TABLE_PTR`** (offset 120) — pointer to task's private fd snapshot page.
- **`sched_create_task`**: allocates and zero-fills a 4 KB PMM page, stores it in `TCB_FD_TABLE_PTR`.
- **`sched_exit_task`** / **`sched_deliver_signals .sdel_default`** / **`sys_kill`**: all
  three task-termination paths now free the private fd page.
- **`sched_swap_fd_tables`** — new helper called at every context switch point
  (`irq_timer`, `sys_yield`, `sys_sleep`, `sys_pause`, `sys_sigsuspend`, `sys_waitpid`):
  saves the global `fd_table` into the outgoing task's private page (or `shell_fd_table`
  when returning to the shell), then loads the incoming task's private page (or
  `shell_fd_table`) into `fd_table`.
- **`shell_fd_table`** (kernel/data.inc): new 256-byte BSS array holds the shell's own fd
  snapshot so the shell's state persists across task switches.

### `TCB_SIZE` grows from 116 to 128 bytes

Three new fields appended to the TCB:

| Field | Offset | Description |
| --- | --- | --- |
| `TCB_WAIT_PID` | 116 | PID being waited on in `sys_waitpid` (0 = not waiting) |
| `TCB_FD_TABLE_PTR` | 120 | Physical address of private fd-table page |
| `TCB_ERRNO` | 124 | Per-task `errno` value |

### Blocking `sys_waitpid` (kernel/sched.inc)

The previous implementation spun up to 2 000 times calling `int 0x80 / SYS_YIELD` from
ring-0, which is a no-op on this kernel (PIT only preempts ring-3 code). Ring-3 callers
would effectively hang the system.

- Ring-3 callers are now **truly blocked**: `TCB_STATE = TASK_BLOCKED`, `TCB_WAIT_PID`
  set to the target PID (or -1 for any child), ESP saved in `TCB_ESP`, and the scheduler
  immediately switches to the next READY task. If no task is ready, execution returns to
  the shell (which runs ring-0 and is never preempted).
- `sched_exit_task` scans for any `TASK_BLOCKED` task whose `TCB_WAIT_PID` matches the
  dying child's PID, writes the exit code into its saved `[TCB_ESP+28]` (EAX slot), sets
  the task `TASK_READY`, and clears `TCB_WAIT_PID`. The woken task resumes from its
  `iretd` with the correct exit code in EAX.
- Shell (ring-0) callers fall through immediately with -1 if the child is not already a
  zombie (non-blocking single scan only).

### Read-only signal trampoline / W^X fix (kernel/paging.inc, kernel/sched.inc)

The sigreturn stub (`MOV EAX,144; INT 0x80; NOP`) was previously written directly to the
user stack on every signal delivery, making the user stack both writable and executable
(W^X violation). The stack is no longer executable in systems that enforce it.

- **`sigreturn_trampoline_init`** (kernel/paging.inc): allocates one physical page, writes
  the 8-byte stub, then maps it **read-only** (`PG_PRESENT | PG_USER`, no `PG_WRITABLE`)
  at `SIGRETURN_TRAMPOLINE_VADDR = 0x1FFFF000` (last page of the 128–512 MB demand zone).
  Called from `paging_init` after paging is enabled.
- `sched_deliver_signals` now sets `[new_esp + 0] = SIGRETURN_TRAMPOLINE_VADDR` instead
  of a stack-local return address, and zeros the previously-used stub bytes `[new_esp+24..31]`.

### `sys_geterrno` — per-task errno (kernel/syscall.inc, kernel.asm)

- New syscall **181 `SYS_GETERRNO`**: returns `TCB_ERRNO` for the running task (0 from shell).
- Errno constants added to `kernel.asm`: `ENOENT=2`, `EBADF=9`, `ENOMEM=12`, `EACCES=13`,
  `EFAULT=14`, `EINVAL=22`, `EMFILE=24`, `ENOTTY=25`, `ENOSYS=38`.

### `sys_fcntl` fd validation fix (kernel/syscall.inc)

- Added `cmp ebx, FD_MAX; jge .fcntl_badf` bounds check at entry.
- `F_GETFL` and `F_SETFL` now check `FD_FLAG_CLOSED` and return -1 for closed fds.

### `sys_ioctl` security and correctness fix (kernel/syscall.inc)

- **`TIOCGWINSZ`**: calls `validate_user_ptr(EDX)` before writing `winsize` struct.
  A null or out-of-range pointer now returns -1 instead of faulting or corrupting kernel memory.
- Unknown ioctl requests now return **-1** (was 0, masking errors from callers).

### `sys_mmap` / `sys_munmap` register fixes (kernel/syscall.inc)

- **`sys_mmap`**: aligned byte length is now right-shifted by 12 (`shr eax, 12`) before
  passing to `pmm_alloc_pages`, which expects a *page count*, not a byte count.
- **`sys_munmap`**: `EAX = EBX` (address) and `ECX = page count` are now set correctly
  before calling `pmm_free_pages`. Previous code had a no-op `mov ebx, ebx` and left
  EAX/ECX undefined.

### `sys_ftruncate` block deallocation (kernel/syscall.inc)

When the new size is smaller than the current size, excess HBFS blocks are now freed:

```text
if new_block_count < old_block_count:
    hbfs_free_blocks(start_block + new_count, old_count - new_count)
    fd_entry[block_count] = new_block_count
fd_entry[size] = new_size
```

### `sys_recvfrom` from-address fill (kernel/syscall.inc)

Previously a stub (`jmp sys_recv`) that silently dropped the `from*` argument. Now a full
implementation: performs the same receive logic as `sys_recv`, then—if `EDI` (the `from*`
pointer) is non-null and passes `validate_user_ptr`—fills a `sockaddr_in` with
`AF_INET`, `SOCK_REMOTE_PORT`, and `SOCK_REMOTE_IP` from the socket struct.

---

## v12.0.0 - POSIX compliance sprint: 36 new syscalls (145–180), TCB extension

### Overview

This release makes a major step toward POSIX compliance by adding 36 new system calls
covering FD operations, session management, signal extensions, environment queries,
identity management, networking extensions, and terminal (termios) control.

### TCB extension (kernel/sched.inc)

`TCB_SIZE` grows from 88 to 116 bytes. Seven new fields:

| Field | Offset | Description |
| --- | --- | --- |
| `TCB_EUID` | 88 | Effective UID |
| `TCB_EGID` | 92 | Effective GID |
| `TCB_SID` | 96 | Session ID |
| `TCB_UMASK` | 100 | File creation mask |
| `TCB_ITIMER_VAL` | 104 | Interval timer current value (ticks) |
| `TCB_ITIMER_INT` | 108 | Interval timer interval (ticks) |
| `TCB_SAVED_MASK` | 112 | Saved signal mask (for `sigsuspend`) |

`sched_create_task` initialises: `EUID=UID`, `EGID=GID`, `SID=own PID`, `UMASK=022`, itimer fields=0.
`sys_fork` child inherits EUID/EGID and clears itimer and saved mask.

### Scheduler fixes (kernel/sched.inc, kernel/isr.inc)

- **`sched_wake_sleepers`**: added guard `TCB_WAKEUP==0 → skip` so tasks blocked
  indefinitely (via `pause`/`sigsuspend`) are not prematurely woken by the clock.
- **`sys_signal` wake path**: when a non-SIGKILL/non-term signal is delivered to a
  `TASK_BLOCKED` task (e.g. sleeping in `pause`), the task is set `TASK_READY` so it
  gets scheduled and `sched_deliver_signals` fires.
- **`sched_check_itimers`**: new function, called from `irq_timer` after `sched_check_alarms`,
  decrements `TCB_ITIMER_VAL` for each non-free task; when it reaches 0, sets SIGALRM
  pending, wakes blocked tasks, and reloads from `TCB_ITIMER_INT` (0 = one-shot, disarm).

### New syscalls (kernel/syscall.inc, programs/syscalls.inc)

#### FD operations (145–151)

| # | Name | Description |
| - | ---- | ----------- |
| 145 | `SYS_FSTAT` | stat an open fd → fills 12-byte stat_buf |
| 146 | `SYS_FTRUNCATE` | truncate file by fd to new size (in-memory) |
| 147 | `SYS_FCHMOD` | change mode by fd (stub, accepted) |
| 148 | `SYS_FCHOWN` | change owner by fd (stub, accepted) |
| 149 | `SYS_FSYNC` | flush fd to storage (write-through, no-op) |
| 150 | `SYS_LINK` | hard-link: creates new dirent sharing same blocks |
| 151 | `SYS_ISATTY` | returns 1 for fd 0–2 (stdin/stdout/stderr) |

#### Session management (152–156)

| # | Name | Description |
| - | ---- | ----------- |
| 152 | `SYS_SETSID` | create new session (fails if already group leader) |
| 153 | `SYS_GETSID` | get session ID by PID (0 = self) |
| 154 | `SYS_WAIT` | reap any zombie child (`WNOHANG` semantics) |
| 155 | `SYS_PAUSE` | block until any signal arrives (returns −1/EINTR) |
| 156 | `SYS_UMASK` | set/query file creation mask; pass 0xFFFFFFFF to query |

#### Signal extensions (157–160)

| # | Name | Description |
| - | ---- | ----------- |
| 157 | `SYS_SIGPENDING` | write pending unblocked signals to caller's sigset |
| 158 | `SYS_SIGSUSPEND` | atomically set mask, block until signal; restores old mask |
| 159 | `SYS_SETITIMER` | set/query `ITIMER_REAL` interval timer (usec precision, 100 Hz) |
| 160 | `SYS_GETITIMER` | query `ITIMER_REAL` current value |

#### Environment / system info (161–165)

| # | Name | Description |
| - | ---- | ----------- |
| 161 | `SYS_SETENV` | set/add environment variable with optional overwrite |
| 162 | `SYS_UNSETENV` | remove environment variable |
| 163 | `SYS_UNAME` | fill utsname: `Mellivora` / `12.0.0` / `i386` |
| 164 | `SYS_UTIME` | update file mtime (utimbuf or current timestamp) |
| 165 | `SYS_SYSCONF` | query `_SC_*` constants: ARG_MAX, CLK_TCK, OPEN_MAX, etc. |

#### Identity (166–169)

| # | Name | Description |
| - | ---- | ----------- |
| 166 | `SYS_SETEUID` | set effective UID (root or matching real UID allowed) |
| 167 | `SYS_SETEGID` | set effective GID |
| 168 | `SYS_SETREUID` | set real+effective UID (−1 = keep current) |
| 169 | `SYS_SETREGID` | set real+effective GID |

#### Networking extensions (170–176)

| # | Name | Description |
| - | ---- | ----------- |
| 170 | `SYS_SENDTO` | send with optional UDP destination; delegates to `sys_send` |
| 171 | `SYS_RECVFROM` | receive; delegates to `sys_recv` |
| 172 | `SYS_SETSOCKOPT` | set socket option (`SO_REUSEADDR` stored, rest accepted) |
| 173 | `SYS_GETSOCKOPT` | get `SO_TYPE` (SOCK_STREAM/SOCK_DGRAM) |
| 174 | `SYS_GETSOCKNAME` | return local `sockaddr_in` (local port, `0.0.0.0`) |
| 175 | `SYS_GETPEERNAME` | return remote `sockaddr_in` |
| 176 | `SYS_SHUTDOWN` | close one or both socket halves (`SHUT_RD/WR/RDWR`) |

#### Terminal control (177–180)

| # | Name | Description |
| - | ---- | ----------- |
| 177 | `SYS_TCGETATTR` | return cooked-mode termios for fd 0–2 |
| 178 | `SYS_TCSETATTR` | store termios in `tty_termios` shadow (fd 0–2) |
| 179 | `SYS_TCDRAIN` | no-op (write-through I/O) |
| 180 | `SYS_TCFLUSH` | no-op |

### New constants (programs/syscalls.inc)

`SYS_FSTAT=145` … `SYS_TCFLUSH=180`, plus: `_SC_ARG_MAX`, `_SC_CLK_TCK`, `_SC_OPEN_MAX`,
`_SC_PAGESIZE`, `ITIMER_REAL`, `SHUT_RD/WR/RDWR`, `AF_INET`, termios flag constants.

### Data (kernel/data.inc)

- `tty_termios`: 36-byte BSS buffer for `tcsetattr`/`tcgetattr` shadow.

---

## v11.0.0 - User-space signal delivery, AHCI/e1000 drivers, GDB stub, 144 syscalls

### User-space signal handlers

- **`TCB_SIG_HAND`** (`kernel/sched.inc`) — new TCB field (offset 84) holds a pointer to a
  lazily-allocated 4 KB PMM page used as a 32-dword handler table (one slot per signal).
  `TCB_SIZE` grows from 84 to 88 bytes.  Child tasks start with `TCB_SIG_HAND = 0`
  (table not yet allocated); `sys_fork` clears the field so children inherit only default
  actions, not handler function pointers.
- **`SIG_DFL` / `SIG_IGN`** — new constants (`sched.inc` / `syscalls.inc`).  `SIG_DFL = 0`
  means restore the kernel default action; `SIG_IGN = 1` silently discards the signal.
- **`sched_deliver_signals`** (`kernel/sched.inc`) — called by `irq_timer` every context
  switch with `EBX = TCB pointer` and `EDI = iretd frame base address`.  For each pending,
  unmasked signal it builds a 28-byte *signal frame* on the user stack:

  ```text
  [new_esp +  0]  return address  → points to inline sigreturn stub
  [new_esp +  4]  signum          (cdecl argument)
  [new_esp +  8]  saved EIP       (interrupted ring-3 EIP)
  [new_esp + 12]  saved EFLAGS
  [new_esp + 16]  saved ESP       (original user stack pointer)
  [new_esp + 20]  stub: MOV EAX,144 / INT 0x80 / NOP (8 bytes)
  ```

  The iretd frame `EIP3` is redirected to the user handler and `ESP3` is set to `new_esp`.
  When the handler returns it lands on the stub which transparently invokes `sys_sigreturn`.
  Default actions: SIGKILL/SIGTERM/SIGINT → terminate task; SIGTSTP → pause; SIGCONT → no-op;
  all others → terminate.
- **`irq_timer`** (`kernel/isr.inc`) — updated to call `sched_deliver_signals` on both the
  "stay on current task" and "switch to new task" paths before performing `iretd`.

### New syscalls

- **`SYS_SIGACTION` (143)** — install or query a signal handler.
  `EBX = signum`, `ECX = handler (fn ptr / SIG_DFL / SIG_IGN)`, `EDX = ptr to receive old
  handler (or 0)`.  Validates signum, rejects SIGKILL.  Allocates the 4 KB handler table
  on first use (zero-filled = all SIG_DFL).  Returns 0 on success, -1 on error.
- **`SYS_SIGRETURN` (144)** — return from a signal handler.  Called automatically by the
  inline stub on the user stack.  Restores the saved EIP, EFLAGS, and ESP from the signal
  frame, discarding the signal stack entirely.  No user arguments needed.

### Kernel drivers (carried from previous development sprint)

- **AHCI SATA driver** (`kernel/ahci.inc`) — full AHCI HBA detection via PCI scan; maps
  one AHCI port (first populated port found), initialises command-list / FIS / command-table
  in PMM-allocated memory, and exposes `ahci_read_sectors`, `ahci_write_sectors`, and
  `ahci_flush`.  `ahci_present` flag lets VFS/HBFS fall back to legacy ATA when no AHCI
  controller is available.
- **Intel e1000 NIC driver** (`kernel/e1000.inc`) — probes PCI for common Intel GbE device
  IDs (82540EM, 82545EM, 82543GC, 82544GC); reads MAC from `RAL0`/`RAH0`; initialises 8
  TX and 8 RX descriptors with 2048-byte buffers; hooks into the existing `net_send_frame`
  dispatcher (`NIC_TYPE_E1000 = 2`).
- **GDB remote stub** (`kernel/gdbstub.inc`) — hooks INT 1 (single-step) and INT 3
  (breakpoint) via `idt_set_gate`; implements the GDB RSP packet protocol over COM1
  (blocking I/O); supports `?`, `g`, `G`, `p`, `P`, `m`, `M`, `c`, `s`, `Z0`, `z0`,
  and `k` commands; 32 software breakpoint slots; 1024-byte packet buffer.

### New programs

- **`trap`** — demo that installs a SIGUSR1 handler via `SYS_SIGACTION`, prints its PID,
  sleeps in a loop, and prints a message each time the signal is delivered.  Exits after
  three received signals.  Useful for testing the signal delivery path end-to-end.

### API changes

- `programs/syscalls.inc` — new constants `SYS_SIGACTION = 143`, `SYS_SIGRETURN = 144`,
  `SIG_DFL = 0`, `SIG_IGN = 1`.
- Syscall table now has 144 defined entries (was 142).

---

## v10.0.0 - VFS layer, HBFS v3, compositor IPC, 140 syscalls, package manager

### Boot & architecture


### Virtual filesystem (`kernel/vfs.inc`)

- **VFS abstraction layer** — unified `vfs_open / vfs_read / vfs_write / vfs_readdir /
  vfs_stat / vfs_mkdir / vfs_unlink` front-ends dispatch to pluggable backend drivers via
  a mount table (up to 8 mounts, `VFS_MAX_MOUNTS`).
- **Four built-in backends** — HBFS (`/`), procfs (`/proc`), devfs (`/dev`), tmpfs (`/tmp`).
- **procfs** — `/proc/uptime`, `/proc/meminfo`, `/proc/version`, `/proc/cpuinfo`, and
  `/proc/<pid>/status` (reports task name from TCB).
- **devfs** — `/dev/null`, `/dev/zero`, `/dev/random` (LCG), `/dev/tty` (serial loopback).
- **tmpfs** — in-memory files backed by PMM pages; up to `TMPFS_MAX_FILES` entries.
- **`/dev/full`** — new devfs node (`DEV_FULL`); reads return zero bytes (like `/dev/zero`),
  writes always return -1 (ENOSPC simulation); enumerated by `vfs_dev_readdir`.
- **64 virtual file descriptors** (`VFS_MAX_FDS`, `VFD_SIZE = 32`) per system (expanded
  from per-process FD_MAX = 8 for legacy syscalls).
- **VFS-aware shell navigation** — `cmd_cd_internal` (`.cd_enter_subdir`) now recognises
  `/proc`, `/dev`, and `/tmp` as valid virtual-directory targets at root level; sets
  `current_dir_lba = 0xFFFFFFFF` (sentinel) so `build_cwd_path` produces e.g. `/proc`
  which `vfs_route` correctly dispatches to procfs.  Guard at the top of `.cd_enter_subdir`
  prevents HBFS ATA reads when the CWD is a virtual directory.
- **VFS-aware `ls`** — `cmd_list_dir` detects the `0xFFFFFFFF` sentinel LBA and, instead
  of calling `hbfs_load_root_dir`, iterates `vfs_readdir` in a loop to list entries from
  the active VFS backend (procfs, devfs, or tmpfs).

### HBFS v3 (`kernel/hbfs.inc`, `kernel.asm`)

- **File permissions** (`HBFS_DE_MODE`) — Unix-style 12-bit mode field in every directory
  entry; `hbfs_check_permission(EBX=dirent, ECX=requested)` enforces owner/group/other
  rwx bits.
- **Extended attributes** (`HBFS_DE_XATTR_LBA`, `HBFS_DE_XATTR_COUNT`) — each file can
  have an xattr block holding up to `HBFS_XATTR_PER_BLOCK` key/value pairs;
  `hbfs_xattr_get` and `hbfs_xattr_set` are the kernel API;
  `SYS_GETXATTR (141)` / `SYS_SETXATTR (142)` expose them to user-space programs.
  `sys_setxattr` upgrades the on-disk filesystem from v2 to v3 and sets
  `HBFS_FEAT_XATTR` in the superblock on first use.  `sys_getxattr` checks the
  feature flag and returns -1 on a filesystem that has never written any xattrs.
- **Feature flags** (`HBFS_SB_FEATURES`) — `HBFS_FEAT_JOURNAL` (existing), `HBFS_FEAT_XATTR`,
  `HBFS_FEAT_PERM`; superblock validated on mount.
- **Dirent layout v3** — new constants `HBFS_DE_NAME=0`, `HBFS_DE_TYPE=253`,
  `HBFS_DE_SIZE=256`, `HBFS_DE_BLOCK=260`, `HBFS_DE_MTIME=272`, `HBFS_DE_MODE=276`,
  `HBFS_DE_XATTR_LBA=280`, `HBFS_DE_XATTR_COUNT=284`; entry size raised to
  `HBFS_DIR_ENTRY_SIZE = 288`.
- **`hbfs_xattr_set` key-update fix** — the scan loop now performs a real `repe cmpsb`
  key comparison against each occupied slot; on a match the value field is updated
  in-place instead of creating a duplicate entry (previously the comparison was a no-op
  stub).  Stack-offset bugs in the new-slot write path (wrong frame offsets for `key_ptr`
  and `val_ptr` after two intermediate pushes) are also corrected; key and value pointers
  are now saved to static locals (`hxs_key_ptr`, `hxs_val_ptr`) immediately after
  `pushad` to avoid frame-offset arithmetic errors.
- **`sys_defrag` Phase 2 — block relocation** (`SYS_DEFRAG = 104`) — replaces the v9 stub
  that only counted fragmentation.  For each non-free, non-directory directory entry the
  defragmenter scans the bitmap for the lowest free contiguous run entirely before the
  file's current start block; if found it copies all file sectors using `hbfs_block_buf`
  as a 512-byte staging buffer, updates the bitmap (destination bits set before source
  bits cleared to prevent double-use), and writes the new start block back to the
  directory entry.  Phase 1 fragmentation counting is retained; EAX returns the frag-run
  count measured before relocation.  Bitmap and root directory are flushed to disk, and
  the superblock is journalled on HBFS v2+ volumes.
- **Journal version check fix** — all journal entry points (`hbfs_journal_begin`,
  `hbfs_journal_log_sector`, `hbfs_journal_commit`, and `sys_defrag`) previously accepted
  only `HBFS_VERSION_V2` exactly.  Comparisons changed to `jb HBFS_VERSION_V2` so that
  v3 filesystems receive identical journal protection; v1 volumes continue to skip the
  journal.  `hbfs_init` likewise uses `jb` so v3 mounts trigger journal-recovery scanning
  on unclean shutdown.

### Syscall interface (`kernel/syscall.inc`, `programs/syscalls.inc`)

- **142 syscalls** — table extended to 256 entries; new POSIX-compat range 116–142:
  - `SYS_DUP (116)`, `SYS_DUP2 (117)`, `SYS_FCNTL (118)`, `SYS_IOCTL (119)`,
    `SYS_MMAP (120)`, `SYS_MUNMAP (121)`, `SYS_MPROTECT (122)`, `SYS_SELECT (123)`,
    `SYS_CLOCK_GETTIME (124)`, `SYS_NANOSLEEP (125)`, `SYS_GETTIMEOFDAY (126)`,
    `SYS_GETUID (127)`, `SYS_SETUID (128)`, `SYS_GETGID (129)`, `SYS_SETGID (130)`,
    `SYS_GETEUID (131)`, `SYS_GETEGID (132)`, `SYS_ACCESS (133)`, `SYS_PIPE2 (134)`,
    `SYS_SURFACE_CREATE (135)`, `SYS_SURFACE_COMMIT (136)`, `SYS_SURFACE_DESTROY (137)`,
    `SYS_SURFACE_MOVE (138)`, `SYS_SURFACE_RESIZE (139)`, `SYS_ALARM (140)`,
    `SYS_GETXATTR (141)`, `SYS_SETXATTR (142)`.
- **`SYS_CHMOD (68)` / `SYS_CHOWN (69)` upgraded to v3** — syscall table entries 68/69
  now dispatch to `sys_chmod_v3` / `sys_chown_v3`, which perform a real read-modify-write
  of the on-disk directory sector via `hbfs_flush_dir_entry`; they also auto-upgrade the
  superblock to `HBFS_VERSION_V3` and set `HBFS_FEAT_PERMS` on first use.
- **`SYS_GETXATTR (141)` / `SYS_SETXATTR (142)`** — user-space interface to the HBFS v3
  xattr kernel API (`hbfs_xattr_get` / `hbfs_xattr_set`).  Both validate all three user
  pointers before touching filesystem state.  `SYS_SETXATTR` upgrades the filesystem to
  v3 and sets `HBFS_FEAT_XATTR` in the superblock on first use; the updated directory
  entry (with new `XATTR_LBA` / `XATTR_COUNT`) is persisted via `hbfs_save_root_dir`.
- **`programs/syscalls.inc`** — constant definitions for all 142 syscalls.
- **`SYS_NANOSLEEP (132)` improved** — now delegates to `sys_sleep` after converting the
  timespec to PIT ticks (100 Hz); uses `TASK_BLOCKED` + `sched_wake_sleepers` when the
  scheduler is active, falls back to a `hlt`-loop in single-task context.
- **`SYS_MPROTECT (123)` real PTE walk** — replaces previous stub; walks the two-level
  page directory (PDI = virt >> 22, PTI = (virt >> 12) & 0x3FF) for every page in the
  requested range; sets/clears `PG_WRITABLE` and `PG_PRESENT` in each PTE according to
  the `PROT_READ / PROT_WRITE / PROT_EXEC / PROT_NONE` flags; issues `invlpg` after each
  PTE update; returns `EFAULT` if any page is not present.
- **`SYS_GETUID/GETEUID/GETGID/GETEGID/SETUID/SETGID`** — syscalls now read and write
  `TCB_UID` / `TCB_GID` fields in the running task's TCB; `setuid`/`setgid` enforce POSIX
  rules (root UID == 0 may set any value; non-root may only re-set their own ID).

### Memory management (`kernel/pmm.inc`, `kernel/sched.inc`)

- **PMM buddy coalescing** (`pmm_free_coalesce`) — when a page (or page range) is freed,
  the allocator now searches the free list of the same buddy order for the freed block's
  buddy partner (XOR of page number with `1 << order`).  On match the buddy is unlinked
  and the merged block is promoted to `order + 1` and the coalesce loop repeats up to
  `BUDDY_ORDERS - 1`; bitmap bits are cleared as part of the same operation.
  `pmm_free_page` and `pmm_free_pages` both call `pmm_free_coalesce` instead of
  `pmm_push_free_block`, resulting in maximal free-block consolidation at every free.
- **Multi-user TCB fields** — `TCB_UID = 72`, `TCB_GID = 76` added to the Task Control
  Block; `TCB_SIZE` raised from 72 to 80 bytes.  New tasks inherit UID/GID = 0 (root) via
  the existing `rep stosd` zero-initialisation in `sched_create_task`.
- **`TCB_ALARM` field + `sys_alarm` (SYS_ALARM = 140)** — `TCB_ALARM = 80` stores a PIT
  tick deadline (0 = no alarm); `TCB_SIZE` raised to 84 bytes.  `sched_check_alarms()` is
  called from `irq_timer` on every tick and sets `TCB_SIG_PEND |= (1 << SIGALRM)` when the
  deadline is reached, then clears `TCB_ALARM`.  `sys_alarm(seconds)` programs the alarm
  and returns the number of seconds remaining before any previously-set alarm would have
  fired (0 if none); passing 0 cancels any pending alarm.  Child tasks created by `fork`
  do not inherit the parent's alarm.  `SYS_ALARM = 140` added to `programs/syscalls.inc`.
- **`sys_access` fix** (`SYS_ACCESS = 133`) — corrected three bugs: (1) missing
  `hbfs_load_root_dir` call before `hbfs_find_entry` (the directory buffer could be stale);
  (2) wrong `pushad`-frame stack offset (`[esp+8]` = EBP instead of `[esp+24]` = ECX) used
  to read the POSIX mode argument; (3) hardcoded UID = 0 replaced with real `TCB_UID` /
  `TCB_GID` read from the running task's TCB via `sched_get_current_task`.

### Networking (`kernel/net.inc`)

- **ICMPv6 echo reply checksum** (RFC 4443) — `ipv6_handle` now computes the correct
  one's-complement checksum over the ICMPv6 pseudo-header (source address, destination
  address, payload length, next-header = 58) plus the full ICMPv6 payload before
  transmitting each echo reply; the checksum field at ICMPv6 header offset +2 is written
  in-place after folding the 32-bit accumulator to 16 bits.

### Shell (`kernel/shell.inc`)

- **`ls -s` sort-by-size** — new flag to `cmd_list_dir`; collects pointers to all valid
  HBFS directory entries into `path_search_buf`, insertion-sorts them descending by file
  size (`[entry + 256]`), then displays the sorted list in long format (type indicator,
  right-aligned 9-digit size, `YYYY-MM-DD HH:MM` timestamp, filename).  Uses
  index-based array access (`path_search_buf + 4 + ebx*4`) with `pushad`/`popad` register
  discipline to avoid clobbering the VGA print register contract under NASM `-O0`.

### Compositor IPC (`kernel/ipc.inc`)

- **Compositor surface API** — kernel-managed pixel buffers (`ipc_surface_table`,
  `SURF_MAX = 16`); surfaces have id, owner PID, width/height, z-order, position, and a
  PMM-backed pixel buffer.  `ipc_surface_create / commit / destroy / move / resize`
  implement the five new syscalls.
- **`vbe_compositor_refresh`** — wakes the Burrows compositor task (by PID stored in
  `burrows_compositor_pid`) whenever a surface is created, committed, moved, or destroyed.
- **`ipc_find_surface`** — helper to locate a surface entry by ID in O(SURF_MAX) time.

### Package manager (`tools/hbpkg.c`)

- **`hbpkg`** — self-contained C tool (compiled by `make hbpkg`) to create, inspect, and
  install `.hbpkg` archives.  Supports `create <dir> <pkg>`, `inspect <pkg>`, and
  `install <pkg> <dest>` subcommands.  Header: magic `HBPKG`, version, file count, and
  per-file records (path + size + data).

### Build system (`Makefile`)

- **`make hbpkg`** — compiles `tools/hbpkg.c` to `tools/hbpkg`.
- **`make count`** — now counts `.c` source files alongside `.asm` / `.inc`.

---

## v9.0.0 - Buddy allocator, demand paging, fork(), HBFS v2, GUI enhancements, IPv6, TLS, POSIX msgq

### Memory management (kernel/pmm.inc, kernel/paging.inc)

- **Buddy allocator** — `pmm.inc` now implements a two-level buddy system (order 0 = 4 KB,
  order 9 = 2 MB) on top of the bitmap-based free-page tracker.  `pmm_alloc_pages(n)` and
  `pmm_free_pages(base, n)` coalesce buddies on free.  Page-fault handler integrated.
- **Demand paging** — `paging.inc` adds a demand-paging region per process starting at
  `USER_DEMAND_BASE (0x08000000)`.  Page-fault handler (#PF via IDT vector 14) maps physical
  pages on first access.

### Process model (kernel/sched.inc, kernel/syscall.inc)

- **`SYS_FORK (103)`** — duplicates calling process's page-directory, stack, and register
  state.  Returns child PID to parent, 0 to child.  Child is immediately runnable.
  (`SYS_EXEC (21)` and `SYS_WAITPID (92)` were already present; both are used by `vsh`.)

### File system (kernel/hbfs.inc)

- **HBFS v2 journal** — 16-entry write-ahead journal records `(block, old_data, new_data)`
  before every metadata write; `hbfs_journal_replay` replays or discards on mount.
- **Defragmenter** — `hbfs_defrag` compacts live blocks toward LBA 0; exposed as
  `SYS_DEFRAG (104)`.
- **Limits raised** — max filename length 64 B → 128 B; max open files 8 → 16.

### Burrows desktop (kernel/burrows.inc)

- **Virtual desktops** — 4 workspaces; Super+1..4 to switch, Super+Shift+1..4 to move a
  window.  Active desktop stored per window; switching hides/shows without destroying state.
- **Alpha-blend** — `gui_alpha_blend_rect(x, y, w, h, color, alpha)` porter-duff over the
  framebuffer; used for window shadows and notification badges.  Fixed 32-bit register usage
  (`sil`/`dil` replaced with `mov+and` idiom for 32-bit NASM compatibility).
- **Edge resize** — 6-pixel resize border on all four window edges; cursor changes to resize
  arrow in border regions.

### Terminal (programs/bterm.asm)

- 80×24 character cell grid with 200-line scrollback ring buffer.
- ANSI SGR colour (`\e[30m`–`\e[37m` fg, `\e[40m`–`\e[47m` bg, `\e[0m` reset).
- PgUp / PgDn scroll through scrollback; Home returns to bottom.

### Networking (kernel/net.inc)

- **IPv6 / ICMPv6** — Ethernet type 0x86DD demux; `ipv6_handle` processes 40-byte fixed
  header; ICMPv6 echo-request/reply (types 128/129) fully handled.
- **TLS stub** — `SYS_TLS_CONNECT (105)`: EBX=ip, ECX=port, EDX=handle_ptr → EAX=0/-1.
  Performs TCP connect then records `(ip, port)` for subsequent send/recv.

### Audio (kernel/ac97.inc)

- AC'97 mixer channel: treble, bass, PCM-in gain knobs wired.
- PCM recording: `SYS_AUDIO_REC_START (106)`, `SYS_AUDIO_REC_READ (107)`,
  `SYS_AUDIO_REC_STOP (108)`.
- Mixer channels: `SYS_AUDIO_OPEN (109)`, `SYS_AUDIO_WRITE (110)`,
  `SYS_AUDIO_CLOSE_CHAN (111)`.

### IPC (kernel/ipc.inc)

- **POSIX message queues** — kernel-managed pool of 8 queues, 16 messages × 256 B each.
  `SYS_MSGQ_CREATE (112)`, `SYS_MSGQ_SEND (113)`, `SYS_MSGQ_RECV (114)`,
  `SYS_MSGQ_CLOSE (115)`.

### New programs (v9.0.0)

- **`chess`** — VBE 1024×768 chess game with greedy-material AI.  Arrow keys to move cursor,
  Enter to select/place piece, R to reset, ESC to quit.
- **`vsh`** — Veritas Shell: fork/exec model, 24-entry command history (↑/↓), built-ins
  `cd`, `echo`, `history`, `uname`, `help`, `exit`.
- **`curl [-X POST] [-d data] host path`** — HTTP/1.0 GET or POST client over raw TCP.
  Uses `SYS_DNS`, `SYS_SOCKET`, `SYS_CONNECT`, `SYS_SEND`, `SYS_RECV`.
- **`rec [-t secs] [-o file]`** — Records PCM audio via `SYS_AUDIO_REC_*` and writes a
  valid 44-byte RIFF/WAV file.
- **`mix file1 file2`** — Mixes two 16-bit mono WAV files sample-by-sample (clamp to
  ±32767) and plays the result through an AC'97 mixer channel.

### Enhanced programs

- **`strace`** — now uses `SYS_FORK` to trace a child process; highlights fork events in
  yellow; `SYS_WAITPID` loop monitors child exit.

### Syscall table

Slots 0–115 fully wired.  New additions this release: 103 (fork), 104 (defrag),
105 (tls_connect), 106–108 (audio rec), 109–111 (audio mixer), 112–115 (msgq).

---

## v8.5.0 - Expanded graphics, audio, and I/O subsystems

### New modules

- **`kernel/pci.inc`** — Generic PCI bus enumeration (buses 0–7).  Scans all
  devices/functions, builds a 64-entry device table.  Exposes `pci_find_device`,
  `pci_read_config32`, `pci_write_config32`, `pci_read_bar`.
  New syscall **SYS_PCI_FIND (101)**: EBX=vendor, ECX=device_id → EAX=bdf or -1.

- **`kernel/ac97.inc`** — Intel AC'97 audio controller driver (ICH2/3/4,
  8086:2415/2425/2445).  Uses PCI Bus Master DMA for playback.  Allocates
  32-entry Buffer Descriptor List and two 32 KB ping-pong buffers.
  Falls back gracefully if device absent.  Hooks into `SYS_AUDIO_PLAY`.

- **`kernel/atadma.inc`** — ATA Bus Master IDE DMA extension.  Detects Intel
  PIIX3/4 IDE controller (8086:7010/7111) via PCI, allocates a PRD table and
  a 4 KB bounce buffer.  Provides `atadma_read_sectors` (up to 8 sectors per
  call).  Falls back to existing PIO path if unavailable.

- **`kernel/virtio.inc`** — VirtIO PCI legacy driver (spec 0.9.5).
  - **virtio-blk** (1AF4:1001/1042): virtqueue-based block device with
    3-descriptor request chains and polled completion.
    Provides `virtio_blk_read`.
  - **virtio-net** (1AF4:1000/1041): negotiation + MAC address read.

### Graphics upgrades (kernel/vbe.inc additions)

- **Bresenham line** — `vbe_draw_line` / **SYS_DRAW_LINE (95)**
- **Scanline triangle fill** — `vbe_fill_triangle` / **SYS_DRAW_TRIANGLE (96)**
- **Sprite blit with color key** — `vbe_sprite_blit` / **SYS_BLIT (97)**
- **Dirty-rectangle tracking** — `vbe_dirty_present` / **SYS_DIRTY_PRESENT (98)**
  Maintains the smallest bounding box of modified pixels and bulk-blits only
  that region from shadow buffer to LFB.
- **PSF2 bitmap font loader** — **SYS_PSF_LOAD (99)** / **SYS_PSF_CHAR (100)**
  Loads any PSF2 font from HBFS (up to 256 KB), renders individual glyphs
  into the framebuffer.

### Kernel wiring

- `pci_init`, `ac97_init`, `atadma_init`, `virtio_init` called from `kernel_entry`
  after `sb16_init`, before `burrows_init`.
- New includes added in correct dependency order.

### New programs (v8.5.0)

- **`more [FILE]`** — paging text viewer (23 lines/page; SPACE=next, ENTER=line, Q=quit).
- **`pciinfo`** — queries `SYS_PCI_FIND` to list all detected PCI devices with
  bus:device.function addresses.
- **`audiodemo`** — probes SB16 and AC'97 via PCI, plays a PC-speaker melody,
  then attempts 16-bit PCM playback via `SYS_AUDIO_PLAY`.
- **`soundviz`** — SB16 chord sequence (C4, E4, G4) with an animated waveform
  visualizer on the 640×480 VBE framebuffer.
- **`blitdemo`** — bouncing 16×16 color-keyed sprite via direct shadow-buffer writes.
- **`gfxdemo`** — scrolling rainbow gradient background + bouncing square sprite
  with title overlay; demonstrates the shadow-buffer flip workflow.
- **`gfxplasma`** — classic sum-of-sines plasma effect rendered as 320×240 → 2×2
  blocks on the 640×480 framebuffer.
- **`breakout`** — Breakout/Arkanoid clone: 5 × 10 bricks, three lives,
  LEFT/RIGHT arrows to move paddle.
- **`cube`** — perspective-projected rotating wireframe cube using 16.16
  fixed-point sine tables and `SYS_DRAW_LINE`.
- **`julia`** — interactive Julia set renderer (arrow keys move *c*, +/- zoom, R reset).
- **`mandelbrot`** — Mandelbrot set at 640×480×32 bpp; progressive rendering,
  rainbow escape-time palette.
- **`sed [-n] [-e SCRIPT] [SCRIPT] [FILE]`** — stream editor: `s///g`, `d`, `p`,
  `=`, `q`, `y///`, line/regex/range addressing.
- **`awk [-F SEP] 'PROG' [FILE]`** — pattern/action processor: `$n`, `NR`, `NF`,
  `print`, `gsub()`, `sub()`, `/regex/`, `BEGIN`/`END` blocks.
- **`top`** — real-time process monitor using `SYS_PROCLIST` (all 128 slots) and
  `SYS_MEMINFO` with a used/free memory bar; refreshes every second.
- **`tar c|x|t ARCHIVE [FILES]`** — HBTAR1.0 flat archive: create, extract, list;
  up to 64 files × 64 KB each.
- **`nm FILE [FILE ...]`** — ELF32 symbol table reader: parses `SHT_SYMTAB` /
  `SHT_STRTAB`, prints address, type letter (T/D/B/R/U), and name.

### Manual pages

- All 16 new programs above added to `man.asm` with full NAME / SYNOPSIS /
  DESCRIPTION / CONTROLS / SEE ALSO sections.

## v8.4.2 - `del` is now persistent across `make full`

### Fix

- `populate.py` previously re-added every program binary on every `make full`,
  silently undoing any `del` the user had run inside the OS.
- Cache schema bumped to **v2** with a new `tombstones` set. Workflow:
  1. On the first `make full` after a `del`, populate notices the cached
     binary is no longer present in the image, records a tombstone, and
     skips re-adding it.
  2. On subsequent `make full` runs, tombstoned binaries stay gone as long
     as the source `.asm` (and therefore the rebuilt `.bin`) hasn't been
     touched.
  3. Touching/modifying the source clears the tombstone — the binary is
     "revived" automatically. This preserves the developer ergonomics of
     "edit `programs/foo.asm`, run `make full`, see new foo".
- Cache also self-prunes tombstones whose `.bin` has been removed from
  `programs/`.
- v1 (flat dict) caches are still loaded for backward compat.

## v8.4.1 - Theme actually themes now

### Fixes

- **`theme`** previously called `SYS_SETCOLOR` once and exited, but every kernel
  print path immediately restored the hard-coded `COLOR_DEFAULT` (0x07), so the
  selected theme survived only until the next character was printed.
- Added a runtime `default_color` byte and `vga_reset_color` helper.
  `SYS_SETCOLOR` now updates *both* the live `vga_color` and the persistent
  `default_color`, and all ~22 `mov byte [vga_color], COLOR_DEFAULT` sites in
  `kernel/` were converted to `call vga_reset_color`. `vga_clear` now uses the
  live default color too, so the screen background recolors immediately.
- Net effect: `theme amber` (or any of the 8 themes) now actually changes the
  shell, prompt, and subsequent output for the rest of the session.

## v8.4.0 - Daily Driver Pass IV

### Six more userspace programs

- **`roll [N | A B]`**  generic random integer (default 1..100, one-arg = 1..N, two-arg = A..B inclusive).
- **`pick item1 item2 ...`**  picks one of the supplied tokens uniformly at random.
- **`reverse <text>`**  reverses the argument string.
- **`upper <text>`** / **`lower <text>`**  case conversion.
- **`countc <text>`**  prints `chars / words / lines` counts for the argument string.

### Tests

- Regression suite grows from 1534 to **1576/1576 passing**.

## v8.3.0 - Daily Driver Pass III

### Six more userspace programs

- **`stopwatch`**  lap stopwatch (Enter = lap, q = quit); HH:MM:SS.cc precision via `SYS_GETTIME`.
- **`countdown <secs|Nm|MM:SS>`**  countdown timer with end-of-time beeps and Burrows notification.
- **`passgen [len] [-a]`**  random password generator (xorshift32 seeded from time XOR PID); `-a` restricts to alphanumerics.
- **`dice [NdM]`**  roll N dice with M sides; prints individual rolls and sum.
- **`coin [N]`**  flip one or more coins; summary heads count when N>1.
- **`tip <bill> [pct] [people]`**  bill / tip / split-by-people calculator with cents-precise math.

### Tests

- Regression suite grows from 1492 to **1534/1534 passing**.

## v8.2.0 - Daily Driver Pass II

### Six more userspace programs

- **`tldr <cmd>`**  one-line summary lookup; `tldr` with no args lists every entry.
- **`todo`**  persistent todo list at `/home/.todo` with `add` and `done <N>`.
- **`pomodoro [min]`**  focus timer with countdown, three completion beeps, and a desktop notification.
- **`morse [-p] <text>`**  ASCII -> Morse (`.-`/`-...`); beeps each symbol on the PC speaker unless `-p` (print only).
- **`wiki list|show|add <topic> ...`**  personal knowledge base under `/home/wiki/<topic>.txt`.
- **`color`**  full 16x16 VGA attribute palette rendered straight to the framebuffer; press any key to exit.

### Tests

- Regression suite grows from 1450 to **1492/1492 passing**.

## v8.1.0 - Daily Driver Pass

### New userspace programs (14, one per roadmap section)

- **Kernel & runtime**: `meminfo` — free/total memory, uptime, PID.
- **Filesystem**: `tag` — file tagging via `/home/.tags.db`; `add`/`list`/`find`.
- **Shell UX**: `histgrep` — case-insensitive search of shell history.
- **Burrows GUI**: `bnotify` — desktop notification with optional `-c <attr>`.
- **Burrows app**: `bcal` — today + scheduled events from `/home/.events`.
- **Demo**: `plasma` — animated text-mode plasma at 0xB8000.
- **Game**: `nim` — single-pile misère Nim with optimal AI.
- **Dev tooling**: `mkprog` — generates a runnable `<name>.asm` skeleton.
- **Networking**: `dnslook` — `SYS_DNS` wrapper printing dotted-quad IPs.
- **Sound**: `play` — token-driven note sequencer for the PC speaker.
- **Productivity**: `journal` — appends `YYYY-MM-DD HH:MM` entries to `/home/journal.txt`.
- **Polish**: `theme` — 8 named color themes, persisted to `/home/.theme`.
- **Documentation**: `tutorial` — 8-page interactive welcome tour.
- **Distribution**: `pkginfo` — OS / version / arch / RAM / uptime banner.

### Manual pages

- **`man` rewrite**: 49 baked-in topics with case-insensitive lookup,
  `man -l` listing and `man -k <kw>` keyword search; falls back to
  `/docs/man/<topic>.txt` if a topic is not baked in.
- New man pages added for every program above plus `intro`, `shell`,
  `files`, `burrows`, `syscalls`, `hbfs`, `keys`, `credits`.

### Tests

- Regression suite grows from 1352 to **1450/1450 passing**.

## v8.0.0 - Hardening Pass

### Kernel safety

- **VBE / framebuffer**: bounds-checked `vbe_putpixel` and full rect
  clipping in `vbe_fill_rect`; mode validation in `vbe_set_mode`
  (320..1920 × 200..1200, bpp ∈ {16,24,32}); LFB mapping page count
  derived from the validated mode and capped; shadow buffer pages now
  freed before reallocation; `.fb_drawtext` capped at 4 KB to prevent
  runaway loops.
- **Syscall pointer validation**: new `validate_user_ptr` helper guards
  user-supplied pointers in `sys_print`, `sys_exec_call`, and
  `sys_proclist`. Range checked against `[PROGRAM_BASE, USER_PTR_MAX)`.
- **Paging / PMM**: `paging_map_page` now drops its prologue pushes on
  the alloc-failure path (was popad'ing with 16 stale bytes on the
  stack); `pmm_alloc_pages` contiguity scan bounded against the bitmap
  size.
- **IPC / scheduler**: pipe `PIPE_COUNT` check + cursor update made
  atomic so concurrent readers/writers can't underflow or over-fill the
  ring; `sys_sigmask` read-modify-write wrapped in CLI/STI.
- **Drivers**: ATA read/write reject LBA outside `[0, ata_total_sectors)`;
  RTL8139 driver refuses BAR0 == 0; HBFS filename compare capped at 252
  to handle non-NUL-terminated entries; shell `Ctrl+W` keeps `line_len`
  in sync with the cursor.

### User-program safety

- `wget` hostname / path / outfile copy loops capped at the buffer size
  (256 / 512 / 128 bytes).
- `programs/lib/vbe.inc` `vbe_fill_rect` now clips against `fb_width` /
  `fb_height`, mirroring the kernel-side fix.

### Build, populate, tooling

- Portable `NPROC` detection (`nproc` → `sysctl` → `getconf` → 4).
- `kernel_sectors.inc` generation hard-fails if the kernel exceeds
  2048 sectors (1 MB), preventing silent overflow of the stage-2 load
  buffer.
- `populate.py` now prints a warning when a filename is truncated to 252
  bytes instead of silently shortening.
- New `make sanitize` target builds with `KERNEL_DEBUG_BOUNDS=1` for
  CI-style hostile-bounds nightlies.
- `Experimental/tools/packet_fuzz.py` — dependency-free fuzz frame
  generator (Eth runts, ARP/IPv4/TCP/UDP malformations, random garbage)
  for offline replay against `kernel/net.inc` parsers.

### Documentation

- `docs/TECHNICAL_REFERENCE.md` GDT table corrected: `0xCF` flags row
  now reads "32-bit, 4K gran".

---

## v7.5.0 - BASIC Compiler & Interpreter Upgrade

### Added

- **`basicc`** — new BASIC-to-x86 compiler. Compiles a BASIC source
  file (`.bas`) to a flat x86-32 native binary that runs directly on
  Mellivora OS. Usage: `basicc source.bas output.bin`. Supports:
  - Numeric variables A–Z (32-bit integer)
  - Full arithmetic: `+`, `-`, `*`, `/`, `MOD`
  - Bitwise / logical: `AND`, `OR`, `XOR`, `NOT`
  - Relational comparisons: `=`, `<>`, `<`, `>`, `<=`, `>=`
  - `PRINT` with string literals and numeric expressions, `;` and `,` separators
  - `INPUT` with optional prompt string
  - `LET var = expr` and bare `var = expr` assignment
  - `IF ... THEN ... [ELSE ...]` with inline statements or line numbers
  - `GOTO` / `GOSUB` / `RETURN` (with forward-reference fixup table)
  - `FOR var = start TO end [STEP step]` / `NEXT` (up to 16 nested loops)
  - `WHILE ... WEND` (up to 64 nested)
  - `END`, `STOP`, `CLS`, `BEEP`, `SLEEP expr`
  - Multi-statement lines via `:`
  - `REM` comments
  - Output binary is a self-contained flat x86 executable starting at
    `0x200000`; no runtime library required.

- **`samples/fib.bas`** — Fibonacci sequence sample program for `basicc`.
- **`samples/hello.bas`** — minimal Hello World sample for `basicc`.

### Changed

- **`basic`** (v3.1) — upgraded the GW-BASIC interpreter:
  - Added `TRON` / `TROFF` — execution trace mode prints `[linenum]`
    before each statement.
  - Added `STOP` / `CONT` — stop mid-program and continue.
  - Added `HEX$(n)` and `OCT$(n)` string functions.
  - Updated `HELP` listing and version banner to 3.1.

## v7.4.0 - BASIC Expansion

### Changed

- **`basic`** — expanded from a tiny integer-only interpreter into a
  much larger GW-BASIC-style environment. Added string variables and
  string functions, `LINE INPUT`, `IF ... THEN ... ELSE`, `WHILE/WEND`,
  `ON ... GOTO/GOSUB`, `DATA/READ/RESTORE`, `LOCATE`, `SWAP`, `TAB()` /
  `SPC()`, broader math functions, multi-statement lines with `:`, a
  `HELP` command, larger program storage, and improved runtime errors.

## v7.3.0 - Bug Fixes & Housekeeping

### Fixed

- **`pacman`**, **`frogger`**, **`sudoku`** — black-screen bug: all three were
  drawing into the VBE shadow buffer but never calling `VBE_GAME_PRESENT` to
  blit it to the live framebuffer. Screen now renders correctly on launch.

### Removed

- **`reversi`** — removed from the distribution. The VBE Reversi/Othello
  experience is covered by **`iago`**, which has a full board renderer,
  greedy-AI opponent, and persistent win counter.

## v7.2.0 - Arcade Authenticity Pass

### Added

- **`frogger`** — Classic road-and-river crossing. 11 lanes (5 river +
  median + 5 road), drifting logs/turtles you must ride, oncoming
  cars/trucks at varying speeds, 5 home slots to fill. 3 lives,
  persistent high score saved to `/scores/frogger`.

### Changed

- **`galaga`** — Major arcade-faithful overhaul:
  - Enemies now **dive-bomb** out of the formation in attack runs
    instead of marching wall-to-wall.
  - Diving enemies **fire bullets** that can hit the player.
  - Formation gently **sways** side-to-side as a unit.
  - Player respawns with **brief invulnerability flicker** when hit;
    the `lives` counter is finally meaningful (game-over only at 0).
  - Arcade **2-shot limit** on player bullets.
  - Arcade-style scoring: bug 50, moth 80, boss 150 — **double** when
    killed mid-dive.
  - Stars now **scroll downward** for parallax.
  - "STAGE N" intro banner between levels.

## v7.1.0 - New Arcade Games

### Added

- **`pacman`** — Pac-Man-style 21x21 maze chase. Eat dots (10 pts) and
  power pellets (50 pts) while avoiding 4 ghosts. Power pellets
  frighten ghosts for ~6 sec, allowing you to eat them (200 pts) and
  send them back to a corner. Persistent high score saved to
  `/scores/pacman`; win/lose SFX cues at end of round.

## v7.0.1 - Sudoku

### Added

- **`sudoku`** — brand-new 9x9 Sudoku puzzle game.
  - 4-puzzle bank cycled per session via `SYS_GETTIME`.
  - Cursor navigation (arrows), digit entry (1–9), clear (0/Space).
  - Real-time row/column/3x3-box conflict highlighting in red.
  - `H` hint key fills one missing cell from the solution.
  - Persistent solve count saved to `/scores/sudoku`; win SFX on solve.

## v7.0.0 - The Hercules Release (Phases 7-8 of Hercules overhaul)

### Release summary

Closes the eight-phase **Hercules** overhaul that began at v6.1. Across
all phases the project gained shared VBE UI libraries, a universal
quit-key sweep, the `audio.inc` + `highscore.inc` libraries (v6.5), and
persistent high-score wiring for **28 games**. v7.0.0 promotes the
result to a stable release.

### Bumped

- Boot banner, `version_text`, and shell help in `kernel/data.inc`
  updated from v6.5 → v7.0 ("The Hercules Release").
- `bsysmon` "OS" field, `neofetch` OS + shell strings, README directory
  tree, and `docs/INSTALL.md` all bumped to v7.0.
- "(v6.5+)" annotations in API docs are kept as historical "added-in"
  markers for the audio + highscore libraries.

### Verified

- Full clean rebuild (`make full`) populates 203 files cleanly with no
  NASM warnings.
- All 28 wired games still build and link without changes.
- No lingering v3.x / v4.x / v5.x version strings in the live banners.

## v6.5.0 - Audio + High-Score Libraries (Phase 6 of Hercules overhaul)

### New shared libraries

- **`programs/lib/audio.inc`** — note table (`NOTE_C2`..`NOTE_C7`),
  `audio_note`, `audio_rest`, `audio_play_score` (byte-packed melody),
  `audio_play_score_w` (word-packed for full Hz range), and stock SFX
  cues `audio_sfx_click`/`_ok`/`_error`/`_win`/`_lose`. All entry points
  preserve registers; built on `SYS_BEEP` (24).
- **`programs/lib/highscore.inc`** — `hs_load`, `hs_save`, `hs_update`.
  Each game's high score lives in `/scores/<name>` as a single
  little-endian dword. `hs_update` writes only when the candidate beats
  the stored value. The `/scores` directory is auto-created on first
  write.

### Wired into existing games

- **`tetris`** — loads/saves high score, displays "High:" under "Score:"
  in the HUD, plays `audio_sfx_lose` once on game-over.
- **`2048`** — splits the score panel into SCORE + HIGH boxes, persists
  high score, plays loss SFX on game-over.
- **`snake`** — adds `HIGH:` to the score bar, persists high score, plays
  loss SFX in `show_game_over`.
- **`breakout`** — adds `HIGH:` to the HUD band, persists high score,
  plays loss SFX once when lives reach 0.
- **`simon`** — shows `HIGH` line under the SCORE on the game-over
  banner, persists best round reached, plays loss SFX once.
- **`galaga`** — adds `High Score:` line to the game-over panel,
  persists high score, plays loss SFX.
- **`mastermind`** — best-game persisted as inverse of guesses-needed
  (so `hs_update`'s max-wins semantic still picks the better record);
  win/lose SFX cues. Best record loaded once at start.
- **`hangman`** — running `WINS:` counter persisted across runs; win/lose
  SFX cues fired once per round.
- **`tictactoe`** — persistent `WINS:` counter (player vs CPU) shown
  under the header; win/lose/draw SFX cues fired once per game.
- **`connect4`** — wins counter now persists across reboots (loaded into
  the existing on-screen WINS panel); win/lose/draw SFX cues.
- **`reversi`** — wins (BLACK > WHITE) persist across runs; win/lose/tie
  SFX cues fired once at end-of-game.
- **`wordle`** — total solved-words counter persists across runs;
  solve / fail SFX cues.
- **`mine`** (Minesweeper) — total cleared-board wins persist across
  runs; safe-clear / mine-trigger SFX cues.
- **`puzzle15`** — total solved-puzzles counter persists across runs;
  win SFX fires once on each solve.
- **`lights`** (Lights Out) — total solved-boards counter persists
  across runs; win SFX cues on each solve.
- **`sokoban`** — total cleared-levels counter persists across runs;
  win SFX cues each time a level is cleared.
- **`guess`** (number guessing) — total correct-guess counter persists
  across runs; win SFX cues on correct answer.
- **`battleship`** — total wins persist across runs; win SFX on
  victory, lose SFX on defeat.
- **`blackjack`** — total dealer-beating rounds persist across runs;
  win/lose SFX cues on each settled hand.
- **`nim`** (misere) — total wins persist across runs (saved to
  `/scores/nim` after each AI takes the last); win/lose SFX cues.
- **`checkers`** — total wins (player=red) persist across runs; win/lose
  SFX cues fired once when the board is decided.
- **`iago`** (Reversi variant, player=BLACK) — total wins persist
  across runs; win/lose/tie SFX cues fired once at end-of-game.
- **`solitaire`** — total completed games persist across runs; win SFX
  fires once when all 4 foundations are filled.
- **`pipes`** — high score persists across runs (best score is kept);
  win/lose SFX cues on flow reaching drain or going dry.
- **`lunar`** — total safe landings persist across runs (saved to
  `/scores/lunar` after every soft touchdown); win SFX on safe landing,
  lose SFX on crash.
- **`kingdom`** — best end-of-reign score persists across runs; win
  fanfare SFX on completing 10 years, lose SFX on collapse.
- **`rogue`** — best XP persists across runs (written when the player
  dies); lose SFX on death.
- **`outbreak`** — best total-vaccinated count persists across runs;
  win SFX on outbreak defeated, lose SFX on collapse.

### `ps` modernized

- Now scans all 128 scheduler slots (was 16) and uses the v4.0 expanded
  48-byte `SYS_PROCLIST` ABI: shows new `PRI` (priority) column and a
  16-char `NAME` column.
- All five live task states have distinct color and label:
  READY (green), RUNNING (white), BLOCKED (blue), STOPPED (magenta),
  ZOMBIE (red).
- Footer now prints a state breakdown:
  `Active: N  Running: r  Ready: r  Blocked: b  Stopped: s  Zombie: z`.

### `bsysmon` enhancements

- **Live memory** — `Memory:` now reports `USED / TOTAL MB (PCT%)` from
  `SYS_MEMINFO` (67) plus a 200-pixel horizontal usage bar that turns
  yellow at ≥70 % and red at ≥90 %.
- **Uptime** — new `Uptime: Hh Mm Ss` line derived from `SYS_GETTIME`
  ticks (PIT @ 100 Hz).
- **Process count** — new `Procs: N active` line that walks the 128-slot
  task table via `SYS_PROCLIST` (66).
- **Auto-refresh** — main loop now sleeps 50 ticks (~500 ms) between
  redraws when no input is pending, so all live values update without
  user interaction.
- Window resized to 360×340 to fit the new rows; `OS:` line bumped to
  v6.5.

### Documentation

- `docs/API_REFERENCE.md` — added Quick Start entries and reference
  tables for both new libraries.

## v6.4.0 - Universal Quit-Key Sweep (Phase 5 of Hercules overhaul)

### Universal Q + ESC quit

Per `docs/STYLE_GUIDE.md`, every interactive VBE game must accept `ESC`,
lowercase `q`, AND uppercase `Q` to quit. This release brings 24 games
into compliance:

- **Added uppercase `Q`** (already had ESC + `q`):
  `2048`, `battleship`, `blackjack`, `checkers`, `chess`, `connect4`,
  `guess`, `hangman`, `iago`, `lights`, `lunar`, `mastermind`, `mine`,
  `nim`, `pipes`, `puzzle15`, `reversi`, `simon`, `sokoban`, `solitaire`
- **Added `q` and `Q`** (had only ESC):
  `breakout`, `maze`
- **Added ESC** (had `q`/`Q` but no ESC):
  `pong`
- **Added `Q` and converted raw `27` → `KEY_ESC` style consistency**:
  `galaga`, `rogue`, `snake`
- **Added `q`/`Q` to main play loop** (had ESC only):
  `outbreak` (title + action loop), `kingdom` (title screen)

Demo/screensaver programs (`doomfire`, `matrix`, `rain`, `starfield`,
`spritetest`) intentionally retain "press any key to exit" behavior.

### Phase 4 — VBE conversion of `adventure.asm` and `neurovault.asm`

**Decision: not converting.** Both are interactive-fiction text adventures
where text-mode terminal flow IS the appropriate medium. Forcing them
into a framebuffer would only simulate a terminal inside a pixel surface
— strictly worse UX for no gain. Their existing VGA text presentation
remains the right call. Future polish will be limited to text-mode
banner/prompt consistency.

## v6.3.0 - Style Migration & Doc Refresh (Phases 2-3 of Hercules overhaul)

### Style migration

- **`tictactoe.asm`** — pilot migration to the shared style infrastructure:
  - Color literals replaced with `MV_*` aliases from `lib/palette.inc`.
  - Title and status text now drawn by `vbe_ui_header_bar` and
    `vbe_ui_status_bar` from `lib/vbe_ui.inc`.
  - `R`/`r` now restarts the game at any time (not just after game-over),
    matching the style-guide convention.

### Documentation refresh

- **`docs/PROGRAMMING_GUIDE.md`**:
  - Removed 7 phantom syscall definitions (`SYS_SEM_CREATE`, `SYS_SEM_WAIT`,
    `SYS_SEM_POST`, `SYS_SEM_CLOSE`, `SYS_WAITPID`, `SYS_GETMTIME`,
    `SYS_SETMTIME`) that were never implemented.
  - Fixed VBE example resolution from 640×480 to 1024×768 (matches the
    actual `VBE_GAME_INIT` macro).
  - Added a new **Shared VBE UI Library (v6.1+)** section documenting
    `lib/palette.inc` and `lib/vbe_ui.inc`, with cross-reference to
    `docs/STYLE_GUIDE.md`.
  - Updated table of contents.
- **`docs/API_REFERENCE.md`**:
  - Fixed VBE example resolution from 640×480 to 1024×768.
  - Added `lib/vbe.inc`, `lib/font.inc`, `lib/vbe_game.inc`,
    `lib/palette.inc`, and `lib/vbe_ui.inc` to the Quick Start include
    list, with a note pointing to `STYLE_GUIDE.md`.
- **`docs/INSTALL.md`**:
  - Project structure now reflects current state (~290 programs, current
    version v6.2.0).

### Audited-clean (no doc changes needed)

`docs/INSTALL.md`, `docs/NETWORKING_GUIDE.md`, `docs/TECHNICAL_REFERENCE.md`,
`docs/TUTORIAL.md`, `docs/USER_GUIDE.md` were all reviewed and found to
match the current code state.

## v6.2.0 - Bug Sweep (Phase 1 of Hercules overhaul)

### Initialization-safety fixes (`section .bss` → `dd 0`)

NASM `-f bin` mode places `.bss` labels past the end of the program binary,
where the kernel loader does NOT zero memory. Variables declared via
`resd`/`resb` therefore start with whatever junk was left by the previous
program. Three programs had counters/buffers in `.bss` that could be
read-before-written; converted them to inline `dd 0` / `times N db 0` so
they're guaranteed zero at startup:

- **`tetris.asm`** — `fb_addr`, `fb_pitch`, `num_buf`
- **`typist.asm`** — 14 `dd` counters + `input_buf` (MAX_INPUT bytes)
- **`bnotes.asm`** — `win_id`, `cursor_pos`, `draw_col`, `note_text`

(See `docs/STYLE_GUIDE.md` §1.2 for the full rule.)

### Audit pass — clean

Audited 20+ programs (VBE games, Burrows GUI apps, CLI utilities) for
common bug patterns; the following were found to be solid: simon, bedit,
bcalc, bsysmon, bpaint, bsheet, bplayer, bsettings, bview, bterm, bhive,
grep, sed, sort, cp, find, wget, dig, asm, bc, diff, tictactoe.

The flip-logic in `iago.asm` was re-audited: logic is correct (the
previously-fixed `reversi.asm` was the source of the visible flip bug).

### Quality-of-life: universal Q/ESC quit

- **`tetris.asm`** — game-over screen now also accepts `Q`/`q`
- **`wordle.asm`** — main loop now also accepts `Q`/`q`

## v6.1.0 - Foundation (Phase 0 of Hercules overhaul)

### New shared infrastructure

- **`programs/lib/palette.inc`**: Project-wide color palette. Single source of
  truth for `MV_BG_*`, `MV_FG_*`, `MV_ACCENT_*`, `MV_STATUS_*`, `MV_CURSOR`,
  `MV_BOARD_*` and other UI tones. Programs should alias these (e.g.
  `COL_BG equ MV_BG_DARK`) instead of hard-coding hex literals.
- **`programs/lib/vbe_ui.inc`**: Shared VBE UI widgets used by all games:
  - `vbe_ui_header_bar` — top title band
  - `vbe_ui_status_bar` — bottom hint band
  - `vbe_ui_modal` — centered dialog (game-over, help, info)
  - `vbe_ui_input_line` — decimal-number input widget
- **`docs/STYLE_GUIDE.md`**: New authoritative cross-program style guide
  documenting program skeleton, calling conventions, syscall rules, VBE
  layout zones, key-binding standards, the flat-binary BSS rule, the CLI
  conventions, the Burrows GUI conventions, the uppercase-string rule, and
  the per-commit code-quality checklist.

### Cleanup

- Removed stale `.bak` files: `edit.asm.bak`, `blackjack.asm.bak`,
  `tcc.asm.bak`, `outbreak.asm.bak` (~210 KB total).

### Game logic fixes

- **`reversi.asm`**: Fixed two flip-counting bugs that produced incorrect
  flips and false "invalid move" rejections.
  - `count_dir`: dotted-local labels `.dr`/`.dc` resolved to the wrong
    function's locals (always 0). Qualified them as
    `[count_flips.dr]` / `[count_flips.dc]`.
  - `flip_dir`: ECX (the flip counter) was being clobbered by a load of
    `do_move.player` mid-loop. Wrapped the board write with `push ecx` /
    `pop ecx`.

### kingdom.asm — VBE conversion

- Completed full VGA→VBE conversion (~2400 lines). Replaced text-mode phase
  screens, status display, mini-bars, and number input with native VBE
  widgets. Migrated from `section .bss` to flat-binary safe storage.
  All game strings uppercased to match the 5×7 bitmap font glyph set.

## v6.0.0 - The Pixel Release

### Kernel — VBE Double Buffering (`kernel/vbe.inc`)

- **Shadow buffer double buffering**: `SYS_FRAMEBUF/1` (set mode) now allocates a PMM-backed
  shadow buffer the same size as the framebuffer. Programs render into the shadow buffer
  (returned by `SYS_FRAMEBUF/0`) rather than the real LFB, eliminating tearing.
- **`SYS_FRAMEBUF/4` — present frame**: New sub-function blits the full shadow buffer to the
  LFB with a single `rep movsd`. Call once per frame after all rendering is complete.
- **Size-aware reallocation**: `vbe_shadow_pages` tracks the allocated page count. When a new
  mode is set that requires more pages than the current allocation, the shadow buffer is
  reallocated. Prevents crashes where a 640×480 game's undersized buffer was reused for
  a 1024×768 program.
- **Vsync hang fix**: Removed the `port 0x3DA` vertical-blank poll loop from the present path.
  The VGA input status register bit 3 does not toggle in QEMU BGA mode, causing an infinite
  kernel-mode hang. The blit now runs unconditionally.

### Programs — Sprite Library (`programs/sprite.inc`)

- **New file `programs/sprite.inc`**: Reusable sprite drawing library for VBE programs.
  - `sprite_draw` (EBX=x, ECX=y, ESI=sprite_ptr): draw with per-pixel alpha (alpha=0 → skip).
  - `sprite_draw_opaque` (EBX=x, ECX=y, ESI=sprite_ptr): draw ignoring alpha — fastest path.
  - `sprite_draw_key` (EBX=x, ECX=y, ESI=sprite_ptr, EDI=key): color-key transparency.
  - `sprite_draw_scaled` (EBX=x, ECX=y, ESI=sprite_ptr, EDX=shift): nearest-neighbour scale
    by 2^shift (e.g. EDX=1 → 2×, EDX=2 → 4×).
  - `SPRITE_BEGIN name, width, height` / `SPRITE_END` macros for inline sprite data.
  - Sprite pixel format: `dd width, height` then `width*height` pixels as `0xAARRGGBB`.

### Programs — Galaga Sprites (`programs/galaga_sprites.inc`)

- **New file `programs/galaga_sprites.inc`**: Pixel-art sprite data for galaga.
  Five sprites defined with the `sprite.inc` format:
  `spr_player` (24×16), `spr_bug` (20×12), `spr_moth` (20×12), `spr_boss` (20×12),
  `spr_bullet` (3×12). Transparent pixels use alpha=0x00.

### Programs — Sprite Test (`programs/spritetest.asm`)

- **New file `programs/spritetest.asm`**: Interactive test program demonstrating all four
  `sprite.inc` routines side by side in a 640×480×32 VBE window.

### Programs — VBE Game Fixes

- **`programs/galaga.asm`**: Replaced direct pixel-fill rendering with `sprite.inc` calls for
  the player ship, enemy bugs/moths/boss, and bullets. Added `SYS_FRAMEBUF/4` present call
  in the main game loop for double-buffered output.
- **`programs/doomfire.asm`, `programs/life.asm`, `programs/pong.asm`,
  `programs/snake.asm`, `programs/tetris.asm`**: Added `SYS_FRAMEBUF/4` present call at
  the end of each frame to make rendered output visible when double buffering is active.

### Build & Tests

- `kernel_sectors.inc`: Corrected `KERNEL_SECTORS` value to match actual kernel binary size.

---

## v5.0.0 - The Hornet Release

### Kernel — Semaphores (`kernel/ipc.inc`)

- **`SYS_SEM_CREATE` (#88)**: Create a counting semaphore with an initial value. Returns a semaphore ID (0–7) or -1 on failure.
- **`SYS_SEM_WAIT` (#89)**: Decrement (P/wait) a semaphore. If the value is 0, yields up to 500 times before returning -1 (non-blocking style consistent with pipe I/O).
- **`SYS_SEM_POST` (#90)**: Increment (V/post) a semaphore.
- **`SYS_SEM_CLOSE` (#91)**: Release a semaphore slot.
- Up to 8 semaphores simultaneously. Semaphore state initialised in `ipc_init`.

### Kernel — Synchronous Wait (`kernel/sched.inc`)

- **`TASK_ZOMBIE` state (5)**: Tasks that call `sys_exit` now enter a zombie state instead of being freed immediately, preserving their slot until reaped by a parent.
- **`SYS_WAITPID` (#92)**: Wait for a task by PID. Yields up to 2000 times polling for task completion, then reaps the zombie and returns its exit code. Returns -1 if the PID is not found.

### Kernel — File Timestamps (`kernel/hbfs.inc`)

- **`SYS_GETMTIME` (#93)**: Query a file's timestamps. Returns packed RTC `DIRENT_MODIFIED` timestamp in EAX and `DIRENT_CREATED` timestamp in ECX.
- **`SYS_SETMTIME` (#94)**: Update a file's `DIRENT_MODIFIED` timestamp. Pass ECX=0 to use the current RTC time.

### Shell — Ctrl+R Reverse History Search (`kernel/shell.inc`)

- **Incremental reverse-i-search**: Press Ctrl+R at the shell prompt to start an interactive history search.
- Shows `(reverse-i-search)\`QUERY': MATCH` while typing.
- **Backspace** removes the last search character and re-searches.
- **Enter** copies the matched command to the input line and executes it.
- **Escape / Ctrl+C** cancels and returns to an empty prompt.

### Batch Scripts — Structured Control Flow (`kernel/util.inc`)

- **`if exist FILENAME cmd`**: Test for file existence. Works with the `not` modifier (`if not exist ...`).
- **`if "STR1"=="STR2" cmd`**: String comparison. Supports `not` modifier.
- **Block `if` / `else` / `endif`**: Multi-line conditional blocks. When an `if` line has no inline command the following lines form the block body; an optional `else` block is executed when the condition is false; `endif` closes the block. Nested `if`/`endif` pairs are handled correctly.
- **`for %%x in (a b c) do cmd`**: Single-line for loop. Iterates over a space-separated list; `%%x` in the command template is substituted with each value in turn.
- `else` and `endif` are now recognised as batch directives in `batch_run_loop`.

### New Programs

- **`strace`**: Syscall trace wrapper. Usage: `strace PROGRAM [args]`. Records the dmesg ring-buffer depth before running the target program and dumps all new log entries added during the run, providing a lightweight activity trace.
- **`patch`**: Apply unified-style diff output to a file. Usage: `patch FILE PATCHFILE`. The patch file is the output of the `diff` utility (`<` = remove, `>` = insert). Applies all hunks and reports how many could not be matched.

### Syscall Constants (`programs/syscalls.inc`)

- Added constants for all v5.0 syscalls: `SYS_SEM_CREATE` through `SYS_SETMTIME` (88–94).

---

## v4.0.0 - The Titan Release

### Kernel — Priority Scheduler (`kernel/sched.inc`)

- **Priority-based scheduling**: Replaced round-robin with 4-level priority scheduling (HIGH, NORMAL, LOW, IDLE). Higher priority tasks are selected first; equal priority tasks use round-robin for fairness.
- **64 tasks max**: Doubled from 32 to 64 concurrent tasks (`MAX_TASKS`).
- **Expanded TCB**: Task Control Block expanded from 32 to 64 bytes with new fields:
  - `TCB_NAME` (16 bytes): Human-readable task name
  - `TCB_SIG_PEND` / `TCB_SIG_MASK`: Signal pending/mask bitmasks
  - `TCB_PGID`: Process group ID
  - `TCB_EXIT_CODE`: Task exit code
- **TASK_STOPPED state**: New state (4) for signal-stopped tasks (SIGTSTP/Ctrl+Z).
- **`sys_proclist` expanded**: Output buffer expanded from 16 to 48 bytes, now includes priority, PGID, pending signals, exit code, and task name.

### Kernel — Signals (`kernel/sched.inc`)

- **POSIX-style signals**: 9 signal types implemented: SIGINT (2), SIGKILL (9), SIGUSR1 (10), SIGUSR2 (12), SIGALRM (14), SIGTERM (15), SIGCHLD (17), SIGTSTP (20), SIGCONT (25).
- **`SYS_SIGNAL` (#74)**: Send any signal to a task by PID. SIGKILL forcibly terminates, SIGTSTP stops, SIGCONT resumes, SIGINT/SIGTERM terminate.
- **`SYS_SIGMASK` (#77)**: Get/set/block/unblock signal mask. 4 operations: get, set, block (OR), unblock (AND NOT).
- **Signal masking**: Tasks can mask signals via bitmask; masked signals are queued as pending.

### Kernel — New Syscalls (`kernel/syscall.inc`)

- 8 new syscalls (72–79), bringing total to 80:
  - `SYS_SETPRIORITY` (#72): Set task priority by PID
  - `SYS_GETPRIORITY` (#73): Get task priority by PID
  - `SYS_SIGNAL` (#74): Send signal to task
  - `SYS_SETPGID` (#75): Set process group ID
  - `SYS_GETPGID` (#76): Get process group ID
  - `SYS_SIGMASK` (#77): Signal mask operations
  - `SYS_TASKNAME` (#78): Set task name string
  - `SYS_REALLOC` (#79): Reallocate memory block

### Kernel — Memory (`kernel/syscall.inc`)

- **`SYS_REALLOC` (#79)**: New syscall for reallocating memory — allocates new block, copies data, returns new pointer. Supports NULL pointer (fresh allocation).

### Shell — Enhanced Line Editing (`kernel/shell.inc`)

- **Ctrl+A**: Move cursor to beginning of line
- **Ctrl+E**: Move cursor to end of line
- **Ctrl+U**: Kill entire input line
- **Ctrl+W**: Delete previous word (skip spaces, then delete word)
- **Ctrl+L**: Clear screen and redraw prompt with current input
- **128 history entries**: Doubled from 64

### Shell — New Commands (`kernel/shell.inc`)

- **`ps`**: List all running tasks with PID, state, priority, and name
- **`jobs`**: List background jobs (alias for ps)
- **`kill <pid> [signal]`**: Send signal to task (default: SIGTERM)
- **`bg <pid>`**: Resume stopped task in background (sends SIGCONT)
- **`fg <pid>`**: Resume stopped task in foreground (sends SIGCONT)
- **`nice <priority> <command>`**: Run command at specified priority level (0=HIGH, 1=NORMAL, 2=LOW, 3=IDLE)
- **`export NAME=VALUE`**: Export environment variable (synonym for set)
- **`source <file>`** / **`. <file>`**: Execute batch script in current shell context

### Shell — Capacity Increases

- **32 environment variables**: Doubled from 16
- **128 history entries**: Doubled from 64

### Programs — Userland Updates (`programs/syscalls.inc`)

- All 8 new v4.0 syscall numbers added to shared header
- Priority level constants (PRIO_HIGH through PRIO_IDLE)
- Signal number constants (SIGINT through SIGCONT)

### Version & Branding

- Version bumped to v4.0.0 "The Titan Release"
- Shell version bumped to HB Lair v3.0
- Updated `ver` output: 80 syscalls, priority scheduler, signal info, enhanced shell shortcuts
- Updated `help` text with all new commands and shortcuts

---

## v3.0.1 - Bug Fixes: Sleep Regression, Neofetch, Pipe Race, Permissions UI

### Kernel — Scheduler (`kernel/sched.inc`)

- **`sys_sleep` HLT fallback**: In v3.0.0 the `SYS_SLEEP` rewrite made sleep a no-op when `task_count == 0` (normal shell / single-program context). This broke 29 programs that use `SYS_SLEEP` for timing, animation frame-rates, and CPU-yield poll loops (clock, galaga, doomfire, matrix, rain, ntpd, serial, etc.). Fixed: when no scheduler tasks are running, `sys_sleep` falls back to the original HLT-based busy-wait loop so timing is preserved.

### Kernel — IPC (`kernel/ipc.inc`)

- **`pipe_retry_count` race condition**: The retry counter in `sys_pipe_read` was a global static variable (`dd 0`). Under preemptive multitasking two tasks blocking on separate pipes could clobber each other's count, causing infinite waits. Fixed by moving the counter to a register (EAX) local to each call — the global is removed.

### Kernel — Filesystem (`kernel/hbfs.inc`)

- **`sys_symlink` dead code**: Two dead `mov edi, ...` instructions preceded the correct `mov edi, [esp+24]` line, producing confusing assembly. Removed the two stale lines; behavior is unchanged.

### Shell — New Commands (`kernel/shell.inc`, `kernel/data.inc`)

- **`chmod <octal> <filename>`**: New shell command wrapping `SYS_CHMOD` (#68). Parses an octal permission value (e.g. `chmod 755 myprog`) and updates `DIRENT_PERMS`. Returns success/failure message.
- **`chown <uid> <filename>`**: New shell command wrapping `SYS_CHOWN` (#69). Parses a decimal UID and updates `DIRENT_OWNER`.
- **`stat` permission display**: The `stat` command now shows two additional lines: `Perms: rwxrwxrwx (octal)` and `Owner: <uid>`, reading from `DIRENT_PERMS` and `DIRENT_OWNER` respectively. Previously these fields were stored on disk but never displayed.

### Programs — Display Fix (`programs/neofetch.asm`)

- **Logo null terminators**: Each `logo_art` line was 32 bytes with no null terminator. `SYS_PRINT` (`vga_print`) scans until null, so every logo line printed the entire remaining logo blob as one continuous string. Fixed by adding a null byte to each line (33 bytes/line) and updating the stride calculation from `shl eax, 5` to `imul eax, 33`. Logo now renders correctly.

---

## v3.0.0 - Kernel Overhaul, Filesystem Permissions, TCP Backlog & 4 New Syscalls

### Kernel — Syscall Dispatch (`kernel/syscall.inc`)

- **O(1) jump table**: Replaced 68-entry linear `cmp`/`je` chain with a 128-entry indexed jump table (`jmp [syscall_table + eax * 4]`). Average dispatch time reduced from ~34 comparisons to 1 lookup.
- **Extensible**: Reserved slots 72–127 for future syscalls; unused entries point to a safe error handler.

### Kernel — Scheduler Enhancements (`kernel/sched.inc`)

- **TASK_BLOCKED state** (state 3): New task state for sleeping/waiting tasks, automatically skipped by round-robin scanner.
- **sys_sleep** (SYS_SLEEP #16): Rewritten from busy-wait `HLT` loop to proper blocking — sets TASK_BLOCKED, stores wakeup tick in `TCB_WAKEUP`, yields CPU to other tasks.
- **sched_wake_sleepers**: Called every PIT tick from `irq_timer`; scans all blocked tasks and wakes those whose wakeup tick has elapsed.
- **TCB_PRIORITY** (offset 24) and **TCB_WAKEUP** (offset 28): New fields replacing reserved padding.
- **MAX_TASKS** doubled from 16 to 32.

### Kernel — IPC Pipe Fix (`kernel/ipc.inc`)

- **Non-spinning pipe reads**: `sys_pipe_read` now yields CPU up to 200 times when the pipe buffer is empty, instead of immediately returning 0 bytes. Eliminates 100% CPU polling loops in callers.
- **pipe_wake_waiter** stub: Preparation for future proper wait-queue integration on pipe write.

### Kernel — ISR / IRQ Hardening (`kernel/isr.inc`)

- **Timer context switch CLI**: Added defensive `CLI` before the critical ESP-swap section in `irq_timer`. Prevents potential nested-interrupt corruption if gate type ever changes.
- **Wake sleepers on tick**: `irq_timer` now calls `sched_wake_sleepers` every tick to unblock sleeping tasks.

### Kernel — Page Fault Handler (`kernel/paging.inc`)

- **Human-readable error codes**: Page fault handler now parses error code bits and prints cause: `[not present]`/`[protection]`, `[read]`/`[write]`, `[supervisor]`/`[user]`.

### Filesystem — Permissions & Symlinks (`kernel/hbfs.inc`)

- **File permissions** (DIRENT_PERMS, offset 276): 9-bit Unix-style `rwxrwxrwx` stored in previously-reserved directory entry bytes. New files default to `0777`.
- **File ownership** (DIRENT_OWNER, offset 278): 16-bit owner UID per file.
- **SYS_CHMOD** (#68): Change file permission bits.
- **SYS_CHOWN** (#69): Change file owner UID.
- **SYS_SYMLINK** (#70): Create symbolic link (file type `FTYPE_LINK=5`, target path stored as file data).
- **SYS_READLINK** (#71): Read symbolic link target path.

### Networking — TCP Accept Backlog (`kernel/net.inc`)

- **Child socket allocation**: When a SYN arrives on a LISTEN socket, a NEW child socket is allocated. The parent stays in `TCP_LISTEN` for more connections — enables multiple simultaneous accepts.
- **TCP SYN_RCVD state handler**: Added missing state machine handler for `TCP_SYN_RCVD`. When the final ACK of the 3-way handshake arrives, properly transitions child socket to `TCP_ESTABLISHED`. This fixes a bug where server-side TCP connections could never complete.
- **Child socket resolution**: `tcp_handle` now scans for child sockets matching remote IP/port before dispatching, ensuring handshake packets reach the correct socket.
- **SOCK_PARENT field** (offset 68): Tracks which listening socket spawned each child.

### Networking — UDP Checksum (`kernel/net.inc`)

- **RFC 768 checksum**: `udp_send` now computes a proper UDP checksum over the pseudo-header (source IP, dest IP, protocol, length) plus UDP header and payload, replacing the previous hardcoded zero.

### Programs & User-Space

- **syscalls.inc**: Added `SYS_CHMOD` (#68), `SYS_CHOWN` (#69), `SYS_SYMLINK` (#70), `SYS_READLINK` (#71) for user programs.
- Version strings updated across: `neofetch`, `uname`, `bterm`, screensaver, MOTD, Burrows About dialog.

### Build & Tests

- All 45 tests passing (syscall consistency, HBFS layout, listing validation).
- 169 files populated in HBFS image.

## v2.2.0 - IPC, Audio, Screensavers, 52 New Programs & Bug Fixes

### Kernel — Inter-Process Communication (`kernel/ipc.inc`)

- **Pipes**: 8 concurrent pipes, 4 KB circular buffer each, with `SYS_PIPE_CREATE`, `SYS_PIPE_WRITE`, `SYS_PIPE_READ`, `SYS_PIPE_CLOSE` (syscalls 60–63).
- **Shared memory**: 4 regions, 4 KB each, keyed access via `SYS_SHMGET` and `SYS_SHMADDR` (syscalls 64–65).

### Kernel — Sound Blaster 16 Audio Driver (`kernel/sb16.inc`)

- ISA DMA playback via SB16 DSP (base port 0x220, IRQ 5, DMA channels 1/5).
- Supports 8-bit and 16-bit PCM, mono/stereo, configurable sample rate.
- Three syscalls: `SYS_AUDIO_PLAY` (50), `SYS_AUDIO_STOP` (51), `SYS_AUDIO_STATUS` (52).

### Kernel — Process Management

- `SYS_KILL` (53): Terminate a task by PID.
- `SYS_GETPID` (54): Get current task PID.
- `SYS_PROCLIST` (66): Query task table slots (0–15).
- `SYS_MEMINFO` (67): Report free/total physical pages.

### Kernel — GUI Enhancements

- **Clipboard**: `SYS_CLIP_COPY` (55) and `SYS_CLIP_PASTE` (56) for inter-app text sharing.
- **Notifications**: `SYS_NOTIFY` (57) — toast-style notifications with color accent bars.
- **File dialogs**: `SYS_FILE_OPEN_DLG` (58) and `SYS_FILE_SAVE_DLG` (59).
- **Date setting**: `SYS_SETDATE` (49) — write to RTC from user programs.
- **Widget toolkit**: 7 sub-functions (button, checkbox, progress bar, textbox, listbox, label, rectangle outline) for GUI_DRAW_BUTTON through GUI_DRAW_RECT (sub-functions 20–26).

### Kernel — Screensaver System (`kernel/screensaver.inc`)

- 5 screensaver modes: Starfield (64 parallax stars), Matrix (cascading green columns), Pipes (6 colored growing pipes), Bouncing Logo, Plasma (color-cycling plasma effect).
- Activates after 5 minutes idle. `scrsaver` shell command to cycle/set mode.

### Kernel — Bug Fixes

- **VBE font restore**: Save/restore 8 KB VGA plane 2 font data before/after BGA mode switches. Fixes corrupted text mode characters after exiting Burrows desktop.
- **3D button rendering**: `gui_bb_button_3d` left-highlight vline clobbered EDX (button height) with the white color value. This caused 3 spurious dark vertical lines from each window's title bar buttons to the bottom of the screen. Fixed by preserving EDX across the vline call.
- **HBFS directory caching**: Added cache-tag check in `hbfs_load_root_dir` to avoid redundant disk reads during PATH searches.
- **HBFS timestamps**: Files now store RTC-based create/modify timestamps in DOS-compatible packed format.
- **HBFS symbolic links**: File type 5 (`FTYPE_LINK`) with `ln -s` shell command and `stat` resolution.

### Shell — New Commands

- **`stat`**: Display file metadata (type, size, blocks, timestamps, link target).
- **`fsck`**: Filesystem consistency check (bitmap vs. directory cross-validation).
- **`whoami`**: Print current user name.
- **`ln`**: Create symbolic links (`ln -s target linkname`).
- **`scrsaver`**: Cycle or set screensaver mode.
- **`mouse`**: Show mouse position and button state.
- Total: 58 unique commands + 6 aliases = 64 dispatched names.

### Programs — 61 New Programs (79 → 140)

New games: blackjack, breakout, chess, connect4, freecell, kingdom, mastermind, neurovault, outbreak, pong, puzzle15, raycaster, rogue, simon, solitaire, starfield.

New utilities: asm (in-OS assembler), banner, base64, basename, bcalc, bedit, bforager, bhive, bnotes, bpaint, bplayer, bsettings, bsheet, bsysmon, bterm, bview, cmp, csv, cut, debug, df, diff, dirname, du, expr, factor, find, forth, free, grep, hexdump, id, lolcat, mandel, nl, od, paste, periodic, perl (interpreter), pipes, ps, sed, seq, sort, strings, sysinfo, tac, tcc (C compiler).

### Programs — Perl Interpreter & Samples

- **perl**: In-OS Perl interpreter supporting variables, arrays, hashes, control flow, string operations, and built-in functions.
- 6 Perl sample scripts: `hello.pl`, `factorial.pl`, `fizzbuzz.pl`, `guess.pl`, `arrays.pl`, `strings.pl`.

### Syscall Summary

20 new syscalls added (48 → 68 total, numbered 0–67):

| Range | Syscalls |
| --- | --- |
| 49 | `SYS_SETDATE` |
| 50–52 | `SYS_AUDIO_PLAY`, `SYS_AUDIO_STOP`, `SYS_AUDIO_STATUS` |
| 53–54 | `SYS_KILL`, `SYS_GETPID` |
| 55–56 | `SYS_CLIP_COPY`, `SYS_CLIP_PASTE` |
| 57 | `SYS_NOTIFY` |
| 58–59 | `SYS_FILE_OPEN_DLG`, `SYS_FILE_SAVE_DLG` |
| 60–63 | `SYS_PIPE_CREATE`, `SYS_PIPE_WRITE`, `SYS_PIPE_READ`, `SYS_PIPE_CLOSE` |
| 64–65 | `SYS_SHMGET`, `SYS_SHMADDR` |
| 66–67 | `SYS_PROCLIST`, `SYS_MEMINFO` |

### Documentation

- All 7 documentation files verified and updated against actual kernel code.
- Fixed outdated statistics across README, USER_GUIDE, INSTALL, TUTORIAL, TECHNICAL_REFERENCE.

### ISO Distribution

- **Bootable ISO**: `make iso` produces an El Torito no-emulation ISO with full disk image, all 7 docs, README, LICENSE, and CHANGELOG.
- **Lite ISO**: `make iso-lite` truncates the 2 GB disk image to 64 MB (~65 MiB ISO vs ~2.1 GiB full) — all HBFS data preserved.
- **ISO verification**: `make iso-verify` validates the El Torito boot record and boot-load-size.
- **ISO launcher**: `run_iso.sh` extracts and boots the ISO with colored output and ASCII banner.
- **Build script**: `build_iso.sh` rewritten with ANSI-colored status output, pre/post build stats, and SHA256 checksums.
- Supports 4 ISO-creation backends: xorriso, genisoimage, mkisofs, hdiutil.

### Build Stats

- Disk image: 169 files, 710 blocks across 4 subdirectories
- Kernel: ~28,800 lines of x86 assembly (22 include files)
- Kernel binary: ~550 KB
- Tests: 1,160 (45 build + 1,115 HBFS integrity)
- Syscalls: 68 (0–67)
- Programs: 140 assembly + 11 C samples + 6 Perl samples

---

## v2.1.0 - Full TCP/IP Networking Stack

### Kernel — TCP/IP Networking (`kernel/net.inc`, ~3,800 lines)

The former RTL8139 networking stub has been replaced with a complete, from-scratch TCP/IP stack:

- **RTL8139 NIC driver**: PCI auto-detect (bus/device/function scan), software reset with timeout, interrupt-driven RX/TX (ISR handles ROK, TOK, RER, TER, LinkChg), 8 KB RX ring buffer with wrap-around, 4 TX descriptors with round-robin rotation.
- **Ethernet II**: Frame construction and parsing with proper EtherType dispatch (0x0800 IPv4, 0x0806 ARP).
- **ARP**: Request/reply handling, 16-entry ARP cache with lookup and expiry, gratuitous ARP on interface configuration.
- **IPv4**: Header construction with checksum, packet reception and protocol dispatch (ICMP=1, UDP=17, TCP=6).
- **ICMP**: Echo request/reply (ping) with sequence numbering and round-trip time calculation.
- **UDP**: Connectionless send/receive with port matching, used for DHCP and DNS.
- **TCP**: Full state machine — SYN → SYN-ACK → ESTABLISHED → data transfer → FIN/FIN-ACK → CLOSED. Sequence/acknowledgment number tracking, 1460-byte MSS, retransmission, connection timeout.
- **DHCP client**: Complete 4-phase negotiation (DISCOVER → OFFER → REQUEST → ACK) with option parsing for subnet mask, gateway, DNS server, and lease time.
- **DNS resolver**: UDP-based query construction and response parsing with answer section extraction.

### Kernel — Socket API (10 new syscalls)

| Syscall | Number | Description |
| --------- | -------- | ------------- |
| `SYS_SOCKET` | 39 | Create a TCP or UDP socket |
| `SYS_CONNECT` | 40 | Connect a TCP socket to a remote host:port |
| `SYS_SEND` | 41 | Send data on a connected socket |
| `SYS_RECV` | 42 | Receive data from a connected socket |
| `SYS_BIND` | 43 | Bind a socket to a local port |
| `SYS_LISTEN` | 44 | Mark a socket as listening for connections |
| `SYS_ACCEPT` | 45 | Accept an incoming TCP connection |
| `SYS_DNS` | 46 | Resolve a hostname to an IPv4 address |
| `SYS_SOCKCLOSE` | 47 | Close a socket and free resources |
| `SYS_PING` | 48 | Send an ICMP echo request and wait for reply |

### Kernel — ISR TX Re-entrance Fix

- **Problem**: TCP acknowledgments and DHCP responses were sent from within the RTL8139 interrupt handler (ISR), which could corrupt in-flight TX descriptors and cause packet loss or hangs.
- **Solution**: Added `SOCK_PENDING` field (offset 48) to the socket structure. The ISR stores pending flags (ACK, SYN-ACK, FIN-ACK) instead of calling `tcp_send_flags` directly. Polling loops in `sys_connect`, `sys_recv`, `sys_send`, `sys_accept`, and `sys_sockclose` call `tcp_flush_pending` to drain deferred transmissions outside interrupt context.
- **Impact**: Fixed DHCP timeout failures and TCP connection stalls.

### Kernel — Bug Fixes

- **`sys_recv` return path**: Changed `ret` to `iretd` — the syscall returns via interrupt frame, not a near return. Missing `iretd` caused stack corruption and triple faults after receiving data.
- **DHCP buffer overflow**: Reduced DHCP option copy length from 300 to 75 bytes (maximum valid DHCP option payload), preventing overwrite of adjacent kernel data.
- **DHCP race condition**: Added `dhcp_state` flag to prevent processing duplicate OFFER/ACK packets from retriggered responses during the 4-phase handshake.

### Shell — 5 New Networking Commands

- **`dhcp`**: Run DHCP client to obtain IP, subnet mask, gateway, and DNS server.
- **`ping <host>`**: Send ICMP echo request and display RTT.
- **`arp`**: Display the ARP cache (IP → MAC mappings).
- **`ifconfig`**: Show network interface configuration (IP, MAC, gateway, DNS).
- **`net`**: Display NIC status, PCI location, I/O base, and MAC address.

### Programs — 7 New Internet Clients + Network Library

- **forager** — HTTP/1.0 web browser. Connects to port 80, sends GET request, displays response body. Tested end-to-end with `forager example.com` in QEMU.
- **ping** — Standalone ICMP ping utility with configurable count, TTL display, and RTT statistics.
- **telnet** — Interactive Telnet client with raw TCP socket communication and escape sequences.
- **ftp** — FTP client with passive mode, directory listing, file get/put, and cd/ls commands.
- **gopher** — Gopher protocol browser (port 70) with selector navigation.
- **mail** — SMTP mail client for composing and sending email via port 25.
- **news** — NNTP news reader for browsing Usenet newsgroups.
- **`programs/lib/net.inc`**: User-space networking library with socket wrappers, DNS helper, HTTP request builder, and line-buffered receive.

### Build Stats

- Disk image: 96 files across 4 subdirectories
- Kernel: ~20,000 lines of x86 assembly (19 include files)
- Kernel binary: ~399 KB
- Tests: 709 (175 build + 534 HBFS integrity)
- Syscalls: 48 (0–48)
- Programs: 79 assembly + 11 C samples

---

## v2.0.0 - Paging, Preemptive Multitasking, Mouse, Burrows Desktop & 10 New Programs

### Kernel — Virtual Memory

- **Paging** (`kernel/paging.inc`): Identity-maps the first 128 MB via 32 page tables (page directory at 0x380000, page tables at 0x381000). All pages marked present, writable, and user-accessible. `paging_map_page` utility for dynamic page mapping. Page-fault handler (INT 14) prints faulting address, EIP, and error code, then recovers to the shell.

### Kernel — Preemptive Multitasking

- **Preemptive scheduler**: The PIT timer handler (IRQ0) now preempts ring-3 tasks every 100 ms (10-tick quantum at 100 Hz). Checks interrupted code's CPL via the CS RPL bits on the interrupt stack frame — kernel code is never preempted. Round-robin scan for next READY task with TCB ESP save/restore and TSS ESP0 update. Cooperative `SYS_YIELD` still works alongside preemptive switching.

### Kernel — PS/2 Mouse Driver

- **Mouse driver** (`kernel/mouse.inc`): Initializes the PS/2 auxiliary port via the 8042 controller (enable aux device, read/write command byte, set defaults, enable data reporting). IRQ12 handler collects 3-byte packets (flags, delta-X, delta-Y) with packet sync validation (bit 3 check). Tracks `mouse_x` (0–79), `mouse_y` (0–24), and `mouse_buttons` (left/right/middle). PS/2 Y-axis inversion for screen coordinates.
- **`SYS_MOUSE` (syscall 36)**: Returns mouse position and button state (EAX=x, EBX=y, ECX=buttons).
- **`mouse` shell command**: Displays current mouse position and button state.

### Kernel — Burrows Desktop Environment

- **VBE/BGA driver** (`kernel/vbe.inc`): Detects Bochs VBE adapter via I/O ports (IDs 0xB0C0–0xB0C5). Sets linear framebuffer modes from protected mode (no real-mode INT 10h). Default 640×480×32 mode. LFB identity-mapped (8 MB) via `paging_map_page`. Full VGA mode 3 restore with CRTC/GC/AC register reprogramming.
- **Drawing primitives**: `vbe_putpixel`, `vbe_fill_rect`, `vbe_clear`.
- **`SYS_FRAMEBUF` (syscall 37)**: Sub-functions for get info (0), set mode (1), and restore text mode (2).
- **`gui` shell command**: Enters the Burrows desktop at 640×480 with colour bars and mouse cursor tracking. Press any key to return to text mode.

### Shell — Wildcard Expansion

- **Global glob preprocessing** (`shell_expand_globs`): Before command dispatch, all arguments containing `*` or `?` are expanded against the current directory listing. Matching filenames replace the glob pattern in the command line. Unmatched globs are passed through literally (POSIX behavior). This makes wildcards work for **all** commands (e.g., `cat *.txt`, `wc *.c`), not just `del` and `copy`.

### Programs — 10 New Showcase Programs

- **top** — Live process/task monitor showing scheduler state, memory usage, and uptime
- **rogue** — ASCII roguelike dungeon crawler with FOV, inventory, combat, and multiple dungeon levels
- **starfield** — Animated 3D starfield simulation with parallax depth effect
- **matrix** — Matrix-style falling green character rain animation
- **weather** — Simulated weather station with multi-day forecast display
- **periodic** — Interactive periodic table browser with element details
- **forth** — FORTH language interpreter with stack operations, arithmetic, and word definitions
- **chess** — Two-player chess with move validation, check detection, and Unicode-style pieces
- **clock** — Analog ASCII clock with sin/cos lookup tables, hour/minute/second hands, and digital display
- **asm** — Interactive x86 assembler REPL that shows machine code bytes for ~25 instruction types

### Build Stats

- Disk image: 83 files, 248 blocks used
- Kernel: ~13,800 lines of x86 assembly (19 include files)
- Tests: 618 (84 build + 534 HBFS integrity)
- Syscalls: 38 (0–37)
- Programs: 66 assembly + 11 C samples

---

## v1.16 - Batch Scripting, Cooperative Multitasking & Networking

### Shell Enhancements

- **Batch scripting directives**: `:LABEL`, `goto LABEL`, `if [not] errorlevel N cmd`, `rem` comments, and `@cmd` (silent execution). Batch scripts now support conditional branching and flow control.
- **Background job detection**: Trailing `&` on a command line is detected and stripped with a "not yet supported" message; the command runs in the foreground. Prepares the shell syntax for future background job support.
- **`net` command**: Displays NIC status, PCI location, I/O base address, and MAC address.
- **`ls -l` timestamps**: Long directory listing now shows a `Modified` column with time since boot in `HHH:MM:SS` format, read from the HBFS `DIRENT_MODIFIED` field.

### Kernel / Subsystems

- **`SYS_STDIN_READ` (syscall 34)**: Programs can read piped stdin data from the shell's redirection buffer. Returns byte count in EAX, or -1 if no stdin is available.
- **`SYS_YIELD` (syscall 35)**: Cooperative yield syscall for the new scheduler. Saves the current task's ring-3 context and switches to the next ready task via round-robin.
- **Cooperative scheduler** (`kernel/sched.inc`): Task Control Block (TCB) array supporting up to 4 concurrent tasks. Per-task kernel stacks allocated from PMM. Round-robin `sys_yield` with proper TSS ESP0 updates for ring transitions.
- **RTL8139 networking stub** (`kernel/net.inc`): PCI bus 0 scan for RTL8139 NIC, software reset, RX/TX buffer allocation via PMM, MAC address read, and frame transmit function. Foundation for future TCP/IP support.

### Programs — Stdin Pipe Support

- **sort, tr, grep, sed, cut**: Now accept piped stdin when no filename argument is given (e.g., `cat file | sort`).
- **tee**: Redesigned to support `tee OUTFILE` (reads stdin) in addition to the legacy `tee INFILE OUTFILE` mode.

### Tests & CI

- **Portable test script**: `tests/test_build.sh` now works on both macOS and Linux. Replaced GNU-specific `stat -c` with a `wc -c` based `file_sz()` helper, and `grep -oP` with `grep -E -o` pipelines.
- **Syscall consistency**: Test now verifies all 36 syscalls (was 34).

### Build Stats

- Disk image: 73 files, 233 blocks used
- Kernel: ~12,300 lines of x86 assembly (16 include files)
- Tests: 45 build + 534 HBFS integrity
- Syscalls: 36 (0–35)

---

## v1.15 - Kernel Hardening & Interrupt Safety

### Kernel Bug Fixes

- **`build_cwd_path` buffer overflow**: Path construction wrote to a 256-byte buffer but max path depth (16 levels × 253 chars) could reach ~4 KB. Enlarged `path_search_buf` to 4096 bytes and added bounds checking with guaranteed null termination on truncation.
- **`copy_word` unbounded write**: Shell word extraction had no destination size limit. Added bounded `copy_word_n` variant (ECX = max bytes, always null-terminates). Migrated `cmd_find_file`, `cmd_append_file`, and `cmd_mkdir` to use the bounded version.
- **Ctrl+C redirection state leak**: Pressing Ctrl+C during a redirected command (e.g., `prog > file`) left `stdout_redir_active` and related flags set, corrupting subsequent output. The Ctrl+C handler now calls `shell_redir_reset` to clear all redirection state before returning to the shell prompt.
- **Interrupt-unsafe stdout redirection**: A keyboard interrupt during `vga_putchar`'s redirection check could corrupt the redirect buffer or length counter. Wrapped the redirection capture section with `cli`/`sti` to make the test-and-write atomic.
- **Interrupt-unsafe directory state**: `cd` path traversal modified `dir_depth`, `current_dir_lba`, `current_dir_sects`, and `current_dir_name` without interrupt protection. A Ctrl+C mid-update could leave the directory stack inconsistent. Added `cli`/`sti` around state mutations in both `cd_enter_subdir` and `cd_pop_stack`.

### Library Bug Fixes

- **`io_file_write` register mapping**: Register remapping clobbered ECX (size) before copying it to EDX. The syscall received the buffer address as the size argument, causing writes to produce corrupted or zero-length files. Fixed register assignment order to preserve all parameters correctly.

### Build System

- **Makefile lib dependency tracking**: Program build rule now depends on `$(wildcard programs/lib/*.inc)` in addition to `syscalls.inc`, so changes to any library file trigger program rebuilds.

### Tests

- **NOBITS warning regression test**: New test in `test_build.sh` scans all `.lst` files for "nobits" warnings, catching `section .bss` ordering bugs (like the v1.14 math.inc issue).
- **File content integrity test**: New test in `test_hbfs.py` reads known program binaries from the disk image and compares them byte-for-byte with the local `.bin` files, catching populate.py data corruption.

### Documentation

- **API_REFERENCE.md**: Added error handling patterns table documenting which functions use EAX=-1, EAX=0/null, or carry flag for error signaling.

## v1.14 - Test Suite Expansion & Shell Hardening

### Bug Fixes

- **Alias expansion infinite loop**: If an alias expanded to a command starting with its own name (or circular aliases like `A→B`, `B→A`), the shell would loop forever. Added `alias_expanding` guard flag that limits alias expansion to one level per command line, matching standard shell behavior:contentReference[oaicite:0]{index=0}. The flag is reset at each new prompt.
- **`sys_readdir` unbounded filename copy**: The `SYS_READDIR` syscall copied filenames to the user buffer without length checking, risking buffer overflow if the caller provided a small buffer. Now capped at `HBFS_MAX_FILENAME` (252) bytes with forced null termination.

### Test Suite

- **Build tests expanded (31 → 44)**: New checks include:
  - All 55 program binaries built successfully (was only 12 spot-checked)
  - No program exceeds 1MB (`PROGRAM_MAX_SIZE`)
  - 9 HBFS constant consistency checks between `kernel.asm` and `populate.py`
  - All 34 syscall numbers verified consistent between `kernel.asm` and `programs/syscalls.inc`
  - Kernel binary entry point validation
- **HBFS integrity tests expanded (40 → 534)**: New checks include:
  - Full subdirectory traversal — all 4 subdirectories validated with child file entry checks
  - Program binary header validation — all 55 executables checked for valid x86 opcode at entry
  - Global block allocation overlap check across all directories (root + subdirectories)
  - Bitmap-vs-file block count cross-verification
  - Stray bitmap bit detection beyond allocated range
  - File census — total files across all directories verified (72 files)

### Build Stats

- Disk image: 72 files, 229 blocks used
- Kernel: ~10,600 lines of x86 assembly
- Tests: 578 (44 build + 534 HBFS integrity)

---

## v1.13 - Standard PATH-Based Program Search

### Breaking Change

- **Removed global directory search**: File operations (`cat`, `size`, `rm`, `rename`, `stat`, `fd_open`, etc.) now only search the **current working directory**. Previously, any file could be accessed from any directory without a path — the kernel would silently scan root and every subdirectory. This was non-standard.. Users must now either `cd` into the correct directory or use an explicit path (e.g., `cat /docs/readme.txt`).

### Shell / Exec

- **PATH-based program execution**: `cmd_exec_program` still uses the `PATH` environment variable (default `PATH=/bin:/games`) to search for executables not found in the current directory. This is the only remaining multi-directory search and works like Unix `$PATH`.
- **`which` command**: Continues to check builtins first, then CWD, then `PATH` directories — unchanged.

### Bugfix

- **`env_get_var` fix**: `env_get` returns the value pointer in EAX (pushad frame offset 28), but `env_get_var` was checking EDI (unchanged after `popad`) instead of EAX — so it always reported "not found". Fixed to use `test eax, eax`. This was a latent bug masked by the old global directory search; with global search removed, PATH-based exec depended on `env_get_var` working correctly.

### Serial I/O Hardening

- **Hardware probe**: `serial_init` now tests the UART scratch register before configuring COM1. If no serial hardware is detected, `serial_present` is set to 0 and all serial I/O becomes a safe no-op.
- **Non-blocking `serial_getchar`**: Changed from an infinite busy-wait to a non-blocking poll. Returns `0xFF` immediately when no data is available. `SYS_SERIAL_IN` (syscall 33) now correctly returns `-1` when the receive buffer is empty, matching the documented ABI.
- **Guard on `serial_putchar`**: Skips output when `serial_present` is 0, preventing hangs on systems without a UART.
- **`serial` test utility** (`/bin/serial`): New program for interactive bidirectional serial testing. `serial send <text>` sends a line; bare `serial` enters an interactive terminal (green = outgoing, cyan = incoming, Escape to quit).
- **`make run-serial`**: New Makefile target that launches QEMU with serial on TCP port 4555 (`nc localhost 4555` to connect).
- **Documentation**: `readme.txt` and `notes.txt` updated with serial usage instructions, QEMU connection examples, and use cases (debug logging, remote shell, file transfer, automated testing, data export).

### Internal

- **`hbfs_find_file_global`**: Simplified from a full recursive directory scan to a single CWD lookup (`hbfs_load_root_dir` + `hbfs_find_file`). The `.gff_moved` flag is always 0 now (kept for ABI compatibility with callers that check it).
- **`hbfs_read_file`**: Removed the `.not_found` fallback that scanned all directories. Path-qualified filenames (`/dir/file`) still work via the path resolution code path.
- **Kernel binary**: ~470 bytes smaller from removed global search code.

---

## v1.12 - Compiler Fixes, Kernel Hardening & Modular Split

### TCC Compiler Fixes

- **Expression precedence**: Replaced flat single-level expression parser with a 7-level precedence-climbing parser (`||` → `&&` → `==`/`!=` → `<`/`>`/`<=`/`>=` → `+`/`-` → `*`/`/`/`%` → unary). Operators now bind correctly: `2 + 3 * 4` evaluates to 14, not 20.
- **String literal addressing**: Rewrote string handling to use a fixup table. `store_string` returns a string index; `emit_string_data` emits string bytes at the end of the output and patches all fixup locations with correct runtime addresses. Fixes printf/string-literal crashes.

### Build System (v1.12)

- **Auto kernel size**: `stage2.asm` no longer has a hardcoded `KERNEL_SECTORS equ 384`. The Makefile generates `kernel_sectors.inc` from the actual `kernel.bin` size (`ceil(size / 512)`), so the stage 2 loader always loads exactly the right amount.
- **Kernel include tracking**: `$(KERNEL_BIN)` now depends on `$(wildcard kernel/*.inc)`, so touching any include file triggers a rebuild.
- **Regression test suite**: New `make check` target runs 71 automated tests:
  - `tests/test_build.sh` — binary size guards (boot ≤ 512, stage2 ≤ 16 KB, kernel < 512 KB), MBR signature, superblock magic, bitmap and root directory sanity, program binary existence, TCC binary checks.
  - `tests/test_hbfs.py` — deep HBFS integrity: superblock field validation, bitmap-vs-directory consistency, per-file block range and allocation overlap checks.

### Kernel Hardening

- **ATA retry wrappers**: `ata_read_sectors` and `ata_write_sectors` now retry up to 3 times with an ATA soft reset (SRST via control register 0x3F6) between attempts. All existing callers (HBFS, shell commands, syscalls) automatically benefit. The raw single-attempt functions are still available as `ata_read_sectors_raw` / `ata_write_sectors_raw`.
- **HBFS error propagation**: `hbfs_load_root_dir`, `hbfs_load_bitmap`, and `hbfs_save_root_dir` now return CF (carry flag) on I/O failure with descriptive error messages.

### Kernel Modular Split

- **13 include files**: `kernel.asm` is now a 300-line master file (constants, entry point, `%include` directives). The ~10,300 lines of subsystem code are split into:
  - `kernel/vga.inc` — VGA text mode driver
  - `kernel/pic.inc` — PIC initialization
  - `kernel/idt.inc` — IDT setup
  - `kernel/isr.inc` — ISR/IRQ handlers
  - `kernel/pit.inc` — PIT timer + keyboard driver
  - `kernel/pmm.inc` — physical memory manager
  - `kernel/ata.inc` — ATA PIO driver + retry wrappers
  - `kernel/hbfs.inc` — HBFS filesystem
  - `kernel/filesearch.inc` — global file search
  - `kernel/syscall.inc` — syscall handler
  - `kernel/shell.inc` — command shell (~4,200 lines)
  - `kernel/util.inc` — utilities, serial, RTC, speaker, TSS, ELF loader, FD table, env vars, subdir support, new syscalls/commands, tab completion
  - `kernel/data.inc` — string data, scancode tables, IDT descriptor, BSS
- Binary output is **byte-identical** to the monolithic version.

### Build Stats (v1.12)

- Disk image: 48 files, 188 blocks used
- Kernel: ~10,600 lines of x86 assembly (split across 14 files)
- Tests: 71 (31 build + 40 HBFS integrity)

---

## v1.10 - Robustness & Filesystem Integrity Enhancements

### Enhancements

- **`df` total file count**: The `df` command now counts files across **all** directories (root + subdirectories), not just the current directory. Reports "N files in M directories" instead of showing a count for just the CWD.
- **Superblock `free_blocks` tracking**: `hbfs_alloc_blocks` and `hbfs_free_blocks` now update the superblock's `free_blocks` counter (offset 12) after every allocation/deallocation, keeping the on-disk superblock consistent with the bitmap.
- **Nested batch execution guard**: `cmd_exec_batch` now detects re-entrant calls (a `batch` command inside a `.bat` script) and rejects them with an error message instead of silently corrupting the shared `batch_script_buf` / `batch_line_buf` buffers.

### Build Stats (v1.10)

- Disk image: 48 files, 188 blocks used
- Kernel: ~10,000 lines of x86 assembly

---

## v1.9 - Code Review Bug Fixes & Enhancements

### Critical Bug Fixes

- **`.save_type` overflow** (hbfs_create_file): The file type parameter was stored via `mov [.save_type], edx` (32-bit write) into a 1-byte `db 0` variable, corrupting the first 3 bytes of `hbfs_delete_file_entry` (overwriting the `pushad` opcode). Fixed by changing to `dd 0`.
- **`cmd_cd` silent failure**: The `cd` command checked `[esp + 28]` (stale pushad-saved EAX) instead of the actual `EAX` register returned by `cmd_cd_internal`. This meant `cd` to a nonexistent directory never showed an error message. Fixed to `cmp eax, -1`.
- **`fd_close` cross-directory bug**: When a file opened via `hbfs_find_file_global` from another directory was closed after writes, `fd_close` only searched the current directory for the entry to persist the updated file size — silently dropping the update. Fixed by recording the directory LBA/sects in the fd table entry at open time (offsets 20-27), then switching to that directory during close.
- **`sys_exec_call` always returned 0**: The SYS_EXEC syscall returned `xor eax, eax` even when `cmd_exec_program` failed (CF set). Programs calling SYS_EXEC couldn't detect failure. Now returns -1 on failure.

### Enhancements (v1.9)

- **`cat -n` line numbering**: Replaced manual 4-digit space padding with `vga_print_dec_width` for cleaner, more maintainable code.
- **`str_has_wildcards` / `str_has_asterisk`**: Now preserve ESI (push/pop) to prevent subtle caller bugs.
- **ls -l alignment**: Right-aligned file sizes in 9-character field using `vga_print_dec_width`.
- **SYS_FWRITE file type**: ESI parameter now specifies file type (FTYPE_TEXT..FTYPE_BATCH); TCC passes FTYPE_EXEC so compiled programs show as executables.
- **Shutdown message**: Styled with COLOR_HEADER separator bar; message printed before ACPI shutdown to prevent cutoff.

### Build Stats (v1.9)

- Disk image: 48 files, 188 blocks used
- Kernel: ~9,900 lines of x86 assembly

---

## v1.8 - Global File Search (Directory-Transparent Operations)

### hbfs_find_file_global

- **New core function**: `hbfs_find_file_global` searches for a file across all directories — current dir first, then root, then every subdirectory. Returns with CWD pointing to the directory containing the file, so save/delete/rename operations target the correct location.
- **GFF-private CWD save/restore**: Dedicated `gff_save_cwd`/`gff_restore_cwd` with separate BSS slots (`gff_cwd_lba`, `gff_cwd_sects`, `gff_cwd_depth`, `gff_cwd_name`, `gff_cwd_stack`) — avoids conflicts with `file_save_cwd` and `path_save_cwd` used by other subsystems.
- **`.gff_moved` flag**: Callers check this to know whether CWD was changed, and restore it after the operation completes.

### Commands Updated

- **rm / del**: Fixed CPU exception bug — now uses `hbfs_find_file_global` + restores CWD after delete. Files can be deleted from any directory regardless of where the user is.
- **ren / rename**: Uses global search for exact renames; saves directory after rename, then restores CWD.
- **size**: Uses global search to display file info from any directory.
- **SYS_DELETE (syscall 9)**: Programs can now delete files in any directory.
- **SYS_STAT (syscall 11)**: Programs can now stat files in any directory.
- **fd_open**: File descriptors can now open files in any directory.

### Build Stats (v1.8)

- Disk image: 48 files, 188 blocks used
- Kernel: ~9830 lines of x86 assembly (~166KB)

---

## v1.7 - Path-Based File Access

### Path Resolution in hbfs_read_file

- **Full path support**: All file operations (cat, batch, run, diff, head, tail, etc.) now accept absolute and relative paths — e.g., `cat /docs/readme`, `run /bin/hello`, `diff /docs/readme /docs/notes`
- **Automatic path splitting**: `hbfs_read_file` scans filenames for `/`; if found, splits into directory part and basename, cd's into the directory, reads the file, then restores the user's original working directory
- **file_save_cwd / file_restore_cwd**: Separate CWD save/restore functions using dedicated BSS variables (`file_save_lba`, `file_save_sects`, `file_save_depth`, `file_save_name`, `file_save_stack`), avoiding conflicts with `path_save_cwd` used by the PATH search
- **Relative paths**: Supports `../bin/hello`, `games/snake`, `./readme` — resolves via `cmd_cd_internal` which handles `.`, `..`, absolute, and multi-component paths
- **Zero call-site changes**: All 19 callers of `hbfs_read_file` gain path support automatically

### Build Stats (v1.7)

- Disk image: 48 files, 188 blocks used
- Kernel: ~9510 lines of x86 assembly (165KB)

---

## v1.6 - Directory Organization & PATH Search

### Filesystem Restructuring

- **Subdirectory support in populate.py**: Rewrote image builder with `FSImage` class supporting `create_subdir()` and `add_file(directory=...)` methods
- **Organized virtual drive into 4 subdirectories**:
  - `/bin` — 22 utility programs (hello, edit, mandel, tcc, sort, grep, wc, etc.)
  - `/games` — 10 game programs (2048, galaga, guess, life, maze, mine, piano, snake, sokoban, tetris)
  - `/samples` — 10 C source files (hello.c, fib.c, calc.c, matrix.c, wumpus.c, etc.)
  - `/docs` — 5 text files (readme, license, notes, todo, poem)

### PATH-Based Program Search

- **Working PATH mechanism**: Kernel searches colon-separated PATH directories when a program isn't found in the current directory
- **Default PATH**: Set to `/bin:/games` — programs in these directories run from anywhere
- **`set PATH` command**: Users can customize PATH (e.g., `set PATH /bin:/games:/samples`)
- **path_save_cwd / path_restore_cwd**: Utility functions to save and restore full directory state (LBA, sectors, depth, name, dir_stack) during PATH traversal
- **cd-based search**: PATH search cd's into each directory, searches there, reads file data directly from directory entry, then restores the user's original working directory

### Updated Commands

- **which**: Now searches PATH directories; shows full path (e.g., `hello is /bin/hello (external)`)
- **help**: Updated to mention PATH search and configuration instructions

### Bug Fixes (v1.6)

- **Critical PATH fallthrough fix**: `.path_not_found` was falling through into `.found_program` — now correctly jumps to `.not_found`
- **NASM optimization oscillation**: Added `-O0` flag to kernel build to prevent label oscillation errors during assembly

### Build Stats (v1.6)

- Disk image: 48 files, 188 blocks used (4 subdirectories + files)
- Kernel: ~9440 lines of x86 assembly

---

## v1.5 - Command & Program Expansion

### New Internal Commands (11)

- **diff**: Side-by-side file comparison with colored output (< red, > green)
- **uniq**: Remove adjacent duplicate lines; flags: `-c` (count prefix), `-d` (duplicates only)
- **rev**: Reverse each line of a file character-by-character
- **tac**: Print file lines in reverse order (last line first)
- **alias**: Define, list, and show shell command aliases (16-slot table)
- **history**: Display numbered shell command history from history buffer
- **which**: Locate a command — shows if built-in or finds external program on disk
- **sleep**: Pause for N seconds (100 ticks/sec timer), supports Ctrl+C abort
- **color**: Set foreground/background VGA color (hex values 0-F)
- **size**: Show file size in bytes/blocks plus type (text/dir/exec/batch/unknown)
- **strings**: Extract printable strings from a file (default ≥4 chars, configurable via flag)

### Shell Enhancements

- **Alias expansion**: Shell parser checks alias table before command dispatch; recursive expansion into `alias_expand_buf`

### New Helper Functions

- **vga_newline**: Convenience function wrapping `mov al, 0x0A / call vga_putchar`
- **str_compare**: Compare two null-terminated strings at ESI/EDI, sets ZF on match

### New External Programs (9 assembly)

- **life**: Conway's Game of Life — 78×23 grid, glider/blinker/R-pentomino seeds
- **maze**: Random maze generator + BFS solver — 39×21 DFS-carved maze with colored path
- **2048**: The 2048 sliding tile game — 4×4 board, arrow keys/WASD, scoring
- **piano**: PC speaker piano — 15 notes (C4-D5), scale and Mary Had a Little Lamb demos
- **mandel**: Mandelbrot set renderer — fixed-point 16.16 arithmetic, 78×23, color gradient
- **pager**: File pager (like `more`) — 23-line pages, space/enter/q controls
- **sed**: Stream editor — search and replace first occurrence per line
- **tr**: Character translator — SET1→SET2 mapping via 256-byte translation table
- **csv**: CSV file viewer — formatted columns, colored headers, pipe separators

### New C Sample Programs (5)

- **hanoi.c**: Tower of Hanoi solver (4 disks, iterative binary counter method)
- **bf.c**: Brainfuck interpreter with hardcoded Hello World program
- **wumpus.c**: Hunt the Wumpus — 8-room cave, move/shoot, hazards
- **matrix.c**: Matrix rain effect — falling characters animation (40 columns × 20 rows)
- **calc.c**: Integer calculator — multi-digit numbers with +, -, *, / operators

### TCC Compiler Bug Fixes (3)

- **line_num reset**: Line counter not reset between compilations — second compile reported wrong line numbers
- **add_global_var extra next_token**: Global variable declarations consumed one too many tokens — broke subsequent parsing
- **Assignment expr_name clobbering**: Assignment expression overwrote expr_name register — corrupted variable name lookup

### Build Stats (v1.5)

- Disk image: 48 files, 172 blocks used
- Kernel: ~9250+ lines of x86 assembly

---

## v1.4 - HBFS Filesystem Expansion

### Bug Fixes (v1.4)

- **sys_free double-shift**: `pmm_free_page` expects physical address but `sys_free` was converting to page number first, causing double `shr 12` and freeing wrong pages — corrupted memory bitmap
- **cmd_copy_file stack corruption**: Wildcard paths jumped to `.src_not_found` which did `pop esi`, but wildcard paths never pushed ESI — stack corruption on "no matches" case
- **env_get_var wrong register**: Checked `EDI == 0` instead of comparing EDI to saved copy; EDI was always non-zero (dest buffer pointer), so variable-not-found was never detected — broke PATH-based program search
- **hbfs_create_file overflow**: `.copy_name` loop had no bounds check against `HBFS_MAX_FILENAME` (252); long filenames could overflow into metadata fields
- **df bitmap scan**: Only scanned 512 of potentially 2000+ bitmap bytes — reported ~1/4 of actual disk usage on 64MB disks
- **hbfs_read_file stale buffer**: Did not call `hbfs_load_root_dir` before `hbfs_find_file`, could search stale directory data
- **fd_close size persistence**: File size updated via `SYS_WRITE` was only stored in the FD table — never written back to the directory entry on close; file appeared truncated after reopen
- **Batch script overwrite**: `cmd_exec_batch` loaded scripts to `PROGRAM_BASE` where shell commands (cat, head, copy) also load data — commands would overwrite the batch script mid-execution
- **ATA LBA48 bits 24-31**: Both `ata_read_sectors` and `ata_write_sectors` zeroed LBA byte 3 instead of sending bits 24-31 from EAX — limited disk access to 8GB (16M sectors) instead of the full 32-bit LBA range

### New Syscalls

- **SYS_MKDIR (12)**: Create a subdirectory; EBX = name pointer, returns EAX = 0 success / -1 error
- **SYS_READDIR (13)**: Read directory entry by index; EBX = filename buffer, ECX = entry index, returns EAX = file type (-1 = end), ECX = file size

### Documentation Fixes

- Version text updated: v1.3 → v1.4, 28 → 227/56 files, 33 → 34 syscalls
- Banner string updated: HB Lair v1.3 → v1.4
- `hbfs_find_file` comment: "root directory" → "current directory"
- `HBFS_DIR_ENTRY_SIZE` comment: corrected field sizes and order to match actual offsets
- `populate.py`: Fixed root dir comment (2 → 16 blocks), readme.txt (34 syscalls), notes.txt (added SYS_SERIAL_IN), todo.txt (34 syscalls)
- `syscalls.inc`: Documented SYS_MKDIR/SYS_READDIR as now implemented

### Internal Changes (v1.4.1)

- Extracted `hbfs_mkdir` shared function from `cmd_mkdir` — used by both shell command and `SYS_MKDIR` syscall
- `cmd_exec_batch` uses 32KB `batch_script_buf` in BSS instead of `PROGRAM_BASE`
- `fd_close` scans directory by `start_block` to persist file size for writable FDs
- `ata_read_sectors` / `ata_write_sectors` send `EAX[24:31]` as LBA byte 3 in high phase

### Major Changes

- **Root directory expanded**: 2 blocks → 16 blocks (28 → 227 file entries per directory)
- **Subdirectories expanded**: 1 block → 4 blocks per subdirectory (14 → 56 entries each)
- **Multi-level subdirectories**: Full support for nested directories to 16 levels deep
- **Directory stack**: Parent directory tracking via push/pop stack enables proper `cd ..` from any depth
- **Multi-component paths**: `cd a/b/c`, `cd ../sibling`, `cd /abs/path` all work correctly
- **Full path display**: Shell prompt, `pwd`, and `SYS_GETCWD` show complete path (e.g., `/projects/src`)

### Disk Layout Changes

- Kernel area increased from 192 to 384 sectors (96KB → 192KB) to accommodate larger BSS
- Superblock moved from LBA 225 to LBA 417
- Bitmap at LBA 418, Root directory at LBA 426-553, Data starts at LBA 554
- **Note**: Existing disk images must be reformatted (incompatible layout change)

### Internal Changes

- New `HBFS_SUBDIR_BLOCKS` constant (4) controls subdirectory allocation size
- New `build_cwd_path` utility builds full path string from directory stack
- `hbfs_format` uses loop to zero all 16 root directory blocks
- `cmd_mkdir` zeros all allocated blocks and stores correct block count
- All 12 directory iteration loops auto-adapt via `hbfs_get_max_entries`
- Added BSS: `dir_depth` (dword), `dir_stack` (16 × 264 bytes = 4,224 bytes)
- `hbfs_dir_buf` expanded from 8KB to 64KB

## v1.3 - Usability & Security Update

### New Features

- **Command-line arguments**: Programs receive arguments via `SYS_GETARGS` (syscall 32); shell parses `program arg1 arg2` syntax
- **Ctrl+C hard-abort**: Keyboard IRQ detects Ctrl+C while a program is running and immediately returns to shell (no program cooperation needed)
- **FD write implementation**: `SYS_WRITE` via file descriptors now performs real block read-modify-write to disk instead of being a stub
- **Raw disk access restriction**: `SYS_DISK_READ` (22) and `SYS_DISK_WRITE` (23) are denied to ring 3 user programs for security

### New Programs

- **cal.asm**: Calendar display showing current month with day-of-week calculation (Sakamoto's algorithm), highlights today
- **calc.asm**: Interactive integer calculator with +, -, *, /, % operators, hex output, signed arithmetic

### Enhancements (v1.3)

- **edit.asm**: Now accepts filename from command line (`edit myfile.txt`) via SYS_GETARGS instead of always editing scratch.txt
- **Syscall count**: 33 syscalls (added SYS_GETARGS = 32)
- **Version text**: Updated to v1.3 with new feature descriptions

### Documentation (v1.3)

- **INSTALL.md**: Complete build and installation guide
- **USER_GUIDE.md**: Comprehensive user manual with all commands
- **TECHNICAL_REFERENCE.md**: Architecture, memory map, HBFS spec, driver details
- **PROGRAMMING_GUIDE.md**: Tutorial on writing Mellivora OS programs
- **API_REFERENCE.md**: Complete syscall API reference with examples

## v1.2 - Major Feature Release

### Bug Fixes (v1.2)

- **IRQ PIC2 EOI**: Split irq_stub into PIC1-only and PIC2 variants; PIC2 IRQs now send EOI to both controllers
- **ATA sector overflow**: LBA48 sector count now sends high byte (CH) instead of always 0
- **cmd_copy redundant find**: Removed unnecessary second `hbfs_find_file` call; uses ECX from `hbfs_read_file` directly
- **guess.asm backspace**: Backspace now does BS+space+BS for proper visual erase
- **CHANGELOG programs**: Fixed v1.0 program list to match actual programs (banner, colors, guess, primes)
- **populate.py docs**: Fixed stale notes.txt (root dir LBA 234-249, data LBA 250+), updated todo.txt/readme.txt

### Architecture Enhancements

- **Ring 3 user mode**: Programs now run in ring 3 with TSS (selector 0x28), user code/data segments (0x18/0x20)
- **ELF loader**: Minimal ELF32 binary loader - parses ELF magic and loads PT_LOAD segments
- **Boot splash**: Stage 2 displays blue title bar ("Mellivora OS - Booting...") during boot
- **Program return code**: SYS_EXIT saves EBX as program exit code; shell reports non-zero codes

### New Drivers

- **Serial console**: COM1 at 115200 baud for debug output; serial_init/serial_putchar/serial_print
- **RTC clock**: Read date/time from CMOS (ports 0x70/0x71) with BCD-to-binary conversion
- **PC speaker**: PIT channel 2 beep via port 0x61; configurable frequency and duration

### New Syscalls (6 new, total 30)

- **SYS_BEEP (24)**: Play tone on PC speaker (EBX=frequency, ECX=duration_ms)
- **SYS_DATE (25)**: Read RTC date/time into buffer
- **SYS_CHDIR (26)**: Change current directory
- **SYS_GETCWD (27)**: Get current working directory
- **SYS_SERIAL (28)**: Write string to serial port
- **SYS_GETENV (29)**: Get environment variable value
- **SYS_OPEN/READ/WRITE/CLOSE/SEEK (5-8,10)**: File descriptor operations implemented

### New Shell Commands (13 new, total 34)

- **echo**: Print text with $VAR environment variable expansion
- **wc FILE**: Line, word, and byte count
- **find FILE PATTERN**: Substring search with line numbers
- **append FILE TEXT**: Append text to existing file
- **date**: Display current date/time (YYYY-MM-DD HH:MM:SS)
- **beep**: Play 1000Hz tone for 200ms
- **batch FILE**: Execute shell commands from a script file
- **mkdir NAME**: Create subdirectory entry
- **cd DIR**: Change current directory
- **pwd**: Print working directory
- **set NAME=VALUE**: Set environment variable
- **unset NAME**: Remove environment variable
- **Tab completion**: Filename auto-completion in shell
- **Ctrl+C**: Interrupt running program

### New Subsystems

- **File descriptors**: 8-slot FD table with open/read/write/close/seek operations
- **Environment variables**: 16 variables, 128 bytes each, $VAR expansion in echo/batch
- **Subdirectories**: Basic directory support with current_dir_lba tracking

### New Programs (v1.2)

- **edit.asm**: Full-screen text editor with cursor movement, insert/delete, Ctrl+S save, Ctrl+Q/ESC quit
- **tetris.asm**: Classic Tetris with 7 tetrominoes, rotation, scoring, levels, next-piece preview

### Code Quality

- **syscalls.inc**: Shared include file with all 30 SYS_* constants and common print_dec routine
- **All 10 programs**: Refactored to use `%include "syscalls.inc"`, eliminated duplicated constants and print_dec
- **Makefile**: Added .lst listing files, populate.py as dependency, syscalls.inc as program dependency
- **Named constants**: DIRENT_* offsets for directory entry fields replace magic numbers

## v1.1 - Comprehensive Review & Hardening

### Bug Fixes (v1.1)

- **Multi-block filesystem**: Files can now span multiple 4KB blocks. `hbfs_alloc_blocks` allocates N contiguous blocks, `hbfs_create_file` writes all sectors, `hbfs_delete_file_entry` frees all blocks.
- **parse_hex_byte**: Fixed inverted carry flag semantics in hex byte parser (enter command).
- **Shift key bounds check**: Added guard for scancodes < 0x20 before shift_table lookup to prevent out-of-bounds read.
- **Keyboard buffer overflow**: Added buffer-full check before writing to ring buffer.
- **cmd_cat overflow**: Clamp file read size to PROGRAM_MAX_SIZE - 1 to prevent null-terminator overflow.
- **ATA flush**: Moved FLUSH CACHE command outside the write loop (was flushing after every sector).
- **Rename length check**: Filename copy now checks against HBFS_MAX_FILENAME (252 chars).
- **Snake tail rendering**: Save old tail position before shift_body loop, use saved coordinates for erase.
- **Sysinfo wasted division**: Removed useless first div in uptime calculation (result was immediately overwritten).
- **Minesweeper stack overflow**: Converted recursive 8-way flood_reveal (up to 800-deep recursion, ~80KB stack) to iterative algorithm with explicit stack array.

### Robustness Improvements

- **IDT fully populated**: All 256 IDT entries now filled with isr_default, preventing #GP on unexpected interrupts.
- **Exception handlers**: Separate handlers for exceptions with/without error codes. Prints faulting EIP and error code, then recovers to shell (no more cli/hlt freeze).
- **Syscall register preservation**: Syscall handlers now save/restore EBX, ECX, EDX, ESI, EDI. Only EAX is modified for return value.

### New Syscalls (v1.1)

- **SYS_DELETE (9)**: Delete a file by name
- **SYS_STAT (11)**: Get file size and block count
- **SYS_MALLOC (19)**: Allocate 4KB-aligned physical memory pages
- **SYS_FREE (20)**: Free allocated memory pages
- **SYS_DISK_READ (22)**: Raw disk sector read
- **SYS_DISK_WRITE (23)**: Raw disk sector write

### New Commands (v1.1)

- **df**: Show HBFS filesystem usage (total/used/free blocks, file count)
- **more FILE**: Page-by-page file viewer (23 lines per page, Space/Enter for next, q/ESC to quit)

### New Features (v1.1)

- **Shell command history**: Up/Down arrow keys recall previous commands (stores last 8 commands)
- **PMM multi-page allocation**: `pmm_alloc_pages` allocates N contiguous physical pages
- **Bitmap load helper**: `hbfs_load_bitmap` shared function for bitmap I/O

### Documentation (v1.1)

- Updated version text to v1.1 with new features
- Fixed stale comments in populate.py (directory = 2 blocks/16 sectors, data starts at LBA 250)
- Updated help text with df and more commands

## v1.0 - Initial Release

- 32-bit protected mode kernel with flat 4GB address space
- HBFS filesystem with 4KB blocks and 28-entry root directory
- ATA PIO disk driver with LBA48 support
- VGA 80x25 text mode with 16 colors
- PS/2 keyboard driver with shift key support
- Physical memory manager with bitmap allocator
- Heap allocator (simple bump allocator)
- PIT timer at 100 Hz
- 11 syscalls via INT 0x80
- Shell with 14 built-in commands
- 10 user programs (hello, banner, colors, fibonacci, guess, primes, sysinfo, snake, mine, sokoban)
