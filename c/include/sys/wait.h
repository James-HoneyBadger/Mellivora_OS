/*
 * sys/wait.h - waiting for a child, and reading what it returned
 *
 * The kernel's waitpid hands back the child's exit status as a plain value or
 * -1. POSIX wraps that in a status word with WIFEXITED and friends, and the
 * macros below do that wrapping without changing the number: a plain exit
 * status n is reported as (n << 8), which is what WEXITSTATUS unpacks.
 */
#ifndef ESYS_WAIT_H
#define ESYS_WAIT_H

#include <mv/types.h>
#include <unistd.h>

#define WNOHANG     1

#define WEXITSTATUS(s)  (((s) >> 8) & 0xFF)
#define WIFEXITED(s)    ((s) >= 0)
#define WIFSIGNALED(s)  (0)
#define WTERMSIG(s)     (0)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

#endif /* ESYS_WAIT_H */
