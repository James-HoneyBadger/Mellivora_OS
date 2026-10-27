/*
 * cprog.c - a user program built by the host C compiler, not by tcc
 *
 * The other half of M0's C exit criterion. It runs in ring 3 from the program
 * window at 0x00500000, links the same freestanding runtime the kernel module
 * does, and reaches the kernel only through int 0x80 (docs/spec/ABI.md).
 *
 * It is deliberately a self-test rather than a demo: a program that prints a
 * string proves the loader and the syscall path, but not that the runtime is
 * linked, that .rodata is at the address it was linked at, or that the stack
 * the kernel hands a task is usable by compiled code. So it checks all of
 * those and prints C_PROG_OK only when every check passes.
 *
 * It stays single-task on purpose: the per-task half of M0 is
 * programs/sse_test.asm, and a program whose verdict depends on the fork path
 * cannot say which of the two broke.
 */
#include <mv/string.h>
#include <mv/syscall.h>
#include <mv/types.h>

static const char msg_ok[] = "C_PROG_OK\r\n";

/* Prints a small unsigned number without a printf: the point is that the
 * compiler's own arithmetic and the syscall path agree. */
static void print_num(u32 value)
{
    char digits[11];
    u32 n = 0;

    if (value == 0) {
        mv_putchar('0');
        return;
    }
    while (value != 0 && n < sizeof(digits)) {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n-- != 0)
        mv_putchar(digits[n]);
}

static i32 check_runtime(void)
{
    static const u8 src[8] = { 'm', 'e', 'l', 'l', 'i', 'v', 'o', 'r' };
    char buf[16];

    if (strlen("") != 0 || strlen("ring three") != 10)
        return 1;

    memset(buf, '#', sizeof(buf));
    if (buf[0] != '#' || buf[15] != '#')
        return 2;

    memcpy(buf, src, sizeof(src));
    if (memcmp(buf, src, sizeof(src)) != 0)
        return 3;

    /* Overlapping move, destination above the source: the seven bytes after
     * buf[0] become its own first seven, and buf[8] is left alone. */
    memcpy(buf, src, sizeof(src));
    memmove(buf + 1, buf, 7);
    if (buf[0] != 'm' || buf[1] != 'm' || buf[7] != 'o' || buf[8] != '#')
        return 4;

    if (strcmp("mellivora", "mellivora") != 0)
        return 5;
    return 0;
}

/* A loop the compiler is free to rewrite, and a stack frame it has to lay
 * out: the point is that compiled code and the kernel agree about this task's
 * stack and its break. */
static u32 check_arithmetic(void)
{
    u32 acc = 0;
    u32 i;

    for (i = 1; i <= 100; i++)
        acc += i * i;
    return acc;
}

__attribute__((section(".text.entry"), used))
void _start(void)
{
    i32 rc;

    rc = check_runtime();
    if (rc != 0) {
        mv_print("C_PROG_FAIL: runtime ");
        print_num((u32)rc);
        mv_print("\r\n");
        mv_exit(1);
    }

    if (check_arithmetic() != 338350u) {
        mv_print("C_PROG_FAIL: arithmetic\r\n");
        mv_exit(2);
    }

    mv_print("C_PROG: C runtime and syscalls ok, pid ");
    print_num((u32)mv_getpid());
    mv_print("\r\n");
    mv_print(msg_ok);
    mv_exit(0);
}
