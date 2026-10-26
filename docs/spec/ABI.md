# ABI — the binary interface (normative)

**Normative from:** M0 for the C boundary (as built), M1 for v2. **Governs:** the
calling convention, the type system at every boundary, structure layout and
versioning, status codes, handles, names, and the encoding of system calls.

The ABI is the one document every other specification leans on. It is written so
that a structure defined here is still valid after the kernel gains an object
manager, a 64 GiB physical ceiling and — if it ever happens — a 64-bit build.

**v2 (M1)** adds the 14 object-manager calls (`187`–`200`), the `u32` handle and
its encoding, and the `MV_HANDLE_SELF`/`MV_HANDLE_NONE` constants. Nothing below
number 187 changes, so a v1 program runs unmodified — that is what makes the
compatibility claim in the [ceilings table](README.md#the-ceilings-this-set-removes)
mean something.

## 1. Principles

1. **Fixed width at every boundary.** `u8`, `u16`, `u32`, `u64` and their signed
   forms. No `int`, no `long`, no `size_t`, no pointer in a structure that is
   shared with assembly, stored on disk, sent over a wire or handed to a driver.
   A pointer that must cross a boundary is a `u32` handle or a `u64` address.
2. **The compiler never chooses a layout.** Every shared structure states its
   padding, and both sides assert its size and the offset of every field
   ([S4](README.md#decisions-this-set-records)).
3. **Extension is additive.** A structure gains fields at the end and a version;
   a caller passes the size it knows, a callee refuses a size it cannot read.
   Nothing is ever reordered or reused.
4. **One status space.** Errors do not travel as `-1` with a side channel except
   in the legacy calls, which are frozen and mapped.

## 2. Compilation of C

[As built, and normative for kernel C.] The kernel module and the user programs
are built freestanding by a host compiler proved by
`tools/find_c_toolchain.sh`:

| Setting | Value | Why |
|---|---|---|
| Target | 32-bit ELF (`i686-elf`, `gcc -m32`, `clang --target=i386-elf`) | The system is 32-bit ([S5](README.md#decisions-this-set-records)) |
| Language | C11, `-ffreestanding -fno-builtin` | There is no libc; `c/rt/` is the runtime |
| Position independence | off (`-fno-pic -fno-pie`) | Everything is linked at a fixed address |
| Stack protector | off (`-fno-stack-protector`) | No TLS segment in ring 0 |
| Unwind information | off (`-fno-asynchronous-unwind-tables -fno-unwind-tables`) | Nothing unwinds |
| Instruction set | `-march=pentium4 -mtune=pentium4` | The CPU floor is a Pentium 4; `cmov` and Netburst selection are available |
| Vector state | off for kernel C (`-mno-sse -mno-mmx -msoft-float`) | XMM belongs to tasks, not to ring-0 C; see §7 |
| Warnings | `-Wall -Wextra`, warnings are not errors | The tree carries historical assembly that is not C |

## 3. Types

| Type | Size | Alignment | Notes |
|---|---|---|---|
| `u8` / `i8` | 1 | 1 | |
| `u16` / `i16` | 2 | 2 | |
| `u32` / `i32` | 4 | 4 | Also the width of a handle and of every shared pointer |
| `u64` / `i64` | 8 | **8** | 8-aligned even here, so the structure survives a 64-bit move unchanged |
| `mv_handle` | 4 | 4 | §6 |
| `mv_status` | 4 | 4 | §5 |
| `mv_time` | 8 | 8 | 100 ns units, monotonic or absolute as documented per call |
| `char16` | 2 | 2 | A UTF-16 code unit; the system's native text type |
| `void` | — | — | Used for "no value", never as a pointer type |

Enumerations are `u32`. Bitfields are forbidden in shared structures; use a
`u32` and named masks, because two compilers disagree about bit order in ways
that only show up in the field.

A `u64` on a 32-bit target is returned in `EDX:EAX` and passed as two
consecutive 4-byte stack slots, low half first. Its 8-byte alignment rule means
a structure containing one is 8-aligned, which is where it wants to be anyway.

## 4. Calling convention

The kernel uses **cdecl** at every boundary, including `sched_switch` targets
and driver entry points.

- Arguments are pushed right to left; the caller removes them.
- The return value is in `EAX`; a 64-bit value is in `EDX:EAX`; a structure
  returned by value is the caller's buffer passed as a hidden first argument.
- The callee preserves `EBX`, `ESI`, `EDI`, `EBP` and `ESP`. `EAX`, `ECX`, `EDX`
  and the flags are scratch.
- The direction flag is clear on entry and must be clear on return.
- The stack is 4-byte aligned at the call instruction and must be 16-byte
  aligned at every call the compiler emits, because a callee may use `fxsave`
  or a vector move on 16 bytes of stack. The entry stubs that call C from
  assembly are responsible for that alignment.

The existing kernel is written in a `pushad`/`popad` style that preserves
everything. That is compatible with cdecl and stays as it is: a routine that
preserves more than it must is not a bug.

A routine that documents what it preserves must actually preserve it, and a
hand-written save list is not evidence: the allocator once saved `EDI` and
forgot the `ECX` it clobbered two instructions later, and the caller that held a
page-table address in `ECX` copied 4 KB from physical address 0. A clobbered
register does not fault at the call — it faults somewhere else later — so the
rule is to save the whole set when the body is not trivially inspectable, and to
have a self-test that checks the contract at boot.

## 5. Status codes

All new calls return `mv_status`, a 32-bit value:

| Bits | Field | Meaning |
|---|---|---|
| 31–30 | severity | `00` success, `01` informational, `10` warning, `11` error |
| 29 | customer | set for vendor-specific codes; the system never sets it |
| 28–16 | facility | the subsystem that produced the code |
| 15–0 | code | the condition |

Success is `0`. A caller tests the severity bits, never the whole word, and
never compares against a specific success code. The first constants:

```text
MV_OK                 0x00000000
MV_PENDING            0x00000103   status: complete later, see §9
MV_TIMEOUT            0x00000102
MV_INVALID_PARAMETER  0xC000000D
MV_ACCESS_DENIED      0xC0000022
MV_INVALID_HANDLE     0xC0000008
MV_END_OF_FILE        0xC0000011
MV_NO_MEMORY          0xC0000017
MV_BUFFER_TOO_SMALL   0xC0000023
MV_NOT_SUPPORTED      0xC00000BB
MV_UNEXPECTED         0xC000000D  /* wrong type, wrong state, wrong owner */
```

Legacy syscalls keep returning what they return today — `-1` in `EAX` and an
`errno` in the TCB — forever ([S3](README.md#decisions-this-set-records)). Each
legacy number maps to exactly one `mv_status` in a generated table
(`docs/syscalls.json`), so a new caller can translate and a test can prove the
mapping did not drift.

## 6. Handles

A handle is a 32-bit value that is only meaningful in the process that owns it:

```text
31              18 17                    2 1  0
+-----------------+-----------------------+-----+
|   generation    |      table index      | 00  |
+-----------------+-----------------------+-----+
```

- The low two bits are zero, so a handle can never be mistaken for a small
  integer or a tagged pointer.
- The index is 16 bits, which is what the 65 536-handle ceiling needs, and the
  generation is the remaining 14. `c/include/mv/objects.h` provides
  `MV_HANDLE_MAKE`, `MV_HANDLE_INDEX` and `MV_HANDLE_GEN`, and the kernel uses
  the same shifts in `kernel/object.inc`.
- The **generation** advances every time the slot is reused. A handle that was
  closed and whose slot was handed out again names nothing: the kernel compares
  the generation and returns `MV_INVALID_HANDLE` rather than acting on the new
  contents. This is what makes a stale handle a refusal and not a security bug.
- Handle `0` is always invalid. `MV_HANDLE_CURRENT_PROCESS`, `MV_HANDLE_CURRENT_THREAD`
  and `MV_HANDLE_SELF` are constants in the private range below.
- Handles are per process, never per thread, and are inherited only where a
  create call asks for it.

The layout is an [ABI](README.md) commitment: a program may hold a handle in a
`u32` and compare it for equality, and nothing else may be assumed about it.

## 7. Floating point and vector state

Each task owns a 512-byte image, saved and restored across a context switch with
`fxsave`/`fxrstor` — x87, `MXCSR` and XMM0-15 in one format. There is no second
format: `FXSR`, SSE and SSE2 are part of the CPU floor, so a machine without
them is refused at boot ([PLATFORM.md](PLATFORM.md#8-cpu-features)) instead of
running a degraded save path. `CR4.OSFXSR` and `CR4.OSXMMEXCPT` are set
unconditionally, before paging. The image is captured once at `sched_init`,
because a zeroed `fxsave` area would unmask every SSE exception. This is **as
built** and proved by `fpu` (`FPU_SELFTEST_OK`, run at boot and on demand) and
`programs/fpu_test.asm`.

Consequences:

- **Kernel C stays `-mno-sse`.** Not because the floor lacks SSE2 — it has it —
  but because XMM state then belongs to two owners at once. If ring-0 C used
  XMM, every C call would have to declare which half of the vector state it
  owns, and the per-task image would stop being the single answer.
- **User code may use SSE unconditionally.** Every accepted CPU has SSE2, so a
  program does not have to probe; the scheduler carries its XMM state across
  switches.
- A future per-task extended state (AVX, if the CPU line ever reaches it) is a
  larger image behind the same `TCB_FPU_AREA` pointer and the same save/restore
  entry points, not a second mechanism.

## 8. Text and names

- The native text type is **UTF-16** (`char16`), because it is what the
  Windows-facing namespace, the clipboard and file associations need, and
  because a 255-character name does not fit in a byte string.
- A **name** is at most 255 `char16` code units including the terminator. It may
  contain any code point except `\0`, `/` and `\`; the namespace is
  case-insensitive and case-preserving, with the fold table fixed in
  `c/rt/utf.c` so two machines agree.
- A **byte string** (a path on the wire, a command line, a config value marked as
  such) is UTF-8. Conversion happens once, at the boundary named in the
  specification that owns that boundary, never in the middle.
- Invalid input is refused (`MV_INVALID_PARAMETER`); unpaired surrogates never
  reach a structure, because two different names must never be equal.

## 9. System call encoding

`int 0x80`, unchanged, because 2 000 programs and every sample in the tree use
it ([S3](README.md#decisions-this-set-records)):

| Register | In | Out |
|---|---|---|
| `EAX` | call number | status / result |
| `EBX`, `ECX`, `EDX`, `ESI`, `EDI` | arguments 1–5 | preserved |
| `EDX:EAX` | — | a 64-bit result where the call returns one |

- Every register except `EAX` is preserved, including the flags except where a
  call documents otherwise. User code may hold a value in `ECX` across a call.
- A call needing more than five arguments takes a **parameter block**: a user
  pointer in `EBX` to a structure whose first two fields are `u32 size` and
  `u32 version`. The kernel validates the range, refuses a size it does not
  know, and copies in before acting. No call ever grows by adding a sixth
  register convention.
- **Numbers are never reused.** Retiring a call leaves its number reserved
  forever. The registry is `programs/syscalls.inc`, `docs/syscalls.json` and
  `c/include/mv/syscall_numbers.h`, all generated from the same file and all
  checked by `make lint`.

### 9.1 Number space

| Range | Class | Rule |
|---|---|---|
| `0`–`186` | Legacy | Frozen by number and meaning; implemented over objects, never changed |
| `187`–`200` | Object manager | Allocated and **frozen**: created, opened, duplicated, closed, waited on, signalled, queried, and the four create kinds. Shipped in `docs/syscalls.json`, `c/include/mv/syscall_numbers.h` and the shell's `syscalls` listing, so the "numbers are never reused" rule applies to them from now on |
| `201`–`511` | Object manager | Still free for handle-based calls; may be renumbered before M1 closes |
| `512`–`1023` | Subsystems | Security, storage, config, network, media — one block per facility |
| `1024`–`0x7FFF` | Reserved | Allocated by the specification that needs it |
| `0x8000`–`0xFFFF` | Private | Test and debug calls, not guaranteed to exist |

### 9.2 Asynchronous calls

An operation that cannot complete inside the call returns `MV_PENDING` and
completes on a **port** object ([IPC.md](IPC.md)). Every such call documents:
what the completion message carries, what happens on cancellation, and whether
the operation is restartable. A call that can block is documented as blocking;
none may block unexpectedly, because a program cannot be asked to guess.

## 10. Kernel module ABI

The C-built module contract is versioned the same way as every other structure:
a header at offset 0 of the module with `magic`, `version`, `size` and function
pointers, read by number from both sides and asserted at compile time in
`c/include/mv/cmod.h`. Its growth path — export tables, imports, IRP entry
points — belongs to [DRIVERS.md](DRIVERS.md); what this document fixes is that
the header stays at offset 0, that every field is 4-byte, and that a host table
is refused whole when its size disagrees rather than read past.

## 11. Versioning

| Thing | How it is versioned | What a mismatch does |
|---|---|---|
| Syscall number | Never reused; table is append-only | Old call keeps working forever |
| Parameter block | `size` + `version` in the block | `MV_BUFFER_TOO_SMALL`, or a refusal naming the version |
| Shared structure | `size` + `version`, fields appended | Callee refuses what it cannot read |
| On-disk structure | magic + version + checksum ([STORAGE.md](STORAGE.md)) | Volume refused, never reformatted |
| Module header | `magic` + `version` + `size` | Module refused at load, reported by name |
| `mv_abi_version` | One call returning the running ABI level | A program that needs more says so at start-up |

A change that alters the meaning of an existing field, the size of a structure a
program passes, or the register contract is not a version bump: it is a new call
and a new structure. The old one stays.

## 12. Conformance

`make lint` fails if any of these regress:

1. `docs/syscalls.json` and `docs/syscalls.md` match `programs/syscalls.inc`
   (`check-syscalls-json`, `check-syscalls-md`).
2. `c/include/mv/syscall_numbers.h` matches the same file
   (`check-syscall-header`).
3. No syscall number is defined twice, or reused after retirement
   (`validate-syscalls`).
4. Every shared structure asserts its size and field offsets at compile time; a
   field inserted in the middle is a compile error, not a kernel that reads the
   wrong bytes.
5. The kernel module is where the build says it is, with the header this
   document describes (`make check-cmodule`).
6. `tools/validate_constants.py` proves the assembly and Python views of the
   same number still agree.

## 13. As built (the gap)

| Contract | Tree today |
|---|---|
| `mv_status` on new calls | Legacy `-1`/`errno` only |
| Generated handles with generations | Bare small integers |
| 255 UTF-16 names | 63-byte byte strings |
| Parameter blocks beyond five arguments | Five registers, no block |
| `u64` 8-aligned | Natural alignment capped at 4 in shared structures |
| Object-manager call range | Not allocated yet |
| `mv_abi_version` | Absent |

The C boundary (§2, §10) is in place; the rest arrives with M1.
