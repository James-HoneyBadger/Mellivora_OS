/*
 * thread.c - threads over SYS_CLONE and SYS_THREAD_EXIT (EXEC.md section 6)
 *
 * The kernel's clone takes an entry point and a stack pointer and nothing else,
 * because everything a thread needs beyond that is already a property of the
 * process it is joining. This file turns that into the shape a C program wants:
 * a function, an argument, and a return value the joiner can collect.
 *
 * The stack is arranged the way a `call` leaves one:
 *
 *     higher addresses   |  argument            |   <- [esp + 4]
 *                        |  mv_thread_exit_thunk|   <- [esp]
 *     esp ->             +----------------------+
 *
 * so the entry point is an ordinary function and its `return` lands on the
 * thread's own exit rather than on whatever the caller was doing.
 */
#include <mv/thread.h>
#include <mv/syscall.h>

#define MV_STR_(x) #x
#define MV_STR(x) MV_STR_(x)

#define MV_THREAD_MAX_JOINABLE 8

/* PROT_* as the kernel defines them (VMM.md section 2). */
#define MV_PROT_READ  1
#define MV_PROT_WRITE 2

static struct {
    mv_thread_t id;
    void *stack;
    u32 size;
} stacks[MV_THREAD_MAX_JOINABLE];

/* The return address mv_thread_create puts on a new stack. The thread's return
 * value arrives in EAX and becomes its exit status, so a joiner sees what the
 * function returned without the function knowing it is a thread. */
__asm__(
    ".text\n"
    ".globl mv_thread_exit_thunk\n"
    ".type mv_thread_exit_thunk, @function\n"
    "mv_thread_exit_thunk:\n"
    "    movl %eax, %ebx\n"
    "    movl $" MV_STR(SYS_THREAD_EXIT) ", %eax\n"
    "    int $0x80\n"
    "    jmp mv_thread_exit_thunk\n"
    ".size mv_thread_exit_thunk, . - mv_thread_exit_thunk\n");

extern void mv_thread_exit_thunk(void);

static int remember_stack(mv_thread_t id, void *stack, u32 size)
{
    int i;

    for (i = 0; i < MV_THREAD_MAX_JOINABLE; i++) {
        if (stacks[i].stack == NULL) {
            stacks[i].id = id;
            stacks[i].stack = stack;
            stacks[i].size = size;
            return 0;
        }
    }
    return -1;
}

static void *forget_stack(mv_thread_t id, u32 *size)
{
    int i;

    for (i = 0; i < MV_THREAD_MAX_JOINABLE; i++) {
        if (stacks[i].stack != NULL && stacks[i].id == id) {
            void *stack = stacks[i].stack;
            *size = stacks[i].size;
            stacks[i].stack = NULL;
            stacks[i].id = 0;
            return stack;
        }
    }
    return NULL;
}

i32 mv_thread_create(mv_thread_t *id, i32 (*fn)(void *), void *arg)
{
    char *stack;
    u32 *sp;
    i32 tid;

    if (id == NULL || fn == NULL) {
        return -1;
    }

    stack = (char *)mv_syscall(SYS_MMAP, 0,
                               MV_THREAD_STACK_SIZE,
                               MV_PROT_READ | MV_PROT_WRITE, 0, 0);
    if ((i32)stack < 0) {
        return -1;
    }

    sp = (u32 *)(stack + MV_THREAD_STACK_SIZE);
    *--sp = (u32)arg;
    *--sp = (u32)mv_thread_exit_thunk;

    tid = mv_syscall(SYS_CLONE, (u32)fn, (u32)sp, 0, 0, 0);
    if (tid <= 0) {
        /* The new thread returns 0 here, and the caller of this function is
         * the one that created it - a 0 in the parent means the clone failed
         * rather than "you are the child", because the child never reaches
         * this line. */
        (void)mv_syscall(SYS_MUNMAP, (u32)stack, MV_THREAD_STACK_SIZE, 0, 0, 0);
        return -1;
    }

    if (remember_stack(tid, stack, MV_THREAD_STACK_SIZE) != 0) {
        /* Too many unjoined threads to account for: the thread is real and
         * running, so it is joined but its stack cannot be released. */
    }
    *id = tid;
    return 0;
}

i32 mv_thread_join(mv_thread_t id, i32 *status)
{
    i32 r;
    u32 size = 0;
    void *stack;

    r = mv_syscall(SYS_WAITPID, (u32)id, 0, 0, 0, 0);
    if (r < 0) {
        return -1;
    }
    if (status != NULL) {
        *status = r;
    }

    stack = forget_stack(id, &size);
    if (stack != NULL) {
        (void)mv_syscall(SYS_MUNMAP, (u32)stack, size, 0, 0, 0);
    }
    return 0;
}

void mv_thread_exit(i32 status)
{
    (void)mv_syscall(SYS_THREAD_EXIT, (u32)status, 0, 0, 0, 0);
    for (;;) {
        /* SYS_THREAD_EXIT does not return. */
    }
}

i32 mv_thread_yield(void)
{
    return mv_syscall(SYS_YIELD, 0, 0, 0, 0, 0);
}
