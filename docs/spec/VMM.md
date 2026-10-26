# Virtual memory (normative)

**Normative from:** M1. **Governs:** address spaces, regions, page tables, the
physical frame database, fault rules, sections and the kernel's own memory.

Today's tree is a flat 32-bit kernel that identity-maps RAM and hands a program a
1 MB window. That was the right shape for a small system and the wrong shape for
this one: it caps a program at 1 MB, caps RAM at what a bitmap can describe, and
makes every kernel pointer a physical address. This document replaces it. The
replacement is not a tweak — every address in the system changes, which is why
it is M1's first commit and why the old layout is kept only as an **as-built**
appendix.

## 1. Principles

1. **A process sees 3 GiB of its own.** Address space is a resource like any
   other: reserved, committed, accounted, and reclaimed
   ([S2](README.md#decisions-this-set-records)).
2. **The kernel is not identity-mapped.** A kernel pointer is not a physical
   address. Physical memory is reached through the frame database and a bounded
   mapping window, so the kernel can describe 64 GiB of RAM while running under
   a 32-bit page table.
3. **Every mapping is declared** — by a region, with a protection and an owner.
   A page that no region covers is not user-accessible, and touching it is a
   fault, not a surprise.
4. **Fail closed.** A missing protection bit denies. A protection that cannot be
   honoured refuses the mapping, rather than granting something weaker.
5. **No writable-and-executable page, ever** — in either ring.
6. **Accounting is exact.** Every committed page is charged to a process and to
   its job, and both counters return to their starting value when the process
   exits. A leak is a test failure, not a slow drift ([S7](README.md#decisions-this-set-records)).

## 2. Address space

32-bit, PAE, split 3 GiB / 1 GiB ([S5](README.md#decisions-this-set-records)).

### 2.1 User space

The range is `0x00000000`–`0xBFFFFFFF`.

| Range | Contents | Notes |
|---|---|---|
| `0x00000000`–`0x0000FFFF` | Reserved | Null and near-null: any access faults, so a null dereference is always caught |
| `0x00010000`–`0x7FFFFFFF` | Private per process | Image, heaps, thread stacks, private sections |
| `0x80000000`–`0xBFFFFFFF` | Shared | Shared sections, mapped files, and the image bases of shared libraries |
| `0xBFFF0000`–`0xBFFFFFFF` | Reserved | The top 64 KB of user space is never mapped, so a wild upward pointer cannot wrap into the kernel |

Allocation strategy inside the private zone:

| Object | Placement |
|---|---|
| Main image | Random base in `0x00010000`–`0x40000000`, 64 KB aligned (ASLR) |
| Shared libraries | Random base in `0x80000000`–`0xA0000000` (ASLR) |
| Main thread stack | 1 MiB reserved, 64 KiB committed, top randomised in `0x60000000`–`0x7FFF0000` |
| Additional thread stacks | 1 MiB reserved each, from the private zone, top randomised per thread |
| Heap | Committed regions on demand from the private zone, never adjacent to a stack |
| Sections | Anywhere in the private zone, honouring the caller's alignment up to 64 KiB |

### 2.2 Kernel space

The range is `0xC0000000`–`0xFFFFFFFF`.

| Range | Contents | Size |
|---|---|---|
| `0xC0000000`–`0xC0FFFFFF` | Kernel image: text, rodata, data, the C module slot ([ABI.md](ABI.md#10-kernel-module-abi)) | 16 MiB |
| `0xC1000000`–`0xC8FFFFFF` | Kernel stacks: two pages per task plus a guard, from a pool | 128 MiB |
| `0xC9000000`–`0xC9FFFFFF` | Per-CPU blocks, interrupt stacks, the GDT/TSS/IDT per processor | 16 MiB |
| `0xCA000000`–`0xCBFFFFFF` | Paged pool (mapped on demand, can be trimmed) | 32 MiB |
| `0xCC000000`–`0xCFFFFFFF` | Nonpaged pool: objects, IRPs, driver data | 64 MiB |
| `0xD0000000`–`0xDFFFFFFF` | **Physical map window** — see §3.3 | 256 MiB |
| `0xE0000000`–`0xEFFFFFFF` | Section and cache views, mapped large regions | 256 MiB |
| `0xF0000000`–`0xF0FFFFFF` | MMIO: local APIC, I/O APIC, HPET, PCIe ECAM | 16 MiB |
| `0xF1000000`–`0xF3FFFFFF` | Framebuffer window | 48 MiB |
| `0xFF000000`–`0xFF3FFFFF` | Page-table self-map and the PFN database's active windows | 4 MiB |
| `0xFFF00000`–`0xFFFFFFFF` | The system page-table page and the per-CPU TSS | 1 MiB |

Kernel pages are `PRESENT | WRITE` with **no** `USER` bit. Kernel text and
rodata are read-only even to the kernel (`CR0.WP=1`). The physical map window,
the framebuffer window and the cache views are the only kernel ranges with
special rules, and each is listed in [SECURITY.md](SECURITY.md#8-kernel-self-protection).

## 3. Page tables and physical memory

### 3.1 Three levels, PAE

PAE is the ceiling-raise ([S5](README.md#decisions-this-set-records)):

| Level | Entry | Covers |
|---|---|---|
| PDPT | 4 entries, one per 1 GiB | Selected by `CR3` |
| PD | 512 entries, one per 2 MiB | A `PS` bit here maps a 2 MiB large page |
| PT | 512 entries, one per 4 KiB | The ordinary mapping |

- **Large pages**: 2 MiB, used for the kernel image, the physical map window,
  the framebuffer and sections of 2 MiB or more. A large page is never used for
  a range that needs differing protections inside it.
- **NX** is honoured per entry. A page is executable only if its region says so.
- **Global pages** are not used: a per-process `CR3` is the model, and a global
  bit would be a stale-mapping bug waiting for an SMP machine.
- **TLB shootdown** is an IPI to every other processor with the affected
  address range; a mapping change does not complete until every processor has
  acknowledged. On uniprocessor, the local TLB is invalidated in place.
- A page directory and each page table are **pools**, not per-allocation pages:
  allocating a page table at a page-fault time that itself needs a page table is
  how a kernel deadlocks on itself. Pools are replenished by a low-priority
  thread and by a reserve for the fault path.

### 3.2 The frame database

One **PFN entry** per physical frame, 16 bytes, allocated at boot from the
largest contiguous run:

| Field | Meaning |
|---|---|
| `refs` | Mappings and kernel references |
| `share` | Copy-on-write share count |
| `state` | Free, standby, active, modified, bad |
| `flags` | Zeroed, dirty, large, DMA-capable, pinned |
| `original_prot` | Protection to restore after a COW break |
| `owner` | Process or section that is charged, 0 for kernel |

At 64 GiB the database is 256 MiB for 16.7 M frames: allocated from the top of
RAM at boot, outside the pool, and never moved. It replaces the bitmap and the
buddy tags, which is why the "used" and "free" numbers the shell prints become
exact rather than estimated.

### 3.3 The physical map window

The kernel cannot address 64 GiB of physical memory with 1 GiB of virtual space,
so it does not try:

```text
mv_phys_map(pa, length)   ->  a VA in 0xD0000000..0xDFFFFFFF
mv_phys_unmap(va)         ->  releases the window
```

- A window is at most **2 MiB** and 4 KiB aligned; up to 128 windows may be live
  at once in total.
- A window may not span a 2 MiB boundary, so a window is a whole number of
  large pages, not a fragmentation source.
- A window is a **short-lived** reference. Anything the kernel keeps must be a
  PFN plus an offset, not a pointer into the window.
- A window is never held across a blocking call, a spinlock release, or a
  context switch. This is enforced: a debug build records the holding task and
  panics if a window outlives its slice.
- Small fixed structures (the page directories, the PFN database, the kernel
  stack of the running task) are the exception: they live in the permanently
  mapped ranges of §2.2.

## 4. Regions

A **region** is the unit of address-space accounting:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `start`, page aligned |
| 4 | 4 | `end`, page aligned, exclusive |
| 8 | 4 | `prot`, `PROT_READ|WRITE|EXEC|GUARD` |
| 12 | 4 | `type`, image / mapped / private / stack / shared |
| 16 | 4 | `flags`, reserve / commit / no-cache / large / write-combine |
| 20 | 4 | `section`, a handle or 0 for anonymous |
| 24 | 8 | `file_offset`, `u64` ([ABI.md](ABI.md#3-types)) |
| 32 | 4 | `charge`, committed pages charged here |
| 36 | 4 | `owner_tag`, for leak attribution |

Regions are kept in a red-black tree keyed by `start` and never overlap. A
process may hold **65 536** regions; the tree depth is bounded by that, not by
an array ([S1](README.md#decisions-this-set-records)).

### 4.1 Reserve, then commit

Two-stage, because it is what makes a large allocation fail at the right moment:

| Step | Meaning | Failure |
|---|---|---|
| **Reserve** | Take address space only; no RAM, no charge | `MV_NO_MEMORY` when the address space is exhausted |
| **Commit** | Back a page with RAM (or a file) and charge it | `MV_NO_MEMORY` when the commit limit is reached |

The commit limit is `physical RAM + pagefile − kernel reserve`. A guard page is
reserved and never committed, so an overflow faults into a growth decision
instead of into another region.

### 4.2 Protections

| Protection | Read | Write | Execute | Notes |
|---|---|---|---|---|
| `PROT_NONE` | — | — | — | Reserved or poisoned |
| `PROT_READ` | ✓ | — | — | |
| `PROT_RW` | ✓ | ✓ | — | The default |
| `PROT_RX` | ✓ | — | ✓ | The only executable form |
| `PROT_RWX` | refused | | | `MAP_JIT` grants a *pair* of pages, not a writable executable page |

`mprotect` may narrow or widen within the region's rules; widening to `EXEC` on
a range that was written requires `MAP_JIT`, and the JIT contract is that the
code page and the writable page are distinct.

## 5. Fault semantics

One handler, one decision tree:

```text
1. The address is in kernel space?
   a. Inside a kernel range -> panic (a kernel bug, not user input)
   b. Outside every kernel range -> panic, with the range table printed
2. Find the region containing the address.
   a. None -> if the address is within 64 KiB below a stack's guard, grow the
      stack (up to its reservation) and retry; otherwise kill the task with
      MV_ACCESS_VIOLATION.
   b. Region is reserved but not committed -> demand-zero page, charge it.
   c. Region is a file view -> read the page through the cache, map it, retry
      once (a second fault on the same page is fatal, because the cache is
      wrong and retrying will not help).
   d. The page is copy-on-write and the access is a write -> allocate, copy,
      add to the COW share count, remap writable.
   e. The access violates `prot` -> kill the task.
3. Charge the frame to the process and its job; on `MV_NO_MEMORY`, kill the
   task rather than returning a page that is not there.
```

Rules that are not negotiable:

- **No fault while holding a spinlock.** A fault inside a non-preemptible
  critical section panics, naming the lock. The fault path itself allocates from
  a reserve so it cannot recurse into the allocator ([SCHED.md](SCHED.md)).
- **Nested faults** are bounded: one level of recursion for a user-copy, then a
  panic. A kernel that faults copying to a user address has a bug, and the
  second fault would hide it.
- **Fault accounting** is per task, and a task that faults in a loop is stopped
  rather than allowed to burn the machine.

## 6. Sections

A **section** is a shareable run of pages, and the only way two address spaces
see the same memory ([OBJECTS.md](OBJECTS.md#3-types)):

| Kind | Backing | Charge |
|---|---|---|
| Pagefile section | The pagefile / swap area | Committed pages |
| File section | A file's data ([STORAGE.md](STORAGE.md)) | Cache pages |
| Image section | An executable image | Cache pages, executable |
| Physical section | A device's memory, given by a driver | Not charged; the driver owns it |

- A view may be mapped read-only, read/write, execute-only or execute-read;
  `SECTION_MAP_WRITE` on the handle is required to ask for a write view.
- A view's size and offset are page aligned, up to 2^47 bytes of section.
- The page cache is the only path from a file to a page. A section does not
  read the volume itself, which is what makes `cat`, `mmap` and the loader see
  the same bytes ([STORAGE.md](STORAGE.md#6-the-cache)).
- A section that is mapped writable and executable at once is refused.

## 7. The two allocators, and why there are two

| Allocator | Serves | Backing | Free-time |
|---|---|---|---|
| **Frame allocator** | Whole 4 KiB pages, large pages | PFN database, buddy lists per zone | Any context; lock-free per zone with a spinlock |
| **Pool / slab** | Kernel objects, buffers, IRPs | Frames, mapped in the pool ranges | Nonpaged: any context. Paged: not from a DPC or a spinlock |

Zones: `DMA` (`< 16 MiB`, for devices that need it), `NORMAL` (everything else).
High memory is not a zone; it is the same pool reached through the window.

Every allocation is tagged with its owner for `pool`/`frames` accounting, so a
leak is attributed to a subsystem instead of appearing as "memory disappeared"
([RELIABILITY.md](RELIABILITY.md#8-tracing)).

## 8. Invariants and conformance

M1 does not close until all of these hold, and `make check` keeps them holding:

1. `memprot_test` — ring 3 cannot read, write or execute kernel memory, and the
   child faults while the parent survives.
2. `vmtest` — reserve, commit, `mprotect`, `munmap` of partial regions, and the
   region tree staying consistent across 10 000 random operations.
3. `vmtest2` — demand-zero pages are zero, and a partial unmap frees exactly the
   committed pages it names.
4. `cow_test` — a fork sees its own copy after a write, the parent is unchanged,
   and the share count returns to 1.
5. `vmtest3` / `vmtest5` — a file-backed page and a shared section read the same
   bytes as `read`, including through the cache's writeback.
6. `highmem` cases — the same tests with 4 GiB of physical RAM, so the PFN
   database, the window and a 32-bit physical address are exercised.
7. **Accounting**: after every suite, the commit charge per process, the pool
   charge per tag and the free frame count return to their pre-test values.
8. **No window leaks**: the number of live physical-map windows is 0 between
   tests and never exceeds 128 during one.

## 9. As built (the gap)

| Contract | Tree today |
|---|---|
| 3 GiB / 1 GiB split | Flat identity map below 8 MB, 1 MB program window, demand zone under 512 MB |
| PAE, 64 GiB | 2-level tables, 4 GiB at most, 128 MB tested |
| PFN database | Bitmap plus buddy order lists |
| Physical map window | RAM is identity-mapped, so a kernel pointer is a physical address |
| Regions in a tree | A linked list of `vm_area` per task |
| Reserve/commit | `sbrk` and `mmap` commit immediately |
| Sections | Shared-memory table keyed by name |
| 2 MiB large pages | None |

The `vm_area` list, the fault tree's demand-zero/COW/file paths and the
isolation invariants are real and tested; the address space they operate in is
what changes.
