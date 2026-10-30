/*
 * signal.h - dispositions and delivery, over SYS_SIGACTION and SYS_KILL
 */
#ifndef ESIGNAL_H
#define ESIGNAL_H

#include <mv/types.h>
#include <unistd.h>

#define SIGHUP   1
#define SIGINT   2
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGTSTP  20
#define SIGCONT  25

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef int sigset_t;

void (*signal(int sig, void (*func)(int)))(int);
int raise(int sig);
int kill(pid_t pid, int sig);

#endif /* ESIGNAL_H */
