/*
 * threadtest.c - threads, and what makes them threads (EXEC.md section 6)
 *
 * The kernel's promise is narrow: SYS_CLONE makes a second task in the calling
 * process, and it shares the address space, the descriptors, the handles and
 * the heap while owning a stack, a kernel stack and its thread-local storage.
 * Every check below is one half of that promise, and the ones that need a
 * switch to mean anything yield in a loop so the scheduler has to do it.
 *
 * What it proves:
 *   - four threads run and each returns its own value to its joiner;
 *   - a global the caller wrote is visible in every thread, and a value a
 *     thread writes through a pointer is visible in the caller after the join
 *     (one address space);
 *   - the heap pointer the caller allocated is still the same allocation
 *     inside a thread (one heap, not a copy);
 *   - getpid() is the same in every thread (one process);
 *   - a thread that calls mv_thread_exit early still reports the status it
 *     chose, and the process keeps running;
 *   - a thread's stack is where its own frames are: each thread reports a
 *     different address for its local, so they are not sharing one stack.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mv/thread.h>

static int fails;
static int shared_value;
static int seen[8];
static void *heap_block;
static pid_t process_id;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("THREAD_FAIL %s\n", what);
        fails++;
    }
}

struct work {
    int index;
    int value;
    unsigned long local_probe;
};

static i32 worker(void *arg)
{
    struct work *w = (struct work *)arg;
    int i;

    /* An address on this thread's own stack, for the caller to compare. */
    w->local_probe = (unsigned long)&i;

    if (getpid() != process_id) {
        return -1;
    }
    if (shared_value != 0x5A5A) {
        return -2;
    }
    if (heap_block == NULL || *(int *)heap_block != 0x1234) {
        return -3;
    }

    /* Give the scheduler every chance to switch away and back: a thread-local
     * or a stack that is really shared shows up here, not in a single pass. */
    for (i = 0; i < 4; i++) {
        w->value += w->index + 1;
        mv_thread_yield();
        if (getpid() != process_id) {
            return -4;
        }
        if (w->local_probe != (unsigned long)&i) {
            return -5;          /* our own frame moved: not our stack */
        }
    }

    seen[w->index] = w->value;
    *(int *)heap_block += 1;
    return w->value;
}

/* A thread with no shared state to inspect, for the churn loop: what it returns
 * is all it has to prove, plus the switches it forces on the way. */
static i32 churn_worker(void *arg)
{
    int i;

    for (i = 0; i < 2; i++) {
        mv_thread_yield();
    }
    return (i32)(unsigned long)arg;
}

static i32 early_exit(void *arg)
{
    (void)arg;
    mv_thread_exit(77);
    return 0;                   /* not reached */
}

int main(void)
{
    mv_thread_t id[4];
    struct work work[4];
    i32 status = 0;
    int i;

    process_id = getpid();
    shared_value = 0x5A5A;
    heap_block = malloc(sizeof(int));
    if (heap_block == NULL) {
        printf("THREAD_FAIL malloc\n");
        return 1;
    }
    *(int *)heap_block = 0x1234;

    for (i = 0; i < 4; i++) {
        work[i].index = i;
        work[i].value = 0;
        work[i].local_probe = 0;
        if (mv_thread_create(&id[i], worker, &work[i]) != 0) {
            printf("THREAD_FAIL create %d\n", i);
            return 1;
        }
    }

    for (i = 0; i < 4; i++) {
        status = -1;
        if (mv_thread_join(id[i], &status) != 0) {
            printf("THREAD_FAIL join %d\n", i);
            return 1;
        }
        /* Each thread added (index + 1) four times and returned the total. */
        check(status == 4 * (i + 1), "return value");
        check(seen[i] == 4 * (i + 1), "shared array");
        check(work[i].local_probe != 0, "local address");
        if (i > 0) {
            check(work[i].local_probe != work[0].local_probe,
                  "two threads shared a stack");
        }
    }

    /* Four threads each incremented the one heap int. */
    check(*(int *)heap_block == 0x1234 + 4, "heap shared");
    check(getpid() == process_id, "pid after joins");

    /* A thread that exits itself is joined like one that returns. */
    {
        mv_thread_t early = 0;

        if (mv_thread_create(&early, early_exit, NULL) != 0) {
            printf("THREAD_FAIL create early\n");
            return 1;
        }
        status = 0;
        if (mv_thread_join(early, &status) != 0) {
            printf("THREAD_FAIL join early\n");
            return 1;
        }
        check(status == 77, "thread exit status");
    }

    /* Churn: more threads than there are task slots or mapping records, one at
     * a time. A join that forgot to release the stack would run out of area
     * records (32), and an exit that forgot its kernel stack or TLS blocks
     * would run out of frames; the loop is the check that neither happens. */
    {
        int round;

        for (round = 0; round < 64; round++) {
            mv_thread_t one = 0;

            if (mv_thread_create(&one, churn_worker, (void *)4) != 0) {
                printf("THREAD_FAIL churn create %d\n", round);
                break;
            }
            status = -1;
            if (mv_thread_join(one, &status) != 0) {
                printf("THREAD_FAIL churn join %d\n", round);
                break;
            }
            check(status == 4, "churn return value");
        }
    }

    free(heap_block);

    if (fails != 0) {
        printf("THREAD_FAIL %d check(s)\n", fails);
        return 1;
    }
    printf("THREAD_OK\n");
    return 0;
}
