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
| FPU | `-mno-sse -mno-mmx -msoft-float` | The CPU floor is an i486, which has no SSE; see §5 |
| Symbol prefix | none | i386 ELF has no leading underscore |

`sh tools/find_c_toolchain.sh` proves a candidate compiles a 32-bit object and
that `ld -m elf_i386` links it. A compiler that cannot do that is rejected: a
native compiler on a non-x86 host builds happily and produces objects the
kernel cannot call. The Makefile reads the probe's answer and refuses to build
without one; `make check-toolchain` reports which toolchain it found.

Two kinds of C are built today, both with the flags above:

- a **kernel module** (`c/kmod/`), linked as a flat blob at an address the
  assembler measures and assembled into the kernel image with `incbin`
  (`kernel/cmod.inc`, the slot at the end of `kernel/data.inc`). It has no
  undefined symbols: everything it may call arrives in a host table passed to
  its `init`, and its header is read by number, with every offset asserted at
  compile time in `c/include/mv/cmod.h`;
- **user programs** (`c/prog/`), linked at `PROGRAM_BASE` (0x00300000) with
  `--oformat binary`, entry first, `.bss` folded into a PROGBITS section so the
  image carries its own zeros rather than inheriting the window's last
  occupant.

Both link the same freestanding runtime (`c/rt/string.c`): `memcpy`, `memmove`,
`memset`, `memcmp`, `strlen`, `strcmp` and `strncmp`. They are not optional —
with optimization on, the compiler itself emits calls to the first four.

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

## 5. Floating point and vector state

The x87 stack, MXCSR and the XMM registers belong to the CPU, not to a task, so
each TCB carries a 512-byte image at `TCB_FPU_AREA` (offset 144, 16-byte
aligned) and every switch path swaps it with the task.

Two save formats, one slot. Where `CPUID.01H:EDX` advertises FXSR the whole
512 bytes are saved with `fxsave`/`fxrstor`, which covers x87, MXCSR and
XMM0-15; otherwise `fnsave`/`frstor` save the 108-byte x87 image the i486 floor
actually has. `cpu_has_fxsr` is sampled once at boot (`kernel/cpu.inc`), so the
save and the load always agree on the format, and `CR4.OSFXSR`/`OSXMMEXCPT` are
set only on a CPU that advertises FXSR — on one that does not, those bits are
reserved and writing them is a `#GP`.

Consequences for C:

- **kernel C stays `-mno-sse -mno-mmx -msoft-float`.** This is now about the
  CPU floor rather than about missing save code: the kernel must run on an
  i486, which has no SSE at all, so vector instructions in ring 0 would fault
  on the oldest machine the tree supports. No kernel path uses `float` or
  `double` for anything that must be exact.
- **user programs may use SSE** where the CPU advertises it. `programs/sse_test`
  checks `CPUID` first and skips on a machine without SSE; where the bits are
  present the kernel has already enabled them, so `movaps` executes instead of
  faulting.

`fpu` (`FPU_SELFTEST_OK`/`FAIL`/`SKIP`, and the `fpu_selftest_boot` line on
COM1) saves two distinct x87+SSE states, disturbs the live registers and
requires each back exactly, so the image is proved before any task exists.

## 6. Versioning

The ABI is versioned in one place: the syscall table. Adding a call is additive
and needs no version bump. Changing the meaning of an existing number, the size
of a structure a program passes, or the register contract does — and requires a
`docs/syscalls.json` regeneration, a CHANGELOG entry and a compatibility note in
this document.
