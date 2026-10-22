/*
 * lm_hpet.c - a clock that is not the PIT
 *
 * The 8254 counts at 1.19 MHz and its tick is the scheduler's unit: a hundred
 * intervals a second, ten milliseconds each. That is the right unit for deciding
 * who runs next and far too coarse for anything else - measuring how long a disk
 * read took, spacing a start-up sequence, waiting half a millisecond. The HPET
 * exists for exactly that: a free-running 64-bit counter whose rate the firmware
 * publishes in the ACPI table, with nanosecond resolution and no interrupt
 * involved.
 *
 * The time-stamp counter is the other one, and it is free: every core has it,
 * nothing has to be mapped, and `rdtsc` is a couple of cycles. What it does not
 * come with is a rate - "invariant TSC" says it ticks at a constant rate, not
 * what that rate is - so it has to be measured, and the HPET is what measures it.
 */

#include "lm.h"

/* Register offsets from the block's base. */
#define HPET_CAPABILITIES 0x000
#define HPET_CONFIG       0x010
#define HPET_MAIN_COUNTER 0x0F0

#define HPET_ENABLE       (1u << 0)

#define FEMTOSECONDS_PER_SECOND 1000000000000000ULL

static volatile u8 *hpet;
static u64 counter_hz;
static u32 period_fs;
static u64 calibrated_tsc_hz;

static u64 read64(u32 offset)
{
    return *(volatile u64 *)(hpet + offset);
}

static void write64(u32 offset, u64 value)
{
    *(volatile u64 *)(hpet + offset) = value;
}

u64 hpet_counter(void)
{
    return hpet != NULL ? read64(HPET_MAIN_COUNTER) : 0;
}

u32 hpet_hz(void)
{
    return (u32)counter_hz;
}

u64 tsc_counter(void)
{
    u32 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

u64 tsc_hz(void)
{
    return calibrated_tsc_hz;
}

int hpet_init(void)
{
    u64 base = acpi_hpet_address();
    if (base == 0) {
        kputs("[hpet] no ACPI table for it hpet=none\r\n");
        return -1;
    }

    if (vmm_map(LM_MMIO_PAGE(LM_MMIO_HPET), base & ~0xFFFULL,
                VMM_W | VMM_PCD | VMM_NX) != 0) {
        kputs("[hpet] FAIL the register page would not map\r\n");
        return -1;
    }
    hpet = (volatile u8 *)(unsigned long)LM_MMIO_PAGE(LM_MMIO_HPET);

    u64 caps = read64(HPET_CAPABILITIES);
    period_fs = (u32)(caps >> 32);
    if (period_fs == 0 || period_fs > 100000000u) {
        kputs("[hpet] FAIL its clock period is nonsense\r\n");
        hpet = NULL;
        return -1;
    }
    counter_hz = FEMTOSECONDS_PER_SECOND / period_fs;

    /* The block starts counting when it is told to. Nothing here uses its
     * comparators or its interrupts: the counter is the whole point. */
    write64(HPET_CONFIG, read64(HPET_CONFIG) | HPET_ENABLE);

    kputs("[hpet] base=");
    kput_hex(base, 8);
    kputs(" hz=");
    kput_dec(counter_hz);
    kputs(" timers=");
    kput_dec(((caps >> 8) & 0x1F) + 1);
    kputs(" bits=");
    kput_dec(((caps >> 13) & 1) != 0 ? 64 : 32);
    kputs("\r\n");
    return 0;
}

/* A wait with a resolution the tick cannot give. */
void hpet_delay_us(u32 us)
{
    if (hpet == NULL) {
        pit_delay_ms(1);
        return;
    }

    u64 ticks = ((u64)us * counter_hz) / 1000000ULL;
    if (ticks == 0) {
        ticks = 1;
    }
    u64 start = read64(HPET_MAIN_COUNTER);
    while (read64(HPET_MAIN_COUNTER) - start < ticks) {
        __asm__ volatile("pause" ::: "memory");
    }
}

/*
 * The self-test measures the PIT's ten milliseconds with both counters. They
 * should agree to within a few percent: the PIT is a rounded divisor of a
 * nominal 1.193182 MHz, the HPET is exact, and a machine where they disagree by
 * more than that is a machine where one of the two is being read wrongly.
 */
int hpet_selftest(void)
{
    if (hpet == NULL) {
        return 0;
    }

    u32 before = (u32)hpet_counter();
    u64 tsc_before = tsc_counter();
    pit_delay_ms(1000 / LM_TIMER_HZ);
    u64 hpet_delta = hpet_counter() - before;
    u64 tsc_delta = tsc_counter() - tsc_before;

    u64 measured_us = (hpet_delta * (u64)period_fs) / 1000000000ULL;
    u64 expected_us = 1000000ULL / LM_TIMER_HZ;
    calibrated_tsc_hz = tsc_delta * LM_TIMER_HZ;

    kputs("[hpet] a tick measures ");
    kput_dec(measured_us);
    kputs("us (the pit says ");
    kput_dec(expected_us);
    kputs("us), tsc=");
    kput_dec(calibrated_tsc_hz / 1000000ULL);
    kputs("MHz");

    /* Ten percent either way: generous for the PIT's own rounding, tight enough
     * that a factor of ten or a wrong period cannot pass. */
    u64 tolerance = expected_us / 10;
    if (measured_us + tolerance < expected_us ||
        measured_us > expected_us + tolerance) {
        kputs(" FAIL the two clocks disagree\r\n");
        return -1;
    }
    if (calibrated_tsc_hz < 100000000ULL) {
        kputs(" FAIL the tsc does not count\r\n");
        return -1;
    }

    /* And the other direction: a wait this driver was asked for, measured by the
     * same counter. Half a millisecond is a length the 10 ms tick cannot even
     * express, which is the reason the HPET is here. */
    u32 wait_start = (u32)hpet_counter();
    hpet_delay_us(500);
    u64 waited_us = ((hpet_counter() - wait_start) * (u64)period_fs) / 1000000000ULL;
    kputs(" waited=");
    kput_dec(waited_us);
    kputs("us");
    if (waited_us < 400 || waited_us > 600) {
        kputs(" FAIL a 500us wait is not 500us\r\n");
        return -1;
    }

    kputs(" hpet=ok\r\n");
    return 0;
}
