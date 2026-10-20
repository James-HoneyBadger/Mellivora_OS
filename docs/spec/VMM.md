# VMM specification (normative)

Status: **draft** — this document is the contract for P1. Code that disagrees with it is
the bug.

## 1. Principles

1. **Ring 3 sees only what it owns.** No kernel data structure, page table, allocator
   bitmap, driver register or another process's memory is reachable from user mode.
2. **Every mapping is declared.** A page becomes user-accessible only when a subsystem
   explicitly maps it for a process (program window, stack, framebuffer shadow, IPC
   surface, trampoline).
3. **Fail closed.** A missing flag denies access; it never grants it.
4. **No identity-map shortcuts.** The historical "everything below 128 MB is user
   writable" identity map is being removed. Code that relied on it (PIO buffers, VGA
   text, shadow framebuffers) must declare a mapping instead.

## 2. Address space layout (32-bit, current)

| Range | Contents | Ring 3 access |
|---|---|---|
| `0x00000000`–`0x000A0000` | Real-mode IVT, BIOS data, kernel image, page tables, PMM bitmaps, kernel BSS/heap | **never** |
| `0x000A0000`–`0x000C0000` | Legacy video aperture (VGA text at `0xB8000`) | read/write (compatibility: direct text-mode output is a documented feature) |
| `0x000C0000`–`0x00300000` | ROMs, kernel workspace | **never** |
| `0x00300000`–`0x003C0000` | Program window: code, data, BSS, heap (grows up, capped at `USER_STACK_BASE`) | read/write/execute |
| `0x003C0000`–`0x00400000` | Reserved user stack region: guard page at `0x003C0000`, on-demand growth area, then the top 64 KB mapped at exec (stack starts at `PROGRAM_EXIT_ADDR - 4`) | read/write, guard page **never** |
| `0x00400000`–`0x08000000` | Reserved | **never** |
| `0x08000000`–`0x20000000` | Demand-paged user zone (`mmap`, large heaps) | read/write, per-`vm_area` permissions |
| `0x1FFFF000` | Sigreturn trampoline (single page) | read/execute only |
| `0xFD000000`+ | VBE linear framebuffer | read/write for the process holding the graphics grant |

Kernel mappings are `PG_PRESENT | PG_WRITABLE` with **no** `PG_USER`. User mappings are
`PG_PRESENT | PG_USER` plus `PG_WRITABLE` only where the contract says so.

## 3. Objects

### 3.1 `vm_area`

```
struct vm_area {
    uintptr_t start;      /* page aligned */
    uintptr_t end;        /* page aligned, exclusive */
    uint32_t  prot;       /* PROT_READ | PROT_WRITE | PROT_EXEC */
    uint32_t  flags;      /* MAP_SHARED | MAP_PRIVATE | MAP_ANON | MAP_FIXED */
    struct file *file;    /* MAP_SHARED/MAP_PRIVATE file backing, else NULL */
    uint32_t  file_off;   /* page aligned */
    struct vm_area *next;
};
```

Areas are sorted by `start`, never overlap, and are owned by the address space.

### 3.2 Rules

- `mmap` with a hint of 0 allocates from the demand zone top-down; `MAP_FIXED` replaces
  overlapping areas only after unmapping them.
- `munmap` splits/trims areas and frees frames that are not file-backed or shared.
- `mprotect` may not add `PROT_WRITE` to a file mapping opened read-only.
- `PROT_EXEC` on a writable anonymous mapping requires an explicit `MAP_JIT` flag
  (W^X); without it the request fails with `EPERM`.
- Unmapped pages inside the demand zone fault with `SIGSEGV` (once signals land) or kill
  the task (current containment behaviour).

## 4. Fault semantics

| Fault | Cause | Action |
|---|---|---|
| Demand zero | Anonymous area, not yet backed | Allocate a zeroed page, map it with the area's protections, retry |
| File-backed | Area with a `file` | Read the page through the page cache, map it read-only, COW on write if `MAP_PRIVATE` |
| COW | Write to a shared frame | Copy the frame, drop the old reference, remap writable |
| Guard page | Access immediately below a stack area | Report stack overflow (task killed, shell survives) |
| Permission | Write to a read-only or supervisor page | Deny: kill the task, leave the system running |
| Kernel fault | Any fault with `CS & 3 == 0` | Fatal: panic with a symbolised stack |

Stack growth (implemented): the user stack has its own region at the top of the program
window — `USER_STACK_BASE` (0x003C0000) to `PROGRAM_END_ADDR` (0x00400000), 256 KB in
total. Only the top `USER_STACK_INIT_PAGES` (64 KB) are mapped when a task starts; the
page at `USER_STACK_BASE` is a **permanent guard page** and is never mapped. A
not-present user fault in the region is serviced by `pf_try_stack_growth`, which:

1. finds the lowest mapped stack page by walking the window's page table down from the
   top of the region (the page tables *are* the state, so a forked or resumed task needs
   no bookkeeping);
2. accepts the fault only if it lies at most `STACK_GROWTH_SLACK` (16 pages, 64 KB)
   below that frontier, so a stray pointer cannot reserve the region;
3. maps every page between the fault and the frontier with a freshly zeroed frame
   (`PG_PRESENT | PG_WRITABLE | PG_USER`) and retries the instruction.

Anything else in the region — the guard page itself, a jump further than the slack — is
not growth: the task is killed with the one-line report and the shell keeps running.
`SYS_SBRK` refuses to move the break above `USER_STACK_BASE`, so the heap cannot grow
into the stack region, and `set_program_brk_init` caps the initial break the same way.

Kernel identity demand: the kernel addresses physical memory directly, but only the first
`PAGING_MAP_MB` (128 MB) is mapped at boot. A supervisor not-present fault between
`USER_DEMAND_BASE` (128 MB) and the highest address the firmware reported as usable
(`pmm_mem_top`) maps that page identity in the master and the active directory, so the
buddy allocator can use RAM above 128 MB instead of faulting the first time it touches
it. Addresses beyond the reported RAM still fault, so a kernel bug is not hidden.

## 4a. Status of the 32-bit implementation (P1)

Implemented in `kernel/vm.inc`:

- Per-task `vm_area` table (32 areas per task) recording start, pages, protection
  and the backing physical run.
- `mmap`: anonymous mappings in the demand zone, page-aligned, zero-filled, with a
  randomized first base (16 MB of ASLR spread) and overlap-checked placement.
- `munmap`: frees the frames, clears the PTEs and drops the record. Only whole
  mappings are supported for now; a partial range returns `-1`.
- `mprotect`: rewrites PTE flags for the range and updates the record.
- W^X: `PROT_WRITE|PROT_EXEC` is refused by both `mmap` and `mprotect` unless
  `MAP_JIT` (0x40) is requested.
- Demand faults **fail closed**: a fault in the demand zone is only serviced when
  the address belongs to one of the task's areas. Anything else — including an
  address that `munmap` just released — kills the task.

Also implemented:

- Stack guard page and on-demand stack growth (`paging_setup_user_stack`,
  `pf_try_stack_growth` in `kernel/paging.inc`), set up per task for both the console and
  the Burrows launch paths; the heap is capped at the stack region in `SYS_SBRK` and
  `set_program_brk_init`.
- Kernel identity demand above 128 MB, bounded by the firmware-reported memory top.

Not yet implemented: file-backed mappings, `MAP_SHARED` semantics, demand-zero for
`mmap` ranges that are painted but never touched, and partial unmapping.

## 5. Isolation invariants (tested)

1. A ring 3 write to `0x00100000` (kernel image) faults and cannot be observed to
   succeed.
2. A ring 3 read of another task's program window faults.
3. A ring 3 write to a read-only file mapping faults unless it is `MAP_PRIVATE`.
4. After `munmap`, the range faults.
5. No stale bytes: a fresh anonymous page reads as zero.
6. Kernel code may still write VGA text (`0xB8000`) and its own memory without faults.

7. A task that writes past the bottom of its stack region dies alone: the guard page is
   never mapped and the shell survives (`stack_overflow`).
8. A task can grow its stack down to the last page above the guard, and every grown page
   keeps what was written to it (`stack_test`).

Each invariant has a regression test; `tests/test_runtime.sh` drives `memprot_test`,
`vmtest`, `stack_test` and `stack_overflow` over QEMU and `make check` fails if any
regresses.

## 6. Compatibility exceptions

These exist for documented features and are bounded:

| Exception | Why | Bound |
|---|---|---|
| VGA text aperture user-writable | "Direct VGA access" is a documented programming technique; `color`, `top` and friends rely on it | 128 KB window, affects only the screen |
| Framebuffer shadow buffer user-writable | `SYS_FRAMEBUF/0` hands programs a shadow buffer they render into | Only the buffer's own pages, mapped per process |
| Sigreturn trampoline executable | Signal return ABI | One page, read-only |

Each exception is listed in `SECURITY.md` with its justification and is covered by a test
that proves nothing else in the same 4 MB region is reachable.

## 7. 64-bit notes

The 64-bit layout replaces this with: kernel in the higher half (`0xFFFF8000_00000000`
direct map), user space `0x00000000_00000000`–`0x00007FFF_FFFFFFFF`, non-canonical holes
guarded, `NX` for data, `SMEP`/`SMAP` enabled. The `vm_area` model carries over unchanged;
only the constants differ.
