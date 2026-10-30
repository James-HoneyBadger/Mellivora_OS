/*
 * errno.h - the POSIX error numbers, and the lvalue a POSIX program expects
 *
 * POSIX wants `errno` to be an lvalue that a failed call sets and a program
 * reads. The kernel keeps errno per task (SYS_GETERRNO), but a task's copy is
 * not addressable from user mode, so the runtime keeps one here and every
 * wrapper in c/rt/posix.c copies the kernel's value into it on failure.
 *
 * One variable, not one per thread: this tree has threads, but its TLS is per
 * task (EXEC.md section 6), so a second thread shares this. That is the same
 * gap the TLS note records, not a separate one.
 */
#ifndef EERRNO_H
#define EERRNO_H

#define EPERM        1      /* Operation not permitted */
#define ENOENT       2      /* No such file or directory */
#define ESRCH        3      /* No such process */
#define EINTR        4      /* Interrupted system call */
#define EIO          5      /* I/O error */
#define ENXIO        6      /* No such device or address */
#define E2BIG        7      /* Argument list too long */
#define ENOEXEC      8      /* Exec format error */
#define EBADF        9      /* Bad file descriptor */
#define ECHILD      10      /* No child processes */
#define EAGAIN      11      /* Resource temporarily unavailable */
#define ENOMEM      12      /* Cannot allocate memory */
#define EACCES      13      /* Permission denied */
#define EFAULT      14      /* Bad address */
#define EEXIST      17      /* File exists */
#define EXDEV       18      /* Cross-device link */
#define ENODEV      19      /* No such device */
#define ENOTDIR     20      /* Not a directory */
#define EISDIR      21      /* Is a directory */
#define EINVAL      22      /* Invalid argument */
#define ENFILE      23      /* Too many open files in system */
#define EMFILE      24      /* Too many open files */
#define ENOTTY      25      /* Not a terminal */
#define EFBIG       27      /* File too large */
#define ENOSPC      28      /* No space left on device */
#define ESPIPE      29      /* Illegal seek */
#define EROFS       30      /* Read-only filesystem */
#define EPIPE       32      /* Broken pipe */
#define ERANGE      34      /* Result too large */
#define ENOSYS      38      /* Function not implemented */
#define ENOTSUP     95      /* Operation not supported */

/* errno is the lvalue POSIX promises, and it is a macro on purpose: a plain
 * `extern int errno` would be a second copy of the truth, and the macro is what
 * a program compiled against any other C library already expects. */
int *__errno_location(void);
#define errno (*__errno_location())

/* Called by the wrappers: a failed syscall leaves its reason in the kernel's
 * per-task errno, and this is where the two are reconciled. */
void __errno_from_kernel(void);

#endif /* EERRNO_H */
