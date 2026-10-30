/*
 * posix.c - the POSIX-callable surface over this kernel's syscalls
 *
 * The gap this closes is the one ABI.md left open: the syscalls exist, but
 * nothing could call them by the names a POSIX program uses. Each function
 * here is a thin shim - translate the arguments, call, turn a negative result
 * into -1 with errno set - and the ones that cannot be thin say why in the
 * header rather than pretending.
 *
 * Two things are deliberately not lies:
 *
 *   * `open()` has no create flag to pass, so O_CREAT is served here: a failed
 *     open of a missing name is followed by SYS_FWRITE with no data - which is
 *     the kernel's create-or-overwrite door - and then by a second open. The
 *     cost of doing it in two steps is that creating truncates an existing
 *     file, which is what O_TRUNC asked for anyway; the cost of not doing it is
 *     that a POSIX program could not write a file at all.
 *   * `struct stat` is twelve bytes because the VFS fills twelve. Fields POSIX
 *     has and this does not - mode, uid, inode - are absent, so code that uses
 *     them fails to compile.
 */
#include <stdarg.h>

#include <mv/types.h>
#include <mv/syscall.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int errno_storage;

int *__errno_location(void)
{
    return &errno_storage;
}

void __errno_from_kernel(void)
{
    i32 kernel_errno = mv_syscall(SYS_GETERRNO, 0, 0, 0, 0, 0);
    /* A kernel that reports nothing useful should not erase a good errno a
     * wrapper set itself; 0 means "no reason recorded". */
    if (kernel_errno > 0) {
        errno = kernel_errno;
    }
}

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */

/* SYS_FWRITE's file types (programs/syscalls.inc). Text is the only one a
 * POSIX open has any business making. */
#define MV_FTYPE_TEXT 1

static int posix_write_empty(const char *path)
{
    static const char empty = 0;

    /* Zero bytes with a valid pointer: the kernel's create-or-overwrite door
     * never reads the buffer, because it copies nothing. */
    return mv_syscall(SYS_FWRITE, (u32)path, (u32)&empty, 0, MV_FTYPE_TEXT, 0);
}

int open(const char *path, int oflag, ...)
{
    i32 fd;

    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }

    fd = mv_syscall(SYS_OPEN, (u32)path, (u32)oflag, 0, 0, 0);

    if ((oflag & (O_CREAT | O_TRUNC)) != 0) {
        int missing = (fd < 0);

        if (missing && (oflag & O_CREAT) == 0) {
            errno = ENOENT;
            return -1;
        }
        if (!missing && (oflag & O_TRUNC) != 0) {
            mv_syscall(SYS_CLOSE, (u32)fd, 0, 0, 0, 0);
            fd = -1;
        }
        if (fd < 0) {
            if (posix_write_empty(path) < 0) {
                errno = EACCES;
                return -1;
            }
            fd = mv_syscall(SYS_OPEN, (u32)path,
                            (u32)(oflag & ~(O_CREAT | O_TRUNC)), 0, 0, 0);
        }
    }

    if (fd < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = ENOENT;
        }
        return -1;
    }

    if ((oflag & O_APPEND) != 0) {
        /* Position at the end now. This is not atomic against another writer
         * the way a kernel-side O_APPEND would be; nothing here shares a file
         * between tasks concurrently yet. */
        mv_syscall(SYS_SEEK, (u32)fd, 0, SEEK_END, 0, 0);
    }

    return fd;
}

int creat(const char *path, unsigned mode)
{
    (void)mode;                     /* the filesystem sets the mode itself */
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

ssize_t read(int fd, void *buf, unsigned count)
{
    i32 n = mv_syscall(SYS_READ, (u32)fd, (u32)buf, count, 0, 0);
    if (n < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EBADF;
        }
        return -1;
    }
    return n;
}

ssize_t write(int fd, const void *buf, unsigned count)
{
    i32 n = mv_syscall(SYS_WRITE, (u32)fd, (u32)buf, count, 0, 0);
    if (n < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EBADF;
        }
        return -1;
    }
    return n;
}

int close(int fd)
{
    i32 r = mv_syscall(SYS_CLOSE, (u32)fd, 0, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EBADF;
        }
        return -1;
    }
    return 0;
}

off_t lseek(int fd, off_t offset, int whence)
{
    i32 r = mv_syscall(SYS_SEEK, (u32)fd, (u32)offset, (u32)whence, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EINVAL;
        }
        return -1;
    }
    return r;
}

int dup(int fd)
{
    i32 r = mv_syscall(SYS_DUP, (u32)fd, 0, 0, 0, 0);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return r;
}

int dup2(int fd, int fd2)
{
    i32 r = mv_syscall(SYS_DUP2, (u32)fd, (u32)fd2, 0, 0, 0);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return r;
}

int fcntl(int fd, int cmd, int arg)
{
    i32 r = mv_syscall(SYS_FCNTL, (u32)fd, (u32)cmd, (u32)arg, 0, 0);
    if (r < 0) {
        errno = EINVAL;
        return -1;
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Files and directories                                               */
/* ------------------------------------------------------------------ */

int unlink(const char *path)
{
    i32 r = mv_syscall(SYS_DELETE, (u32)path, 0, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = ENOENT;
        }
        return -1;
    }
    return 0;
}

int rmdir(const char *path)
{
    /* The filesystem removes a directory with its contents refused; the
     * syscall is the same one unlink uses. */
    return unlink(path);
}

int access(const char *path, int mode)
{
    i32 r = mv_syscall(SYS_ACCESS, (u32)path, (u32)mode, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EACCES;
        }
        return -1;
    }
    return 0;
}

int chdir(const char *path)
{
    i32 r = mv_syscall(SYS_CHDIR, (u32)path, 0, 0, 0, 0);
    if (r < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int isatty(int fd)
{
    return mv_syscall(SYS_ISATTY, (u32)fd, 0, 0, 0, 0) == 1;
}

int stat(const char *path, struct stat *buf)
{
    i32 r = mv_syscall(SYS_STAT, (u32)path, (u32)buf, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = ENOENT;
        }
        return -1;
    }
    return 0;
}

int fstat(int fd, struct stat *buf)
{
    i32 r = mv_syscall(SYS_FSTAT, (u32)fd, (u32)buf, 0, 0, 0);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int mkdir(const char *path, unsigned mode)
{
    (void)mode;
    i32 r = mv_syscall(SYS_MKDIR, (u32)path, 0, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EEXIST;
        }
        return -1;
    }
    return 0;
}

int chmod(const char *path, unsigned mode)
{
    i32 r = mv_syscall(SYS_CHMOD, (u32)path, mode, 0, 0, 0);
    if (r < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Processes                                                           */
/* ------------------------------------------------------------------ */

pid_t fork(void)
{
    i32 r = mv_syscall(SYS_FORK, 0, 0, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = EAGAIN;
        }
        return -1;
    }
    return r;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    /* The kernel takes a real argv array (it flattens it for the loader), and
     * accepts envp for shape: the environment is global in this wave. */
    i32 r = mv_syscall(SYS_EXECVE, (u32)path, (u32)argv, (u32)envp, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = ENOEXEC;
        }
        return -1;
    }
    return 0;                       /* only reached if exec failed */
}

int execl(const char *path, const char *arg, ...)
{
    char *argv[16];
    va_list ap;
    unsigned n = 0;

    /* POSIX's execl is execv with the argument list written out. The kernel
     * wants an array, so the array is built here - sixteen entries is more than
     * a command line that arrives flattened into 512 bytes can express. */
    if (arg != NULL) {
        argv[n++] = (char *)arg;
        va_start(ap, arg);
        while (n < 15) {
            char *next = va_arg(ap, char *);
            if (next == NULL) {
                break;
            }
            argv[n++] = next;
        }
        va_end(ap);
    }
    argv[n] = NULL;

    return execve(path, argv, NULL);
}

pid_t getpid(void)
{
    return mv_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
}

pid_t getppid(void)
{
    return mv_syscall(SYS_GETPPID, 0, 0, 0, 0, 0);
}

uid_t getuid(void)
{
    return (uid_t)mv_syscall(SYS_GETUID, 0, 0, 0, 0, 0);
}

uid_t geteuid(void)
{
    return (uid_t)mv_syscall(SYS_GETEUID, 0, 0, 0, 0, 0);
}

gid_t getgid(void)
{
    return (gid_t)mv_syscall(SYS_GETGID, 0, 0, 0, 0, 0);
}

unsigned sleep(unsigned seconds)
{
    mv_syscall(SYS_SLEEP, (u32)(seconds * 100), 0, 0, 0, 0);
    return 0;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)options;                  /* WNOHANG would need a polling wait */
    i32 r = mv_syscall(SYS_WAITPID, (u32)pid, 0, 0, 0, 0);
    if (r < 0) {
        errno = ECHILD;
        return -1;
    }
    if (status != NULL) {
        *status = r << 8;           /* WEXITSTATUS unpacks this */
    }
    return pid;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

/* ------------------------------------------------------------------ */
/* Signals                                                             */
/* ------------------------------------------------------------------ */

void (*signal(int sig, void (*func)(int)))(int)
{
    i32 r = mv_syscall(SYS_SIGACTION, (u32)sig, (u32)func, 0, 0, 0);
    if (r < 0) {
        errno = EINVAL;
        return SIG_ERR;
    }
    return func;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

int kill(pid_t pid, int sig)
{
    i32 r = mv_syscall(SYS_KILL, (u32)pid, (u32)sig, 0, 0, 0);
    if (r < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = ESRCH;
        }
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* C locale                                                            */
/* ------------------------------------------------------------------ */

char *setlocale(int category, const char *locale)
{
    static char c_locale[] = "C";

    (void)category;
    if (locale == NULL) {
        return c_locale;            /* a query */
    }
    /* "C", "POSIX" and the empty string - which POSIX defines as the native
     * environment, and the native environment here is the C locale. */
    if ((locale[0] == 'C' && locale[1] == '\0') ||
        (locale[0] == '\0') ||
        (locale[0] == 'P' && locale[1] == 'O' && locale[2] == 'S' &&
         locale[3] == 'I' && locale[4] == 'X' && locale[5] == '\0')) {
        return c_locale;
    }
    errno = EINVAL;
    return NULL;                    /* a locale this system does not have */
}

struct lconv *localeconv(void)
{
    static char dot[] = ".";
    static char empty[] = "";
    static struct lconv conv = {
        dot, empty, empty, empty
    };
    return &conv;
}

/* ------------------------------------------------------------------ */
/* Character classes                                                   */
/* ------------------------------------------------------------------ */

int isdigit(int c) { return c >= '0' && c <= '9'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isalpha(int c) { return islower(c) || isupper(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
}
int isprint(int c) { return c >= 0x20 && c <= 0x7E; }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int ispunct(int c) { return isprint(c) && !isalnum(c) && !isspace(c); }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }
