# Security model (normative)

Status: **draft**. P1 defines isolation; P6 defines identity, capabilities and audit.

## 1. Threat model

In scope:

- A hostile **application** running with user privileges (a downloaded program, a game,
  a package with a malicious manifest).
- **Corrupted on-disk data** (a damaged or crafted filesystem image).
- **Malformed input** from the network, the GUI event queue and syscall arguments.
- A **buggy application** that crashes: it must not take the system with it.

Out of scope today: physical attacks, malicious firmware, side channels, and a hostile
*kernel* (no in-kernel sandboxing).

## 2. Isolation guarantees

| Resource | Guarantee | Enforced by |
|---|---|---|
| Kernel memory | Ring 3 cannot read or write it | Page tables: kernel RAM is supervisor-only (`docs/spec/VMM.md`) |
| Other processes' memory | Not mapped in the process's address space | Per-process page directories |
| Devices | Reachable only through driver syscalls | No user mapping of MMIO/PIO; `IOPL=0` |
| Filesystem | Access checked per open, per file mode | HBFS permissions + VFS checks |
| Shared buffers | Only buffers the kernel explicitly publishes | `paging_map_phys_user` on the VBE shadow buffer, IPC surfaces, shared memory |
| Faulting task | Killed alone; system keeps running | `fault_kill_current_task` |

### 2.1 Documented exceptions

| Exception | Justification | Bound |
|---|---|---|
| VGA text aperture (`0xA0000`–`0xC0000`) writable | Direct text output is a documented programming technique | 128 KB, affects only the display |
| Framebuffer shadow buffer | Programs render into it; `SYS_FRAMEBUF/4` presents it | The buffer's pages only |
| IPC surface pixels | The owning app draws its window | The surface's pages only |
| Shared memory | Explicitly shared between cooperating processes | The shm table's pages only |

Every exception has a regression test that also proves the *surrounding* pages remain
unreachable.

## 3. Identities and privileges

- A process has UID, GID, EUID, EGID, SID, groups.
- UID 0 is the system administrator; everything else is unprivileged.
- Capabilities replace "root can do anything" as the enforcement unit:
  `CAP_FS_RAW`, `CAP_NET_BIND_LOW`, `CAP_DEV_*`, `CAP_PROC_CONTROL`, `CAP_SYS_BOOT`.
- An app manifest declares the capabilities it needs; the launcher passes them at spawn
  and the kernel enforces them per syscall. **A manifest is a request, not a grant** —
  the user approves it on first run.

## 4. Filesystem security

- Unix-style 12-bit modes with owner/group/other; symlinks are not followed for
  privileged operations unless `CAP_FS_RAW`.
- ACLs (P4) extend modes for named users and groups.
- `noexec` and `nosuid` mount options.
- Every open records the credentials used, so a compromised process cannot re-open
  something it lost access to.

## 5. Kernel self-protection

- No writable+executable pages; `PROT_EXEC` on writable mappings requires `MAP_JIT`.
- Kernel text mapped read-only (`CR0.WP` on; already enforced for kernel writes).
- Guard pages at the bottom of every kernel stack; stack overflow panics loudly instead
  of corrupting the allocator.
- Allocator metadata behind magic numbers and canaries (P6).
- Syscall arguments validated with `user_range_ok` before any dereference; a bad pointer
  returns `EFAULT`, it never faults the kernel.

## 6. Audit

Security-relevant events are appended to a ring buffer with a monotonic sequence number:
identity changes, capability grants/denials, permission denials, raw device opens,
package installs. `dmesg`-style tooling reads it; P6 adds persistence.

## 7. Signals and crashes

- A user-mode fault kills the faulting task and reports `[fault] task killed, eip=…
  addr=…`. The desktop and the shell both survive.
- A supervisor fault is fatal by definition: panic, symbolised stack, restart. Silently
  continuing after a kernel fault is never acceptable.
- Crash data (EIP, address, error code, task name) is written to COM1 so a host-side
  harness can collect it.

## 8. Test obligations

`make check` fails if any of these regress:

1. `memprot_test` — ring 3 cannot write kernel memory (child faults; parent survives).
2. `sbrk_test` — heap growth works and returns valid, zeroed memory.
3. `vmtest` (P1) — `mmap`/`munmap`/`mprotect` semantics per `VMM.md`.
4. `permtest` (P6) — an unprivileged process is denied on every protected object.
5. Filesystem fuzzing — a corrupted image never panics the kernel.
