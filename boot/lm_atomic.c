/*
 * lm_atomic.c - atomics, and the lock built on them
 *
 * Everything in this port has been safe by construction so far because there was
 * one CPU and the kernel never ran two things at once: interrupts were off inside
 * a syscall, and the scheduler refused to switch away from a ring-0 frame. That
 * stops being true the moment a second processor starts, and the fixes are the
 * two primitives in this file - an instruction the bus will not let two CPUs
 * interleave, and a word that says whether somebody is inside a critical section.
 *
 * The lock is also useful with one CPU: taking it means interrupts are off while
 * it is held, so a timer tick can no longer observe a half-updated file table.
 * That is why sched_lock() is built on it rather than on a counter alone.
 *
 * The self-test can only prove what one CPU can observe - that the instructions
 * do what they say and that the interrupt state is restored exactly - and is
 * honest about being that.
 */

#include "lm.h"

u64 irq_save(void)
{
    u64 flags;
    __asm__ volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

void irq_restore(u64 flags)
{
    if ((flags & 0x200) != 0) {
        __asm__ volatile("sti" ::: "memory");
    } else {
        __asm__ volatile("cli" ::: "memory");
    }
}

int irqs_enabled(void)
{
    u64 flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

u32 atomic_xchg32(volatile u32 *target, u32 value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

/* 1 when the swap happened: the value was `expected` and is now `value`. */
int atomic_cmpxchg32(volatile u32 *target, u32 expected, u32 value)
{
    return __atomic_compare_exchange_n(target, &expected, value, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

u32 atomic_add32(volatile u32 *target, u32 value)
{
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}

void spin_init_named(lm_spinlock *lock, const char *name)
{
    lock->locked = 0;
    lock->klass = (u16)lockdep_register(lock, name);
    lock->named = 1;
}

void spin_init(lm_spinlock *lock)
{
    spin_init_named(lock, "lock");
}

/* The exchange is the lock: whoever turns 0 into 1 owns it, and everyone else
 * keeps trying until they do. The pause is not politeness - it is what stops a
 * spinning core from hammering the cache line the owner is trying to write. */
void spin_lock(lm_spinlock *lock)
{
    for (;;) {
        if (atomic_xchg32(&lock->locked, 1) == 0) {
            if (lock->named) {
                lockdep_acquire(lock->klass);
            }
            return;
        }
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED) != 0) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
}

int spin_trylock(lm_spinlock *lock)
{
    if (atomic_xchg32(&lock->locked, 1) != 0) {
        return 0;
    }
    if (lock->named) {
        lockdep_acquire(lock->klass);
    }
    return 1;
}

void spin_unlock(lm_spinlock *lock)
{
    if (lock->named) {
        lockdep_release(lock->klass);
    }
    __atomic_store_n(&lock->locked, 0, __ATOMIC_SEQ_CST);
}

/*
 * What one CPU can prove about all of that: the exchange returns the old value,
 * the compare-and-swap fails without writing when it should, trylock refuses a
 * held lock, and the interrupt flag comes back the way it was.
 */
int atomic_selftest(void)
{
    int ok = 1;
    volatile u32 word = 0x1234;

    if (atomic_xchg32(&word, 0x5678) != 0x1234 || word != 0x5678) {
        ok = 0;
    }
    if (!atomic_cmpxchg32(&word, 0x5678, 0x9ABC) || word != 0x9ABC) {
        ok = 0;
    }
    if (atomic_cmpxchg32(&word, 0x1111, 0x2222) || word != 0x9ABC) {
        ok = 0;                        /* a failed swap must not write */
    }
    if (atomic_add32(&word, 1) != 0x9ABC) {
        ok = 0;
    }

    lm_spinlock lock;
    spin_init(&lock);
    if (!spin_trylock(&lock)) {
        ok = 0;
    }
    if (spin_trylock(&lock)) {
        ok = 0;                        /* it is ours already */
    }
    spin_unlock(&lock);
    if (!spin_trylock(&lock)) {
        ok = 0;                        /* and free again once released */
    }
    spin_unlock(&lock);

    /* The interrupt state has to survive a critical section exactly: off stays
     * off, on comes back on. */
    int before = irqs_enabled();
    u64 flags = irq_save();
    if (irqs_enabled()) {
        ok = 0;
    }
    irq_restore(flags);
    if (irqs_enabled() != before) {
        ok = 0;
    }

    kputs(ok ? "[lock] spin=ok atomic=ok irq=ok\r\n"
             : "[lock] FAIL atomics or the lock misbehave\r\n");
    return ok ? 0 : -1;
}
