# POSIX — what this system does and does not claim

This document exists because "POSIX compatible" is a claim that has to be paid
for. Earlier the answer was: the syscalls are shaped like POSIX, no POSIX header
existed, no POSIX program could be built, and nothing had ever called these
numbers by the names a POSIX program uses. Two of those are now fixed. The third
is fixed for the subset written down here and nothing more.

The short version, so nobody has to infer it:

> A program written against the POSIX headers in `c/include/` builds, runs and
> gets the semantics in the tables below. It is not certified, it is not the
> whole standard, and no POSIX test suite has been run.

## 1. What is there

`c/rt/posix.c` is the surface. Every function in it is a shim over a call the
kernel already had: translate the arguments, make the call, turn a negative
result into `-1` with `errno` set. `c/prog/posixtest.c` is the gate — it includes
no `<mv/…>` header at all, so it compiles only if the POSIX names are enough, and
it prints `POSIX_OK` only when every check below holds.

| Header | Contents |
|---|---|
| `unistd.h` | `read`, `write`, `close`, `lseek`, `dup`, `dup2`, `unlink`, `rmdir`, `access`, `chdir`, `isatty`, `fcntl`, `fork`, `execve`, `execl`, `getpid`, `getppid`, `getuid`, `geteuid`, `getgid`, `sleep`; `pid_t`, `uid_t`, `gid_t`, `ssize_t`, `off_t`; `STD*_FILENO`, `SEEK_*`, `R_OK`/`W_OK`/`X_OK`/`F_OK` |
| `fcntl.h` | `open`, `creat`; `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_ACCMODE`, `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NONBLOCK`; `F_*` commands |
| `sys/stat.h` | `stat`, `fstat`, `mkdir`, `chmod`; `struct stat`; `S_ISDIR`, `S_ISREG` |
| `sys/wait.h` | `wait`, `waitpid`; `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG` |
| `signal.h` | `signal`, `raise`, `kill`; `SIG*` numbers, `SIG_DFL`, `SIG_IGN`, `SIG_ERR` |
| `errno.h` | the `E*` numbers, `errno` as an lvalue, `__errno_location` |
| `ctype.h` | `isdigit`, `isalpha`, `isalnum`, `isspace`, `isupper`, `islower`, `isprint`, `iscntrl`, `isxdigit`, `ispunct`, `toupper`, `tolower` |
| `locale.h` | `setlocale`, `localeconv` — the C locale, and only the C locale |
| `limits.h` | `NAME_MAX` 252, `PATH_MAX` 512, `ARG_MAX` 512, `OPEN_MAX` 64, `INT_MAX`, `INT_MIN` |

Whatever is not in that list is not declared, which makes it a compile error
rather than a call that returns a plausible lie. `pipe`, `socket`, `mmap`,
`wait`-with-options, `getrlimit`, `umask`, `readdir`, `symlink`, `tcsetattr` and
the rest of the standard are absent.

## 2. What conforms

Verified by `posixtest` on every `make check-runtime` run:

| Behaviour | Why it holds |
|---|---|
| `open` of a name that is not there fails, `errno` = `ENOENT` | The VFS asks the mount, and the mount no longer creates names it was only asked to read |
| `open(O_CREAT\|O_TRUNC)` creates, or empties an existing file | Served by `SYS_FWRITE` with no data, then a second open: the kernel has no create flag, so the shim uses the create door it has |
| `O_APPEND` positions at the end before returning | A seek to `SEEK_END` on the returned descriptor |
| `read`/`write` return the count, `-1` with `errno` otherwise | `SYS_READ`/`SYS_WRITE`, which validate the buffer range before copying |
| `lseek` returns the new offset | `SYS_SEEK` |
| `close` twice: the second is `EBADF` | One descriptor table per task, and a closed slot is closed |
| `stat` reports size and file type; a missing name is `ENOENT` | `SYS_STAT` fills twelve bytes on both mounts — it used to be a stub that reported success for `/tmp` and wrote nothing |
| `fstat` on an open file | `SYS_FSTAT` |
| `execl`/`execve` reach a program with its arguments | `execl` builds the array; `SYS_EXECVE` flattens it for the loader |
| `mkdir`, `chmod`, `unlink`, `rmdir` | `SYS_MKDIR`, `SYS_CHMOD`, `SYS_DELETE` |
| `access(R_OK)` on a file that exists | `SYS_ACCESS`, which now asks the mount rather than assuming HBFS |
| `isatty` is false for a file, true for 0/1/2 | Descriptors are allocated from 3 up, so a file never takes the console's number |
| `dup`/`dup2` give a working descriptor for a file | They operate on the descriptor table a program was actually handed |
| `fork` returns the child's PID to the parent and 0 to the child | The child's PID is its slot, and PIDs start at 1 — PID 0 is the scheduler's |
| `waitpid` returns the child and `WEXITSTATUS` unpacks its status | `SYS_WAITPID`, with the status shifted the way POSIX defines |
| `getpid`, `getppid`, `getuid`, `geteuid`, `getgid` | Per-task state; the first task is PID 1 |
| `signal` + `kill` to self with `SIG_IGN` | `SYS_SIGACTION`, `SYS_KILL` |
| `setlocale(LC_ALL, "C")` succeeds; an absent locale returns NULL | There is one locale and it is the C locale |
| `toupper`, `isdigit`, `isspace` and the rest | Table-free ASCII classifiers in the runtime |

## 3. What does not conform, and why

Each row is a decision, not an oversight. A row that could be closed cheaply was
closed; the rest are here so a port knows what to expect.

| Gap | Consequence | Why it stands |
|---|---|---|
| `struct stat` is 12 bytes: size, type, modification time | No `st_mode`, `st_uid`, `st_gid`, `st_ino`, `st_nlink`, `st_dev` | The filesystem stores a mode word per entry but the VFS stat does not report it; the honest fix is a larger stat structure, not a filled-in lie |
| No permission bits in `stat`; `access` is the only check | A program cannot decide access from the stat it took | `SYS_ACCESS` exists and enforces; the stat structure has to grow first |
| `dup` copies the file position instead of sharing it | Two handles read independently | Sharing needs one open-file-description object behind both descriptors; today each descriptor carries its own offset |
| `O_EXCL` is ignored | `open(O_CREAT\|O_EXCL)` does not fail on an existing file | No create-and-fail atomically; the create path is a whole-file write |
| `pipe`, `socket`, `poll`, `select` are not in the POSIX surface | A port that needs them cannot build | The kernel has sockets and pipes, but through its own calls; wrapping them is a separate increment |
| No `fork` with threads; no `pthread_*` | A threaded POSIX program cannot build | Threads exist at the syscall level and their TLS is per task; `pthread` needs per-thread TLS first |
| `errno` is one variable per task, not per thread | A second thread shares it | The same TLS gap |
| No `getrlimit`/`setrlimit`, no `umask`, no `nice` | Limits cannot be queried or changed | Nothing in the kernel exposes them yet |
| One locale: `"C"` | No collation, no case folding outside ASCII, no message catalogues | Multi-byte collation is a data and library project |
| No `#!/bin/sh`; scripts are `.bat` | A POSIX shell script does not run | EXEC.md section 7 write-up; the loader's interpreter line is not implemented |
| No `dirent.h`, no `readdir` | Directory listing is `SYS_READDIR` only | The VFS has a directory iterator but no POSIX-shaped wrapper yet |
| `open` on a `/tmp` file with `O_WRONLY` creates it | A write-open without `O_CREAT` can create | tmpfs has no separate create call; documented rather than hidden |
| Slow writes are short writes at 4 KB | A tmpfs file holds one page | That mount is a scratch area, not a filesystem |
| No conformance evidence | Nothing here has run LTP or the Open POSIX Test Suite | The gate is `posixtest`, and it is this repository's own |

## 4. What would make the claim defensible

In the order the cost rises:

1. **A conformance run.** The Open POSIX Test Suite or a subset of LTP built
   against these headers, with the pass list committed. Until then,
   "compatible" is this document's word, not a test result.
2. **A bigger `struct stat`.** Mode, owner, inode and link count are in the
   filesystem already; the VFS structure and the twelve-byte contract
   (`docs/spec/ABI.md` section 9) are what stand between them and `stat`.
3. **One open-file description shared by `dup`.** So `dup`, `dup2` and `fcntl`
   share the offset the way the standard requires.
4. **`pipe` and `socket` wrappers**, since the kernel has both.
5. **`pthread` on top of per-thread TLS**, which needs the TLS index to be per
   thread rather than per task (EXEC.md section 6's open item).

## 5. The shape of the ABI, for the record

`int 0x80`, the call number in `EAX`, arguments in `EBX`, `ECX`, `EDX`, `ESI`,
`EDI`, result in `EAX`, every other register preserved. That is the Linux i386
convention exactly, which is why this surface could be written as thin shims and
why a Linux binary personality is a plausible later step — as a *personality*,
not as POSIX conformance. The numbers, though, are Mellivora's own
(`docs/syscalls.md`), and a Linux binary's numbers are not.
