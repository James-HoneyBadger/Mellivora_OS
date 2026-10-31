/*
 * threadmore.c - detaching, priorities and cancellation (EXEC.md section 6)
 *
 * The rest of what a thread can be told to do once it exists. Each check is
 * something a program can observe rather than a claim: a detached thread's
 * join fails because its status is gone, a priority change reports what it
 * replaced, and a cancelled thread's joiner collects MV_THREAD_CANCELED rather
 * than the value the thread would otherwise have returned.
 *
 * Prints THREAD_MORE_OK when all of it held.
 */
#include <stdio.h>
#include <mv/thread.h>

#define FAIL(w) do { printf("THREAD_MORE_FAIL %s\n", w); fails++; } while (0)

static int fails;

/* Runs, returns 11, and is never joined - it is detached before it starts. */
static i32 detached_worker(void *arg)
{
    (void)arg;
    mv_thread_yield();
    return 11;
}

/* Asks to be cancelled in a loop, and would otherwise return 22. */
static i32 cancellable_worker(void *arg)
{
    volatile int spin = 0;
    int i;

    (void)arg;
    for (i = 0; i < 200000; i++) {
        spin += i;
        mv_thread_testcancel();     /* the cancellation point */
    }
    return spin == 0 ? 22 : 22;
}

static i32 ordinary_worker(void *arg)
{
    (void)arg;
    return 33;
}

int main(void)
{
    mv_thread_t t;
    i32 status = 0;
    i32 old;

    printf("A start\n");
    /* --- detach: the thread ends, and no status is left for a joiner --- */
    if (mv_thread_create(&t, detached_worker, NULL) != 0) {
        FAIL("create detached");
    } else {
        printf("A created t=%d\n", (int)t);
        if (mv_thread_detach(t) != 0) {
            FAIL("detach");
        }
        printf("A detached\n");
        mv_thread_yield();
        mv_thread_yield();
        printf("A joining\n");
        if (mv_thread_join(t, &status) == 0) {
            FAIL("a detached thread was joined");
        }
        printf("detach ok\n");
    }

    /* --- priorities: the call reports the value it replaced --- */
    if (mv_thread_create(&t, ordinary_worker, NULL) != 0) {
        FAIL("create priority");
    } else {
        old = mv_thread_set_priority(t, MV_THREAD_PRIO_HIGH);
        if (old != MV_THREAD_PRIO_NORMAL) {
            FAIL("the default priority was not NORMAL");
        }
        if (mv_thread_set_priority(t, 7) != -1) {
            FAIL("an out-of-range priority was accepted");
        }
        if (mv_thread_join(t, &status) != 0 || status != 33) {
            FAIL("join after a priority change");
        }
        printf("priority ok\n");
    }

    /* --- cancel: cooperative, and the joiner sees CANCELED --- */
    if (mv_thread_create(&t, cancellable_worker, NULL) != 0) {
        FAIL("create cancel");
    } else {
        if (mv_thread_cancel(t) != 0) {
            FAIL("cancel");
        }
        if (mv_thread_join(t, &status) != 0) {
            FAIL("join after cancel");
        } else if (status != MV_THREAD_CANCELED) {
            FAIL("a cancelled thread's status was not CANCELED");
        } else {
            printf("cancel ok\n");
        }
    }

    /* Cancelling an id that is not a live thread of this process is refused. */
    if (mv_thread_cancel(9999) != -1) {
        FAIL("cancel of a made-up id was accepted");
    }

    if (fails != 0) {
        return 1;
    }
    printf("THREAD_MORE_OK\n");
    return 0;
}
