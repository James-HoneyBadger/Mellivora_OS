/*
 * unistd.h - the calls a POSIX program makes without a stream in between
 *
 * Every function here maps to a syscall this kernel has. The ones POSIX also
 * requires that have no syscall behind them - pipe(), getcwd() beyond the
 * kernel's own limit, symlink()'s POSIX argument order - are deliberately
 * absent rather than faked: a missing declaration is a compile error, which
 * is a better outcome than a call that returns a plausible lie.
 */
#ifndef EUNISTD_H
#define EUNISTD_H

#include <mv/types.h>

typedef i32 pid_t;
typedef u32 uid_t;
typedef u32 gid_t;
typedef i32 ssize_t;
typedef i32 off_t;

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

ssize_t read(int fd, void *buf, unsigned count);
ssize_t write(int fd, const void *buf, unsigned count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

int dup(int fd);
int dup2(int fd, int fd2);
int unlink(const char *path);
int rmdir(const char *path);
int access(const char *path, int mode);
int chdir(const char *path);
char *getcwd(char *buf, unsigned size);
int pipe(int fds[2]);
int isatty(int fd);
int fcntl(int fd, int cmd, int arg);

pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execl(const char *path, const char *arg, ...);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
unsigned sleep(unsigned seconds);

#endif /* EUNISTD_H */
