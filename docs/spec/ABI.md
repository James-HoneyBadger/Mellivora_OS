# ABI — the boundary between NASM and C

**Status:** draft, normative from roadmap wave M1.

New subsystems are written in freestanding 32-bit C ([ADR D2](../ROADMAP.md)).
The boot path, entry stubs, context switch and every existing module stay in
NASM. This document is the contract between the two, and the reason a C module
can be linked into this kernel at all.

## 1. Compilation

| Setting | Value | Why |
|---|---|---|
| Target | `i386` / `i686`, 32-bit ELF, flat or linked image | The kernel is 32-bit protected mode |
| Language | C11, freestanding (`-ffreestanding -fno-builtin`) | There is no hosted runtime |
| Optimisation | `-O2` | Matches the asm kernel's size budget |
| Stack protector | off (`-fno-stack-protector`) | No TLS segment is set up for ring 0 C |
| Position independence | off (`-fno-pic -fno-pie`) | The image is linked at a fixed base |
| Unwind tables | off (`-fno-asynchronous-unwind-tables -fno-unwind-tables`) | Nothing unwinds |
| FPU | `-mno-sse -mno-mmx -msoft-float` until per-task FPU state lands | See §5 |
| Symbol prefix | none | i386 ELF has no leading underscore |

`sh tools/find_c_toolchain.sh` proves a candidate compiles a 32-bit object and
that `ld -m elf_i386` links it. A compiler that cannot do that is rejected: a
native compiler on a non-x86 host builds happily and produces objects the
kernel cannot call.

## 2. Calling convention

The kernel uses **cdecl** for every C boundary.

- Arguments are pushed right to left; the caller removes them.
- Return value in `EAX`; a 64-bit return (a packed structure passed by value) in
  `EDX:EAX`.
- Callee saves `EBX`, `ESI`, `EDI`, `EBP` and `ESP`. `EAX`, `ECX` and `EDX` are
  scratch.
- The direction flag is clear on entry and must be clear on return.

Assembly that calls C does `push` arguments, `call`, then `add esp, N`. C that
calls an assembly routine declares it `extern` and calls it as a normal
function; the assembly side obeys the same rules, including preserving
`EBX`/`ESI`/`EDI`/`EBP`.

The existing kernel is written in a `pushad`/`popad` style that preserves
everything. That is compatible with cdecl and stays as it is.

## 3. Structure layout

Structures cross the boundary, so their layout is fixed rather than left to the
compiler:

- Fixed-width types only: `u8`, `u16`, `u32`, `i8`, `i16`, `i32`. `int` is never
  used for a field that asm reads.
- Natural alignment, capped at 4 bytes. A `u32` is 4-aligned, a `u16` 2-aligned.
- No bitfields and no anonymous unions in a shared structure. Use a `u32` and
  named masks.
- Padding is explicit: a structure asm walks must name its `reserved` fields so
  both sides count the same bytes.
- Enums are 32-bit and are not used as structure fields; use `u32` plus a named
  constant.
- A structure shared with assembly carries a compile-time size assertion:

```c
typedef char assert_tcb_size[(sizeof(struct tcb) == TCB_SIZE) ? 1 : -1];
```

Offsets are asserted the same way against the `%define`d constants already used
by the asm (`TCB_ESP`, `TCB_STATE`, ...), so a field inserted in the middle is a
compile error rather than a kernel that corrupts its own scheduler.

## 4. Syscalls

The user-facing ABI does not change in this plan:

- `int 0x80`, call number in `EAX`, arguments in `EBX`, `ECX`, `EDX`, `ESI`,
  `EDI`.
- The result is returned in `EAX` and **every other register is preserved**.
  User code may hold a value in `ECX` or `EDI` across a syscall.
- Numbers are allocated once in `programs/syscalls.inc` and never reused;
  `docs/syscalls.json` is generated from that file and checked in CI.

A C-implemented syscall body is reached through a small NASM entry stub that
saves the caller's registers, builds the C argument list on the kernel stack,
calls the C handler and writes the result back to the saved `EAX` slot. The
stub is the only place that knows both languages.

## 5. Floating point

The kernel initialises the x87 once at boot and does not save FPU or SSE state
across a context switch, so two tasks using floating point corrupt each other.
Until roadmap M0 adds a 512-byte `fxsave`/`fxrstor` area to the task control
block:

- kernel C is compiled `-mno-sse -mno-mmx -msoft-float`;
- no kernel path uses `float` or `double` for anything that must be exact;
- user programs that need floating point remain a documented hazard.

After that change, SSE2 is permitted (the kernel sets `CR4.OSFXSR`), x87 is
discouraged, and the compiler flags are revisited in the same commit that adds
the state save.

## 6. Versioning

The ABI is versioned in one place: the syscall table. Adding a call is additive
and needs no version bump. Changing the meaning of an existing number, the size
of a structure a program passes, or the register contract does — and requires a
`docs/syscalls.json` regeneration, a CHANGELOG entry and a compatibility note in
this document.
