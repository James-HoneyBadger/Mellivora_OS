/*
 * lm_irq.c - the 8259 PIC and the PIT for the 64-bit nucleus
 *
 * Roadmap P2's timers step. The IDT already delivers vectors 32..47 to
 * lm_irq_dispatch; this file puts the two legacy chips behind them on the
 * vector numbers the IDT expects and starts a periodic tick.
 *
 * Interrupts are enabled only in ring 3. The kernel runs with IF clear
 * everywhere - the SYSCALL entry masks it and every gate is an interrupt gate -
 * so a tick can never land in the middle of the kernel, and the timer is a
 * proof that the TSS stack works rather than a scheduler yet: each tick arrives
 * from ring 3, the CPU switches to TSS.RSP0, and the handler records that it
 * did. The scheduler that will use these ticks to preempt arrives next.
 */

#include "lm.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1
#define PIT_CMD    0x43
#define PIT_CH0    0x40

/* 8254 input clock; the divisor has to fit in 16 bits. */
#define PIT_HZ 1193182U

static void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void irq_init(void)
{
    /* ICW1: begin initialisation, ICW4 follows. ICW2: vectors - the remap is
     * what keeps IRQ0 off the CPU's #DE.. vectors. ICW3: the slave is on IRQ2
     * of the master. ICW4: 8086 mode, normal EOI. */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, LM_IRQ_FIRST);
    outb(PIC2_DATA, LM_IRQ_FIRST + 8);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Everything masked but the timer. A spurious IRQ7 the master invents is
     * still routed and acknowledged harmlessly. */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);

    /* Channel 0, mode 2 (rate generator), LSB then MSB of the divisor. */
    u32 divisor = PIT_HZ / LM_TIMER_HZ;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;
    }
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0, (u8)(divisor & 0xFF));
    outb(PIT_CH0, (u8)(divisor >> 8));
}

/*
 * One tick's bookkeeping, whichever unit delivered it: the count the rest of the
 * kernel reads, how much of it was ring 3, and whether the handler ran on the
 * task's own kernel stack (which is what proves the TSS RSP0 was right).
 */
static void tick_account(struct lm_fault_frame *f, int from_apic)
{
    struct lm_cpu *cpu = cpu_this();
    u64 rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    cpu->ticks++;
    if (from_apic) {
        cpu->apic_ticks++;
    }
    if ((f->cs & 3) == 3) {
        cpu->user_ticks++;
    }
    if (rsp <= cpu->kstack_top && rsp > cpu->kstack_base) {
        cpu->tick_stack++;
    }
}

/*
 * Called from the common entry path for vectors 32..47. The tick is the
 * scheduler's switch point: sched_tick() returns the frame to IRETQ into, which
 * is this task's own frame unless another one was chosen.
 */
struct lm_fault_frame *lm_irq_dispatch(struct lm_fault_frame *f)
{
    int irq = (int)f->vec - LM_IRQ_FIRST;

    if (irq == 0) {
        tick_account(f, 0);
    }

    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);           /* slave first, when it was involved */
    }
    outb(PIC1_CMD, 0x20);

    return irq == 0 ? sched_tick(f) : f;
}

/*
 * The same thing from the local APIC, whose acknowledge is a write to its own
 * EOI register rather than the 8259's two ports. Once this is the tick, the
 * 8259 is masked off entirely: two clocks with different phases would double
 * the rate and make every sleep and timeout wrong.
 */
struct lm_fault_frame *lm_apic_timer_dispatch(struct lm_fault_frame *f)
{
    lapic_eoi();
    tick_account(f, 1);
    return sched_tick(f);
}

/* Every line masked: the PIT keeps counting but nothing it says is delivered. */
void pic_mask_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
