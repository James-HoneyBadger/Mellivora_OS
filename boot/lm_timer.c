/*
 * lm_timer.c - high-resolution timers
 *
 * The tick is a fine thing for deciding whose turn it is and a poor thing for
 * deciding when something should happen: it arrives a hundred times a second, so
 * every wait expressed against it is rounded up to the next ten milliseconds and
 * a hundred one-millisecond sleeps take a second. What is wanted is a deadline -
 * a moment on a free-running counter - and a device that can be told to interrupt
 * at exactly that moment. The HPET is the counter and the local APIC's timer is
 * the device, once it is used as a one-shot rather than a metronome.
 *
 * So there is one timer per CPU, and it is always armed. The next interrupt is
 * whichever comes first: the next deadline in the queue, or the next scheduling
 * tick. A queue of nothing still gives the tick, and a deadline in the middle of
 * a tick arrives in the middle of a tick.
 *
 * Two things a deadline must not do. It must not be handled twice: a slot is
 * cleared before its callback runs, so a callback is free to arm another timer
 * from inside itself. And it must not be handled late by more than the loop
 * below takes: every expired timer is run on the interrupt that finds it, so the
 * error is the time between the deadline and the interrupt, which is the accuracy
 * of the device - some hundreds of nanoseconds - and not the tick.
 */

#include "lm.h"

#define TIMER_SLOTS 64

struct timer_slot {
    u64 expiry;                 /* nanoseconds on the HPET's clock, 0 when free */
    void (*fn)(void *);
    void *arg;
    u8 used;
};

static struct timer_slot slots[TIMER_SLOTS];
static u64 tick_deadline;       /* when the next 100 Hz tick is due */
static u64 tick_period;         /* nanoseconds between ticks */
static int timer_ready;

/* The counter the whole file is expressed against: the HPET when there is one,
 * the TSC when there is not. Both are free-running and neither is ever reset, so
 * a deadline is an absolute instant and not an interval. */
static u64 now_ns(void)
{
    u32 hz = hpet_hz();
    if (hz != 0) {
        u64 count = hpet_counter();
        return (count / hz) * 1000000000ULL + ((count % hz) * 1000000000ULL) / hz;
    }

    u64 tsc = tsc_hz();
    if (tsc != 0) {
        u64 count = tsc_counter();
        return (count / tsc) * 1000000000ULL + ((count % tsc) * 1000000000ULL) / tsc;
    }
    return 0;
}

u64 timer_now_ns(void)
{
    return timer_ready ? now_ns() : 0;
}

/* Called from the timer interrupt with interrupts already off and, on a machine
 * of one, with nothing else able to touch the queue. The mutators mask interrupts
 * for the same reason: a slot is four stores long and an interrupt in the middle
 * of them would see a deadline attached to the wrong callback. */
static u64 next_deadline(void)
{
    u64 best = 0;

    for (int i = 0; i < TIMER_SLOTS; i++) {
        if (slots[i].used && (best == 0 || slots[i].expiry < best)) {
            best = slots[i].expiry;
        }
    }
    return best;
}

/*
 * Point the one-shot at the next thing that needs to happen. This has to be done
 * before any decision to switch tasks: after sched_tick() returns another task's
 * frame this code is not resumed, and a timer left unarmed is a machine that
 * never ticks again.
 */
void timer_rearm(void)
{
    if (!timer_ready) {
        return;
    }

    u64 now = now_ns();
    u64 target = tick_deadline;
    u64 deadline = next_deadline();
    if (deadline != 0 && deadline < target) {
        target = deadline;
    }

    u64 delta = target > now ? target - now : 1;
    u64 ticks = (delta * (u64)lapic_ticks_per_ms() + 999999ULL) / 1000000ULL;

    /* A count that has already gone by still has to interrupt once, or the queue
     * would sit there waiting for a tick that is not armed. */
    lapic_timer_arm_once(ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (u32)ticks);
}

/*
 * The interrupt's work: report whether a scheduling tick has come due, and run
 * everything whose deadline has passed. Running the callbacks here, before the
 * scheduler looks at the run queue, is what lets a wake-up land in the queue the
 * same tick decides on.
 */
int timer_service(void)
{
    if (!timer_ready) {
        return 1;                       /* no queue yet: every interrupt is a tick */
    }

    u64 now = now_ns();
    int ticked = 0;

    if (now >= tick_deadline) {
        ticked = 1;
        tick_deadline += tick_period;
        if (tick_deadline <= now) {
            /* A tick was missed - a long critical section, or a switch that took
             * longer than a tick. Skipping the lost ones keeps the phase right;
             * replaying them would fire a burst of them, and every sleeper would
             * wake at once. */
            tick_deadline = now + tick_period;
        }
    }

    for (int i = 0; i < TIMER_SLOTS; i++) {
        if (!slots[i].used || slots[i].expiry > now) {
            continue;
        }
        void (*fn)(void *) = slots[i].fn;
        void *arg = slots[i].arg;

        slots[i].used = 0;              /* cleared first: the callback may arm one */
        slots[i].expiry = 0;
        if (fn != NULL) {
            fn(arg);
        }
    }
    return ticked;
}

int timer_add_ns(u64 ns, void (*fn)(void *), void *arg)
{
    if (!timer_ready || fn == NULL) {
        return -1;
    }

    sched_lock();
    int slot = -1;
    for (int i = 0; i < TIMER_SLOTS; i++) {
        if (!slots[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        sched_unlock();
        return -1;                      /* every slot taken */
    }
    slots[slot].expiry = now_ns() + (ns != 0 ? ns : 1);
    slots[slot].fn = fn;
    slots[slot].arg = arg;
    slots[slot].used = 1;
    sched_unlock();

    timer_rearm();
    return slot;
}

int timer_add_ms(u64 ms, void (*fn)(void *), void *arg)
{
    if (ms > 0xFFFFFFFFULL) {
        ms = 0xFFFFFFFFULL;
    }
    return timer_add_ns(ms * 1000000ULL, fn, arg);
}

int timer_cancel(int slot)
{
    if (slot < 0 || slot >= TIMER_SLOTS) {
        return -1;
    }

    sched_lock();
    int was = slots[slot].used;
    slots[slot].used = 0;
    slots[slot].expiry = 0;
    slots[slot].fn = NULL;
    slots[slot].arg = NULL;
    sched_unlock();

    timer_rearm();
    return was;
}

int timer_pending(void)
{
    int n = 0;
    for (int i = 0; i < TIMER_SLOTS; i++) {
        n += slots[i].used != 0;
    }
    return n;
}

/*
 * Start the clock. The one-shot is armed here for the first time, and from then
 * on the interrupt re-arms it: the tick is no longer a device that runs by
 * itself, it is a deadline like any other, which is why a machine whose APIC has
 * no timer falls back to the 8259's periodic tick and this returns an error.
 */
int timer_init(void)
{
    tick_period = 1000000000ULL / LM_TIMER_HZ;

    if (now_ns() == 0) {
        kputs("[timer] FAIL no counter to keep time on\r\n");
        return -1;
    }

    tick_deadline = now_ns() + tick_period;
    timer_ready = 1;
    timer_rearm();
    return 0;
}

/*
 * The self-test arms four deadlines - 1, 2, 4 and 8 ms - and measures how late
 * each was delivered. The number that matters is that the lateness does not grow
 * with the interval: a deadline that arrives a hundredth of the way into a
 * one-millisecond wait would be a device that rounds to something, and one whose
 * error stays put is a device that is delivering an instant. What the error
 * actually is belongs to the machine - an emulated one delivers interrupts from
 * its main loop and has a floor of a few hundred microseconds - so it is
 * reported rather than asserted on, and the comparison that means something is
 * against the tick, which could never do better than ten milliseconds.
 */
static volatile int selftest_fired;
static volatile u64 selftest_at;

static void selftest_cb(void *arg)
{
    (void)arg;
    selftest_at = now_ns();
    selftest_fired = 1;
}

int timer_selftest(void)
{
    if (!timer_ready) {
        kputs("[timer] hrtimer=unavailable\r\n");
        return -1;
    }

    u64 late_max = 0;
    u64 worst_ms = 0;

    for (u32 ms = 1; ms <= 8; ms *= 2) {            /* 1, 2, 4 and 8 ms */
        selftest_fired = 0;
        u64 start = now_ns();
        int slot = timer_add_ms(ms, selftest_cb, (void *)(unsigned long)ms);
        if (slot < 0) {
            kputs("[timer] FAIL the queue would not take a deadline\r\n");
            return -1;
        }

        u64 spins = 0;
        while (!selftest_fired && spins++ < 200000000ULL) {
            __asm__ volatile("sti; hlt");
        }
        if (!selftest_fired) {
            timer_cancel(slot);
            kputs("[timer] FAIL a deadline never arrived\r\n");
            return -1;
        }

        u64 elapsed_ms = (selftest_at - start) / 1000000ULL;
        u64 late_us = 0;
        if (selftest_at > start + ms * 1000000ULL) {
            late_us = (selftest_at - (start + ms * 1000000ULL)) / 1000ULL;
        }
        if (late_us > late_max) {
            late_max = late_us;
        }
        if (elapsed_ms > worst_ms) {
            worst_ms = elapsed_ms;
        }
    }

    /* Interrupts go back off: the nucleus has always run them masked, and the
     * iretq into the first task is what turns them on. Leaving them on here would
     * change the timing of everything that boots after this point. */
    __asm__ volatile("cli");

    kputs("[timer] hrtimer=ok");
    kputs(" slots=");
    kput_dec(TIMER_SLOTS);
    kputs(" worst=");
    kput_dec(worst_ms);
    kputs("ms late=");
    kput_dec(late_max);
    kputs("us source=");
    kputs(hpet_hz() != 0 ? "hpet" : "tsc");
    kputs("\r\n");
    return 0;
}
