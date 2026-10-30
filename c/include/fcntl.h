/*
 * fcntl.h - open() and the flags it takes
 *
 * The kernel's open understands access mode and O_NONBLOCK. It has no create
 * flag: a file is made with SYS_TOUCH (see c/rt/posix.c, creat()) and then
 * opened. So oflag carries O_CREAT here and open() returns ENOSYS for it
 * rather than quietly opening something that already exists - a program that
 * asked to create a file and got an existing one back is worse than one that
 * was told no.
 */
#ifndef EFCNTL_H
#define EFCNTL_H

#include <mv/types.h>

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800

#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

int open(const char *path, int oflag, ...);
int creat(const char *path, unsigned mode);

#endif /* EFCNTL_H */
