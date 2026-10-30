/*
 * start.c - the start-up code a C program is linked against
 *
 * The linker script puts .text.entry first and names _start, so this is the
 * first byte of every C-built program: the kernel jumps here with the task's
 * stack ready and nothing else set up. The start-up calls main and hands its
 * return value to the kernel as the exit status, which is what makes
 * `crtdemo && something` mean what a user expects.
 *
 * main takes no arguments yet. The kernel gives a program a flattened
 * command-line buffer (SYS_GETARGS), not the argument and environment block
 * EXEC.md section 5.1 describes, so there is no argv to pass. A program reads
 * the buffer with mv_getargs() until the block lands; the day it does, this
 * file grows the argc/argv form and main's signature changes with it.
 */
#include <mv/stdlib.h>

extern int main(void);

__attribute__((section(".text.entry"), used, noreturn))
void _start(void)
{
    exit(main());
}
