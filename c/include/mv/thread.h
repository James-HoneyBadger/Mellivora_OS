/*
 * thread.h - threads inside one process (EXEC.md section 6)
 *
 * A thread is a second task in the calling process: it shares the address
 * space, the open descriptors, the handles, the token and the heap, and it has
 * its own stack, its own kernel stack and its own thread-local storage. That is
 * the whole of what the kernel promises (SYS_CLONE), and this header is the
 * shape a program wants: create with a function and an argument, join for its
 * return value, exit early if it must.
 *
 * The kernel is not told a calling convention. mv_thread_create prepares the
 * new stack the way a call does - the argument at [esp+4], a return address at
 * [esp] - and names the function as the entry point, so the thread's first
 * instruction is an ordinary function call and its `return` goes to the thread
 * exit in this file, which hands the value to the kernel.
 *
 * A thread's id is the value mv_thread_create wrote to *id; the kernel has no
 * gettid, because SYS_CLONE's return value already names the thread and a
 * program that needs it keeps it.
 *
 * A thread shares the process's heap, and `malloc` in c/rt/malloc.c is not
 * synchronised - there is no kernel lock and no atomics in this runtime - so a
 * program that allocates from two threads at once needs a lock of its own. The
 * console is the same: two threads printing at once interleave.
 *
 * Detaching, priorities and cancellation are here; what is still not is
 * thread-local storage a C program can *declare* - `__thread` needs a TLS
 * template in the image, which only HBX images built by tools/mkhbx.py carry
 * (see programs/hbx/hbx_thread.asm).
 */
#ifndef MV_THREAD_H
#define MV_THREAD_H

#include <mv/types.h>

/* A thread id: what the kernel calls the thread, and what waitpid is given. */
typedef i32 mv_thread_t;

/* Stacks are demand-zero mappings, so the size costs address space rather than
 * memory until the thread actually uses it. 64 KB is the size the kernel gives
 * a background task's stack. */
#define MV_THREAD_STACK_SIZE (64 * 1024)

/*
 * Create a thread that runs fn(arg) and returns its result to the joiner.
 *   Out: *id = the thread's id
 *   Returns 0 on success, -1 when the kernel refused the clone or the stack
 *   could not be mapped.
 */
i32 mv_thread_create(mv_thread_t *id, i32 (*fn)(void *), void *arg);

/*
 * Wait for a thread and collect its return value (or the status it passed to
 * mv_thread_exit). Releases the thread's stack.
 *   Returns 0 on success, -1 if there is no such thread.
 */
i32 mv_thread_join(mv_thread_t id, i32 *status);

/* End this thread only; the process and its other threads keep running. */
void mv_thread_exit(i32 status) __attribute__((noreturn));

/* Hand the processor to another ready thread (SYS_YIELD). */
i32 mv_thread_yield(void);

/*
 * Say that this thread will never be joined. Its exit status is discarded and
 * its kernel resources are recycled as soon as it ends, so detaching is what
 * keeps a program that starts many short threads from accumulating zombies.
 * The stack it was given is a mapping of the process and is released with the
 * process; unmap it yourself if you need the address space back.
 *   Returns 0, or -1 when the id is not a live thread of this process.
 */
i32 mv_thread_detach(mv_thread_t id);

/*
 * Set a thread's scheduling priority (PRIO_HIGH 0 .. PRIO_IDLE 3; lower runs
 * first). The scheduler already orders the run queue by this value.
 *   Returns the priority it had, or -1.
 */
#define MV_THREAD_PRIO_HIGH   0
#define MV_THREAD_PRIO_NORMAL 1
#define MV_THREAD_PRIO_LOW    2
#define MV_THREAD_PRIO_IDLE   3
i32 mv_thread_set_priority(mv_thread_t id, i32 prio);

/*
 * Ask a thread to end. Cancellation is *cooperative*: the thread stops when it
 * next calls mv_thread_testcancel(), so it never abandons a kernel call
 * half-way. A cancelled thread exits with MV_THREAD_CANCELED as its status, and
 * its joiner sees that value.
 *   Returns 0, or -1 when the id is not a live thread of this process.
 */
#define MV_THREAD_CANCELED ((i32)0x40000000)
i32 mv_thread_cancel(mv_thread_t id);

/*
 * A cancellation point. If a cancel is pending, this thread ends here with
 * MV_THREAD_CANCELED and this function does not return; otherwise it returns 0.
 */
i32 mv_thread_testcancel(void);

#endif /* MV_THREAD_H */
