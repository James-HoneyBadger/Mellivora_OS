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
| `0x00300000`–`0x00400000` | Program window: code, data, BSS, heap (grows up), user stack (grows down from `PROGRAM_EXIT_ADDR - 4`) | read/write/execute |
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

Stack growth: a fault within 64 KB below `vm_area.start` of the stack area extends the
stack instead of failing.

## 5. Isolation invariants (tested)

1. A ring 3 write to `0x00100000` (kernel image) faults and cannot be observed to
   succeed.
2. A ring 3 read of another task's program window faults.
3. A ring 3 write to a read-only file mapping faults unless it is `MAP_PRIVATE`.
4. After `munmap`, the range faults.
5. No stale bytes: a fresh anonymous page reads as zero.
6. Kernel code may still write VGA text (`0xB8000`) and its own memory without faults.

Each invariant has a regression test; `tests/test_isolation.py` drives them over QEMU and
`make check` fails if any regresses.

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
