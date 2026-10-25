# Security model (normative)

Status: **draft**. P1 defines isolation; P6 defines identity, capabilities and audit.

## File permissions

HBFS stores a mode word (owner/group/world read, write, execute) and an owner
uid/gid per directory entry. The mode is enforced, not advisory:

| Operation | Rule |
|---|---|
| `open` for read | owner/group/world read bit, by the caller's effective uid/gid |
| `open` for write | owner/group/world write bit |
| `SYS_FWRITE` over an existing file | write bit on the file |
| `unlink` | write bit on the file |
| create a file or subdirectory | write bit on the **directory** |
| `rename` | write bit on the file and on the directory |
| `truncate` | write bit on the file |
| `rmdir` | write bit on the directory |
| `chmod` | root, or the file's owner |
| `chown` | root only |
| anything, as uid 0 | allowed |

Access is checked in `fd_open`, which every open funnels through, and in the
write paths that do not open a descriptor first. The shell and other kernel
contexts have no task control block and are treated as root.

`setuid`/`setgid` move the effective ID as well as the real one, because the
checks read the effective ID; a process that dropped privileges used to keep
passing them as root.

A name is part of the directory that holds it, so adding one is a write to the
directory: `hbfs_create_file` and `hbfs_mkdir` check the directory's write bit
before the transaction opens, and a refusal journals nothing. The directory's
own mode and owner are found in the entry that names it in its parent, which
the directory stack records; the root directory has no such entry and uses a
stand-in - root-owned, `0755`. A mode of 0 is treated as a legacy entry:
readable by its owner, not writable.

**Not yet enforced:** directory *execute* (search) permission, and the sticky
bit on shared directories. Ownership of a directory is not required to rename
within it beyond the write bits above.

## Handle rights

File permissions decide whether an open succeeds. They do not decide what the
resulting handle may do. Every virtual descriptor carries a right word, stamped
at open time from the flags the open asked for, and the read and write paths
check it before any backend runs:

| Opened with | May read | May write |
|---|---|---|
| `O_RDONLY` (0) | yes | no — even as root, even if the mode allows it |
| `O_WRONLY` (1) | no | yes |
| `O_RDWR` (2) | yes | yes |

The bits are the same numbers as the kernel's internal `FD_FLAG_READ` and
`FD_FLAG_WRITE`, which is exactly why the mapping happens once, at the `SYS_OPEN`
boundary: `O_RDONLY` is 0 and `O_WRONLY` is 1, so a program that passes the
internal bits is asking for the wrong thing. Passing `1` ("read", if you are
thinking in `FD_FLAG_*`) opens write-only.

Sockets are created with both rights, and the ends of a pipe get the right that
matches their end. Closing a handle takes its rights with it.

This is the first piece of the object model in `docs/ROADMAP.md`: a handle is
not a bare number, it is a number bound to a right. Generically typed objects,
per-object reference counts and wait-on-object arrive in the Warren.

The rule is exercised by `programs/handle_test.asm`, which runs as root: every
file operation it makes is permitted by the file's mode, so a refusal can only
come from the handle.

## Signals

`kill` reaches only a task the sender may touch: itself, another task of the
same effective user, or anything at all when the sender is root. Every match is
checked before anything is delivered, so a process-group signal that includes a
task the sender does not own is refused whole rather than half-delivered. The
shell and other kernel contexts have no task control block and are root.

The rule is exercised by `programs/hostile_test.asm`: as uid 1000 it must fail
to kill a uid-2000 task, and must succeed killing a child of its own.

## Conformance gate

`programs/hostile_test.asm` is the executable form of this document's claims.
It runs as uid 1000 and requires eleven outcomes: five refusals on a file owned
by somebody else (read, write, `access`, remove, replace-by-write), two on its
metadata (`chmod`, `chown`), two on kernel addresses passed where user pointers
belong (`open`, `write`), and two controls that must still succeed (reading a
world-readable file, and being refused its write). It prints `HOSTILE_OK` only
when every one holds, and it is a `make check-runtime` case.

The gate has been seen to say no: with the access check in `fd_open` temporarily
forced to succeed, the program fails on its first attempt.

`programs/handle_test.asm` is the gate for the handle-rights section, and
`programs/dirperm_test.asm` for the directory rules: as uid 2000, owning both
directories, it must be refused a create in one whose write bit is clear, and
allowed the same call in the other, and it must be refused renaming and
truncating a file of uid 1000's that sits in its own writable directory. Forcing
`hbfs_cwd_access_current` to allow makes it print `DIRPERM_FAIL`. Both are
`make check-runtime` cases.


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
