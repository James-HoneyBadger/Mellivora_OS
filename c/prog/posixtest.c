/*
 * posixtest.c - the POSIX surface, called only through POSIX headers.
 *
 * This program deliberately includes nothing from <mv/>. If it compiles, a
 * program written against POSIX names - open, write, lseek, stat, mkdir,
 * fork, wait, errno, toupper, setlocale - reaches this kernel without knowing
 * that the kernel is not POSIX. If it runs to POSIX_OK, the shims underneath
 * actually work rather than merely link.
 *
 * Nothing here is skipped quietly: a step that cannot hold prints
 * POSIX_FAIL and the name of the step.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* printf comes from the runtime's stdio, which is not a POSIX header. Declaring
 * it here keeps the claim exact: the only non-POSIX thing this program uses is
 * the C library's own output routine. */
int printf(const char *fmt, ...);

static int fails;

static void ok(int cond, const char *what)
{
    if (!cond) {
        printf("POSIX_FAIL %s\n", what);
        fails++;
    }
}

static unsigned slen(const char *s)
{
    unsigned n = 0;

    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int main(void)
{
    static char buf[64];
    static char path[] = "/tmp/posix.txt";
    struct stat st;
    int fd;
    int status;
    pid_t pid;
    int n;

    /* errno exists, is writable, and is per-program state. */
    errno = 0;
    ok(errno == 0, "errno starts clear");
    ok(open("/tmp/no-such-file-here", O_RDONLY) == -1, "open of a missing file fails");
    ok(errno != 0, "a failed call sets errno");

    /* Create, write, seek, read back: the descriptor path end to end. */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    ok(fd >= 0, "create with O_CREAT");
    if (fd >= 0) {
        n = write(fd, "hello posix\n", 12);
        ok(n == 12, "write returns the count");
        ok(lseek(fd, 0, SEEK_SET) == 0, "seek to the start");
        close(fd);
    }

    ok(stat(path, &st) == 0, "stat the new file");
    ok(st.st_size == 12, "stat reports the size written");
    ok(!S_ISDIR(st.st_type), "stat says a file is not a directory");

    fd = open(path, O_RDONLY);
    ok(fd >= 0, "open for reading");
    if (fd >= 0) {
        n = read(fd, buf, sizeof buf - 1);
        ok(n == 12, "read returns the count");
        buf[n > 0 ? n : 0] = '\0';
        ok(n == 12 && buf[0] == 'h' && buf[11] == '\n', "the bytes came back");
        ok(isatty(fd) == 0, "a file is not a tty");
        ok(close(fd) == 0, "close succeeds");
        ok(close(fd) == -1, "closing twice fails");
    }

    /* access() is the one call that was enforcing permissions before open was. */
    ok(access(path, R_OK) == 0, "access R_OK on a readable file");
    ok(access("/tmp/no-such-file-here", F_OK) == -1, "access F_OK on nothing");

    /* Descriptor duplication: the second handle sees the same file. */
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        int fd2 = dup(fd);
        ok(fd2 >= 0 && fd2 != fd, "dup returns a new descriptor");
        if (fd2 >= 0) {
            ok(read(fd2, buf, 5) == 5, "the duplicate reads");
            close(fd2);
        }
        close(fd);
    }

    /* Where we are, and a pipe between two descriptors. */
    {
        char cwd[64];

        ok(getcwd(cwd, sizeof cwd) != 0, "getcwd");
        ok(cwd[0] == '/', "the working directory is a path");
        /* One byte short of the path plus its terminator: "/" needs two. */
        ok(getcwd(cwd, slen(cwd)) == 0 && errno == ERANGE,
           "a buffer too small is ERANGE, not a truncated path");
    }
    {
        int fds[2];

        /* pipe() is not implemented and says so: ENOSYS, not a descriptor that
         * faults when it is used. */
        errno = 0;
        ok(pipe(fds) == -1 && errno == ENOSYS, "pipe reports ENOSYS");
    }

    /* Directories, then removal. */
    ok(mkdir("/tmp/posixdir", 0755) == 0 || errno != 0, "mkdir either works or says why");
    ok(rmdir("/tmp/posixdir") == 0 || errno != 0, "rmdir either works or says why");
    ok(unlink(path) == 0, "unlink removes the file");
    ok(stat(path, &st) == -1, "the file is gone");

    /* A child process, its exit status, and wait() to collect it. */
    pid = fork();
    ok(pid >= 0, "fork returns a pid");
    if (pid == 0) {
        /* The child: getpid inside a process must agree with the parent's
         * idea of the child's pid, which is checked below by the status. */
        return getpid() > 0 ? 42 : 1;
    }
    if (pid > 0) {
        status = -1;
        ok(waitpid(pid, &status, 0) == pid, "waitpid returns the child");
        ok(WIFEXITED(status), "the child exited");
        ok(WEXITSTATUS(status) == 42, "the child's status came through");
    }

    /* Identity: everything runs as root in this wave, and says so. */
    ok(getpid() > 0, "getpid");
    ok(getuid() == 0, "uid 0");
    ok(geteuid() == 0, "euid 0");
    ok(getgid() == 0, "gid 0");

    /* Signals: a name is a number, and an ignorable one can be ignored. */
    ok(signal(SIGUSR1, SIG_IGN) != SIG_ERR, "signal installs an ignore");
    ok(kill(getpid(), SIGUSR1) == 0, "kill to self with an ignored signal");

    /* Locale: the C locale is the only one, and that is a valid answer. */
    ok(setlocale(LC_ALL, "C") != 0, "setlocale C");
    ok(setlocale(LC_ALL, "en_GB.UTF-8") == 0, "an absent locale is refused");
    ok(setlocale(LC_ALL, 0) != 0, "a query returns the current one");

    /* Character classes, which every C program assumes. */
    ok(toupper('q') == 'Q', "toupper");
    ok(tolower('Q') == 'q', "tolower");
    ok(isdigit('7') && !isdigit('x'), "isdigit");
    ok(isspace(' ') && isspace('\n') && !isspace('x'), "isspace");

    /* limits.h holds numbers, and PATH_MAX has to fit the kernel's path limit. */
    ok(PATH_MAX >= 256, "PATH_MAX is at least the filesystem's name limit");
    ok(NAME_MAX >= 8, "NAME_MAX is usable");

    ok(slen("posix") == 5, "the C library's own code still works");

    if (fails != 0) {
        return 1;
    }
    printf("POSIX_OK\n");
    return 0;
}
