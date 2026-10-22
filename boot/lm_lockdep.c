/*
 * lm_lockdep.c - the lock order checker
 *
 * A deadlock is invisible until it happens, and then it is a machine that has
 * stopped with no evidence of why. The evidence can be collected as the locks are
 * taken: two locks taken in both orders by different paths are a deadlock waiting
 * for two CPUs, and that is a fact about the code, not about the schedule, so it
 * can be found by the first path that tries the second order.
 *
 * This is the shape lockdep established. Every lock gets a class when it is
 * initialised. Each CPU keeps a stack of the classes it is holding, which catches
 * the two mistakes one CPU can make on its own - taking a lock it already holds,
 * and releasing a lock that is not the innermost - and a graph records which
 * classes have been held inside which, which catches the mistake that needs two.
 *
 * The class is per lock object and not per type of lock, which is the one place
 * this is simpler than the real thing: two separate filesystem locks are two
 * classes here and one class there. With a handful of locks the difference does
 * not hide anything.
 *
 * Interrupts are off while a spinlock is held, so a single stack is enough on a
 * machine of one: nothing can interleave with the push. When the other processors
 * start this has to become one stack per CPU, and the graph has to be updated
 * under a lock of its own.
 */

#include "lm.h"

struct lock_class {
    const char *name;
    u32 seen;                   /* 1 once registered */
};

/* "a was held inside b": bit set means b -> a was observed. */
static struct lock_class classes[LOCKDEP_MAX_LOCKS];
static u8 order[LOCKDEP_MAX_LOCKS][LOCKDEP_MAX_LOCKS];

static int held[LOCKDEP_MAX_HELD];
static int depth;
static int deepest;
static int known;

static u64 violations;
static u64 caught;              /* what the self-test provoked on purpose */
static int planting;            /* the self-test is provoking one right now */

u32 lockdep_register(const void *lock, const char *name)
{
    u32 key = (u32)(((u64)(unsigned long)lock >> 4) % LOCKDEP_MAX_LOCKS);

    /* A collision would merge two locks into one class, which reports more
     * orderings than exist rather than fewer - the safe direction. */
    for (int n = 0; n < LOCKDEP_MAX_LOCKS; n++) {
        u32 slot = (key + (u32)n) % LOCKDEP_MAX_LOCKS;
        if (!classes[slot].seen) {
            classes[slot].seen = 1;
            classes[slot].name = name;
            known++;
            return slot;
        }
        if (classes[slot].name == name) {
            return slot;        /* the same lock initialised twice */
        }
    }
    return 0;
}

static void note(int kind, int a, int b)
{
    violations++;
    /* A violation the self-test asked for is not a fault, and the log should not
     * pretend it is: the same checker reports both, with the same words. */
    kputs(planting ? "[lockdep] planted " : "[lockdep] FAIL ");
    kputs(kind == 0 ? "already held: "
         : kind == 1 ? "released out of order: "
         : kind == 2 ? "released while not held: "
         : "lock order inverted: ");
    kputs(classes[a].name != NULL ? classes[a].name : "?");
    if (kind == 3) {
        kputs(" and ");
        kputs(classes[b].name != NULL ? classes[b].name : "?");
    }
    kputs("\r\n");
}

void lockdep_acquire(int klass)
{
    if (klass < 0 || klass >= LOCKDEP_MAX_LOCKS) {
        return;
    }

    for (int i = 0; i < depth; i++) {
        if (held[i] == klass) {
            note(0, klass, klass);
        } else if (order[klass][held[i]] != 0) {
            /* This lock was already seen inside that one, and now that one is
             * inside this one: two paths, two CPUs, one deadlock. */
            note(3, held[i], klass);
        }
        order[held[i]][klass] = 1;
    }

    if (depth < LOCKDEP_MAX_HELD) {
        held[depth++] = klass;
        if (depth > deepest) {
            deepest = depth;
        }
    }
}

void lockdep_release(int klass)
{
    if (klass < 0 || klass >= LOCKDEP_MAX_LOCKS) {
        return;
    }
    if (depth == 0) {
        note(2, klass, klass);
        return;
    }
    if (held[depth - 1] != klass) {
        note(1, klass, klass);
        /* Drop it from wherever it is, so the stack does not stay wrong. */
        for (int i = depth - 1; i >= 0; i--) {
            if (held[i] == klass) {
                for (int j = i; j < depth - 1; j++) {
                    held[j] = held[j + 1];
                }
                depth--;
                return;
            }
        }
        note(2, klass, klass);
        return;
    }
    depth--;
}

u64 lockdep_errors(void)
{
    return violations;
}

void lockdep_report(void)
{
    kputs("[lockdep] locks=");
    kput_dec((u64)known);
    kputs(" deepest=");
    kput_dec((u64)deepest);
    kputs(" errors=");
    kput_dec(violations);
    kputs(violations == 0 ? " lockdep=ok\r\n" : " lockdep=FAIL\r\n");
}

/*
 * The self-test provokes each check once and counts what came back. It is the
 * only way to know a checker works - a checker that silently agrees with
 * everything looks exactly like a correct kernel - and it costs four
 * acquisitions with nothing else running, so a violation that would deadlock a
 * real machine cannot here.
 */
int lockdep_selftest(void)
{
    lm_spinlock a, b;

    planting = 1;
    spin_init_named(&a, "probe-a");
    spin_init_named(&b, "probe-b");

    /* A inside B, then B inside A: the second is the inversion. */
    spin_lock(&b);
    spin_lock(&a);
    spin_unlock(&a);
    spin_unlock(&b);

    spin_lock(&a);
    spin_lock(&b);              /* would be reported */
    spin_unlock(&b);
    spin_unlock(&a);

    /* Released while not held, and released out of order. */
    spin_unlock(&a);

    spin_lock(&a);
    spin_lock(&b);
    spin_unlock(&a);            /* not the innermost */
    spin_unlock(&b);
    spin_unlock(&a);

    planting = 0;
    caught = violations;
    if (caught < 3) {
        kputs("[lockdep] FAIL the checker missed a planted violation\r\n");
        return -1;
    }

    /* Empty the graph: the orderings above were planted, and leaving them would
     * make every later acquisition against probe-a report an inversion. */
    for (int i = 0; i < LOCKDEP_MAX_LOCKS; i++) {
        for (int j = 0; j < LOCKDEP_MAX_LOCKS; j++) {
            order[i][j] = 0;
        }
    }
    depth = 0;
    violations = 0;

    kputs("[lockdep] selftest=ok caught=");
    kput_dec(caught);
    kputs(" probes=2\r\n");
    return 0;
}
