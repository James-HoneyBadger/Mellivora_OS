/*
 * lm_lapic.c - the local APIC
 *
 * Every CPU in a machine with more than one of them carries one of these: a
 * page of registers at a physical address the firmware names (0xFEE00000 as a
 * rule) that does three things this port is about to need - it counts down a
 * timer of its own, it carries inter-processor interrupts, and it is where a
 * driver acknowledges an interrupt it took.
 *
 * This step proves the page is there and that the timer inside it counts, and
 * then leaves it alone. That is deliberate: an interrupt from the APIC needs a
 * gate in the IDT for the vector it would deliver, and nothing above the 8259's
 * sixteen vectors exists yet. So the test turns the unit on with interrupts
 * still off at boot, watches the timer count down, and turns it off again - and
 * the tick stays on the 8254 until the next step gives it a vector.
 */

#include "lm.h"

#define LAPIC_ID            0x020
#define LAPIC_VERSION       0x030
#define LAPIC_TPR           0x080
#define LAPIC_EOI           0x0B0
#define LAPIC_SPURIOUS      0x0F0
#define LAPIC_LVT_TIMER     0x320
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERROR     0x370
#define LAPIC_TIMER_INIT    0x380
#define LAPIC_TIMER_CURRENT 0x390
#define LAPIC_TIMER_DIVIDE  0x3E0

#define SPURIOUS_ENABLE     (1u << 8)
#define LVT_MASKED          (1u << 16)
#define TIMER_PERIODIC      (1u << 17)
#define LINT0_EXTINT        0x00000700u /* delivery mode ExtINT, unmasked */

static volatile u32 *lapic;
static u32 apic_id;
static u32 apic_version;

static u32 lapic_read(u32 offset)
{
    return lapic[offset / 4];
}

static void lapic_write(u32 offset, u32 value)
{
    lapic[offset / 4] = value;
    (void)lapic_read(LAPIC_ID);         /* make the store land before the next */
}

int lapic_init(void)
{
    u64 base = acpi_lapic_address();
    if (base == 0) {
        base = 0xFEE00000ULL;           /* the architectural default */
    }

    if (vmm_map(LM_MMIO_PAGE(LM_MMIO_LAPIC), base,
                VMM_W | VMM_PCD | VMM_NX) != 0) {
        kputs("[lapic] FAIL the register page would not map\r\n");
        return -1;
    }
    lapic = (volatile u32 *)(unsigned long)LM_MMIO_PAGE(LM_MMIO_LAPIC);

    apic_id = lapic_read(LAPIC_ID) >> 24;
    apic_version = lapic_read(LAPIC_VERSION) & 0xFF;
    return 0;
}

u32 lapic_id(void)
{
    return apic_id;
}

/* On, count, off. Interrupts are off at boot, so nothing can arrive from the
 * unit while it is enabled - which is the whole reason this can be done before
 * there is a vector to deliver. */
int lapic_selftest(void)
{
    if (lapic == NULL) {
        kputs("[lapic] no local APIC lapic=none\r\n");
        return 0;
    }

    kputs("[lapic] id=");
    kput_dec(apic_id);
    kputs(" version=");
    kput_hex(apic_version, 2);

    /* Everything masked, then the unit itself switched on: spurious vector 0xFF
     * is a placeholder because nothing is unmasked to use it. */
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SPURIOUS, SPURIOUS_ENABLE | 0xFF);

    /* Divide by two and start the counter; it only runs while the unit is
     * enabled, which is why this comes after the write above. */
    lapic_write(LAPIC_TIMER_DIVIDE, 0);
    lapic_write(LAPIC_TIMER_INIT, 1000000);

    u32 first = lapic_read(LAPIC_TIMER_CURRENT);
    int moved = 0;
    for (int spin = 0; spin < 1000000 && !moved; spin++) {
        __asm__ volatile("pause");
        moved = lapic_read(LAPIC_TIMER_CURRENT) != first;
    }

    u32 last = lapic_read(LAPIC_TIMER_CURRENT);
    lapic_write(LAPIC_TIMER_INIT, 0);
    lapic_write(LAPIC_EOI, 0);

    /* Put the 8259's line back the way it was found. On a machine whose APIC can
     * be enabled, the PIC reaches the CPU through this pin in ExtINT mode, so a
     * masked LINT0 means every ISA interrupt - the timer among them - stops.
     * LINT1 stays masked; it is the NMI pin and nothing here wants one. */
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LINT0_EXTINT);
    lapic_write(LAPIC_SPURIOUS, 0);     /* off again until it has a vector */

    kputs(" count=");
    kput_dec(first);
    kputs("->");
    kput_dec(last);
    if (!moved) {
        kputs(" FAIL the timer never counted\r\n");
        return -1;
    }
    kputs(" lapic=ok\r\n");
    return 0;
}
