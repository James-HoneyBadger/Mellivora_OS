/*
 * lm_spk.c - the PC speaker
 *
 * The oldest sound hardware there is, and the only one every machine has: the
 * 8254's third channel drives a square wave into a gate in the PIT's own latch
 * (port 0x61). Channel 2 is not wired to interrupts, so programming it is just
 * the divisor: 1193182 Hz divided by nothing else gives the output frequency,
 * because the channel counts straight down and toggles the output on every pass
 * (mode 3).
 *
 * There is no way to ask this hardware what it is doing, and no way to hear it
 * from a test, so the boot self-test proves what can be proved: the gate bits in
 * port 0x61 read back what was written to them. That is a real register round
 * trip and it is honest about being one - the rest is a person listening.
 */

#include "lm.h"

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43
#define SPEAKER_GATE 0x61

#define PIT_FREQUENCY 1193182u

static void outb_port(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb_port(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* 0 silences it. Anything above the PIT's own clock cannot be represented. */
void speaker_tone(u32 hz)
{
    if (hz == 0) {
        outb_port(SPEAKER_GATE, (u8)(inb_port(SPEAKER_GATE) & ~0x03));
        return;
    }
    if (hz > PIT_FREQUENCY) {
        hz = PIT_FREQUENCY;
    }

    u32 divisor = PIT_FREQUENCY / hz;
    if (divisor < 2) {
        divisor = 2;
    }

    /* Channel 2, access low byte then high byte, mode 3 (square wave), binary. */
    outb_port(PIT_COMMAND, 0xB6);
    outb_port(PIT_CHANNEL2, (u8)(divisor & 0xFF));
    outb_port(PIT_CHANNEL2, (u8)(divisor >> 8));

    /* Both gate bits: the timer's output and the speaker's own enable. */
    outb_port(SPEAKER_GATE, (u8)(inb_port(SPEAKER_GATE) | 0x03));
}

void speaker_off(void)
{
    speaker_tone(0);
}

/* A tone for a while, then silence. A syscall runs with interrupts off, so the
 * wait opens them again the way SYS_SLEEP does: hlt parks the CPU until the next
 * interrupt, and the tick is the one that moves the clock this loop watches. A
 * spin would never end - the timer could not fire to advance the tick. */
void speaker_beep(u32 hz, u32 ms)
{
    if (hz == 0 || ms == 0) {
        return;
    }

    u64 deadline = cpu_this()->ticks + (ms + (1000 / LM_TIMER_HZ) - 1)
                                     / (1000 / LM_TIMER_HZ);
    speaker_tone(hz);
    while (cpu_this()->ticks < deadline) {
        __asm__ volatile("sti; hlt" ::: "memory");
    }
    speaker_off();
}

int speaker_selftest(void)
{
    u8 before = inb_port(SPEAKER_GATE);

    speaker_tone(440);
    u8 while_on = inb_port(SPEAKER_GATE);
    speaker_off();
    u8 while_off = inb_port(SPEAKER_GATE);

    if ((while_on & 0x03) != 0x03 || (while_off & 0x03) != 0) {
        kputs("[spk] FAIL the speaker gate does not follow the driver\r\n");
        speaker_off();
        return -1;
    }

    /* Leave the gate as it was found; the divisor does not matter while it is
     * closed. */
    outb_port(SPEAKER_GATE, before);
    speaker_off();
    kputs("[spk] pit channel 2, 1193182 Hz clock spk=ok\r\n");
    return 0;
}
