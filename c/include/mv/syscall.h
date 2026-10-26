/*
 * syscall.h - the user-mode side of the syscall ABI, in C
 *
 * docs/spec/ABI.md section 4: int 0x80, the call number in EAX, arguments in
 * EBX/ECX/EDX/ESI/EDI, the result in EAX, and every other register preserved.
 * That last part is what makes these wrappers safe to write as one asm
 * statement each: because the kernel preserves EBX..EDI, they need no clobber
 * list beyond "memory", and a C caller can keep values in registers across a
 * call exactly as an assembly program does.
 *
 * A program that calls a syscall this header does not wrap can still use
 * mv_syscall() with the numbers in syscall_numbers.h.
 */
#ifndef MV_SYSCALL_H
#define MV_SYSCALL_H

#include <mv/syscall_numbers.h>
#include <mv/types.h>

static inline i32 mv_syscall(u32 nr, u32 a, u32 b, u32 c, u32 d, u32 e)
{
    i32 ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return ret;
}

static inline i32 mv_print(const char *s)
{
    return mv_syscall(SYS_PRINT, (u32)s, 0, 0, 0, 0);
}

static inline i32 mv_putchar(char ch)
{
    return mv_syscall(SYS_PUTCHAR, (u32)(u8)ch, 0, 0, 0, 0);
}

static inline i32 mv_write(i32 fd, const void *buf, u32 len)
{
    return mv_syscall(SYS_WRITE, (u32)fd, (u32)buf, len, 0, 0);
}

static inline i32 mv_getpid(void)
{
    return mv_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
}

static inline i32 mv_yield(void)
{
    return mv_syscall(SYS_YIELD, 0, 0, 0, 0, 0);
}

static inline i32 mv_fork(void)
{
    return mv_syscall(SYS_FORK, 0, 0, 0, 0, 0);
}

static inline void mv_exit(i32 code)
{
    (void)mv_syscall(SYS_EXIT, (u32)code, 0, 0, 0, 0);
}

#endif /* MV_SYSCALL_H */
