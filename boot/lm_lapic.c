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
#define LAPIC_ICR_LOW       0x300
#define LAPIC_ICR_HIGH      0x310

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

/*
 * An inter-processor interrupt: the destination in the low register's top byte,
 * the delivery mode in bits 8-10, and for a start-up the page number in bits 0-7.
 * Delivery has to finish before the next one is sent, which is what bit 12 says.
 */
int lapic_send_ipi(u32 dest, u32 mode, u8 vector)
{
    if (lapic == NULL) {
        return -1;
    }

    u32 command = ((u32)mode << 8) | (u32)vector | ((dest & 0xFF) << 24);
    if (mode == LAPIC_IPI_INIT) {
        /* An INIT is a level-triggered assert: bit 15 says the trigger is level
         * and bit 14 that the level is asserted. Sending it edge-triggered is
         * what a lot of example code does and what the architecture does not. */
        command |= (1u << 15) | (1u << 14);
    }

    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, command);
    for (int spin = 0; spin < 1000000; spin++) {
        if ((lapic_read(LAPIC_ICR_LOW) & (1u << 12)) == 0) {   /* delivered */
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
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
/* Acknowledge the interrupt just taken. Called from the timer handler before
 * anything that might be preempted. */
void lapic_eoi(void)
{
    if (lapic != NULL) {
        lapic_write(LAPIC_EOI, 0);
    }
}

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

    /* Put the 8259's line back the way it was found. On a machine whose APIC can
     * be enabled, the PIC reaches the CPU through this pin in ExtINT mode, so a
     * masked LINT0 means every ISA interrupt - the timer among them - stops.
     * LINT1 stays masked; it is the NMI pin and nothing here wants one. */
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LINT0_EXTINT);

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

/*---------------------------------------
 * The tick
 *
 * The APIC timer counts at the bus clock divided by the divider, which the
 * firmware does not tell you and which no two machines agree on - so it is
 * measured against something known. The PIT's channel 2 is that something: it is
 * a spare counter nothing else in this port uses, its output is visible as bit 5
 * of port 0x61, and a one-shot there is an accurate ten milliseconds. Count the
 * APIC down from its maximum until the PIT's output goes high and the divisor
 * falls out of the arithmetic.
 *---------------------------------------*/

/* How many APIC ticks pass in one PIT tick (10 ms at 100 Hz). Zero if the
 * machine would not answer. The interval comes from the PIT's spare channel,
 * which is the one clock at boot that does not need interrupts. */
static u32 measure_per_tick(void)
{
    lapic_write(LAPIC_TIMER_DIVIDE, 0);         /* divide by two */
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFFu);

    pit_delay_ms(1000 / LM_TIMER_HZ);

    u32 left = lapic_read(LAPIC_TIMER_CURRENT);
    lapic_write(LAPIC_TIMER_INIT, 0);
    return 0xFFFFFFFFu - left;
}

int lapic_timer_start(void)
{
    if (lapic == NULL) {
        return -1;
    }

    u32 per_tick = measure_per_tick();
    if (per_tick < 1000) {
        kputs("[lapic] FAIL the timer would not calibrate\r\n");
        return -1;
    }

    /* The unit has to be on for its timer to run, and the spurious vector has to
     * point at a gate that exists - a spurious interrupt is not acknowledged, so
     * it must simply return. */
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_VECTOR | TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_DIVIDE, 0);
    lapic_write(LAPIC_TIMER_INIT, per_tick);
    lapic_write(LAPIC_SPURIOUS, SPURIOUS_ENABLE | LAPIC_SPURIOUS_VECTOR);

    /* The 8259 has had it: the PIT keeps counting into a masked controller. */
    pic_mask_all();

    kputs("[lapic] timer per tick=");
    kput_dec(per_tick);
    kputs(" vector=");
    kput_dec(LAPIC_TIMER_VECTOR);
    kputs(" hz=");
    kput_dec(LM_TIMER_HZ);
    kputs(" tick=lapic\r\n");
    return 0;
}
