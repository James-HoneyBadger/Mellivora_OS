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
 *   * `open()` passes O_CREAT, O_TRUNC and O_APPEND straight to the kernel,
 *     which settles them before it hands back a descriptor. It did not always:
 *     the flags were ignored and creating was a SYS_FWRITE with no data, which
 *     only understood a bare name.
 *   * `struct stat` is twelve bytes because the VFS fills twelve. Fields POSIX
 *     has and this does not - mode, uid, inode - are absent, so code that uses
 *     them fails to compile.
 */
#include <stdarg.h>

#include <mv/types.h>
#include <mv/syscall.h>

#include <stdio.h>
#include <string.h>

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

int open(const char *path, int oflag, ...)
{
    i32 fd;

    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }

    /* O_CREAT, O_TRUNC and O_APPEND are the kernel's now: SYS_OPEN settles them
     * before it hands back a descriptor, so this is the whole of the call. The
     * two-step version that made the file with SYS_FWRITE first only worked for
     * names in the current directory, which is exactly what a path is not. */
    fd = mv_syscall(SYS_OPEN, (u32)path, (u32)oflag, 0, 0, 0);
    if (fd < 0) {
        __errno_from_kernel();
        if (errno == 0) {
            errno = ENOENT;
        }
        return -1;
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

char *getcwd(char *buf, unsigned size)
{
    static char kernel_path[256];

    if (buf == NULL) {
        errno = EFAULT;
        return NULL;
    }
    if (mv_syscall(SYS_GETCWD, (u32)kernel_path, 0, 0, 0, 0) < 0) {
        errno = ENOENT;
        return NULL;
    }
    /* The kernel builds into its own 256-byte buffer and copies the exact
     * range, so the length is known before the caller's buffer is touched -
     * which is the only way to answer ERANGE without overflowing it. */
    {
        unsigned len = 0;
        while (kernel_path[len] != '\0' && len < sizeof kernel_path) {
            len++;
        }
        if (len + 1 > size) {
            errno = ERANGE;
            return NULL;
        }
        {
            unsigned i;
            for (i = 0; i <= len; i++) {
                buf[i] = kernel_path[i];
            }
        }
    }
    return buf;
}

int pipe(int fds[2])
{
    if (fds == NULL) {
        errno = EFAULT;
        return -1;
    }
    /* The kernel refuses this, and refuses it deliberately: SYS_PIPE2 used to
     * hand out two descriptors that no VFS backend read or wrote. ENOSYS is
     * what POSIX means by "not implemented", and docs/POSIX.md says so. */
    if (mv_syscall(SYS_PIPE2, (u32)fds, 0, 0, 0, 0) < 0) {
        errno = ENOSYS;
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

/* ------------------------------------------------------------------ */
/* getopt                                                              */
/* ------------------------------------------------------------------ */

char *optarg;
int   optind = 1;
int   opterr = 1;
int   optopt;

int getopt(int argc, char *const argv[], const char *optstring)
{
    static int scanned;         /* index being scanned in the current word */
    char **v = (char **)argv;   /* permutation writes through this: the standard
                                 * declares argv const for the callee's benefit,
                                 * but GNU's own getopt reorders it too */
    int stop_at_operand = 0;

    optarg = 0;
    if (optstring[0] == '+') {
        stop_at_operand = 1;    /* POSIXLY_CORRECT: -a f -b stops at "f" */
        optstring++;
    }
    if (optstring[0] == '-') {
        optstring++;            /* "-" mode is not implemented; see POSIX.md */
    }

    for (;;) {
        if (scanned == 0) {
            /* Find the next word that starts an option and bring it to
             * optind, which is what lets "sort file -r" mean the same as
             * "sort -r file". */
            int i;

            while (optind < argc) {
                const char *w = v[optind];

                if (w[0] == '-' && w[1] != '\0') {
                    if (w[1] == '-' && w[2] == '\0') {
                        optind++;   /* "--" ends the options */
                        return -1;
                    }
                    break;
                }
                if (stop_at_operand)
                    return -1;
                for (i = optind + 1; i < argc; i++) {
                    if (v[i][0] == '-' && v[i][1] != '\0')
                        break;
                }
                if (i >= argc)
                    return -1;
                {
                    char *word = v[i];

                    while (i > optind) {
                        v[i] = v[i - 1];
                        i--;
                    }
                    v[optind] = word;
                }
                break;
            }
            if (optind >= argc)
                return -1;
            scanned = 1;
        }

        {
            char option = v[optind][scanned];
            const char *spec = strchr(optstring, option);
            int last_char = (v[optind][scanned + 1] == '\0');

            if (last_char) {
                optind++;
                scanned = 0;
            } else {
                scanned++;
            }

            if (spec == 0 || option == ':') {
                optopt = (u8)option;
                if (opterr && option != ':') {
                    fprintf(stderr, "%s: invalid option -- '%c'\n",
                            argv[0], option);
                }
                return '?';
            }
            if (spec[1] == ':') {
                if (!last_char) {
                    /* -ofile: the rest of the word is the argument */
                    optarg = &v[optind - 1][scanned];
                    scanned = 0;
                } else if (optind < argc) {
                    optarg = v[optind++];
                } else {
                    if (opterr) {
                        fprintf(stderr,
                                "%s: option requires an argument -- '%c'\n",
                                argv[0], option);
                    }
                    return (optstring[0] == ':') ? ':' : '?';
                }
            }
            return (u8)option;
        }
    }
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
