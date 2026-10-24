/*
 * lm_kbd.c - the PS/2 keyboard
 *
 * QEMU and real hardware both answer on the i8042: port 0x64 is the status
 * register (bit 0: a byte is waiting, bit 5: it came from the mouse) and 0x60 the
 * data. The controller's default is scancode set 1, one byte per key press with
 * 0x80 added for the release, so a shifted key needs the state of the shift key
 * to be tracked from those release codes.
 *
 * The controller's bytes are collected by an interrupt now that there is one to
 * collect them with: the I/O APIC routes IRQ1 and IRQ12 to this driver, the
 * handler drains the port into a ring of scancodes, and kgetc translates from
 * there. That matters because a keystroke is a fact - polling only reads the port
 * when somebody asks, and between two asks the controller holds exactly one byte.
 * A program that computes for a tenth of a second used to lose every key pressed
 * in it; now they queue.
 *
 * Polling is still the fallback, because a key pressed before interrupts are on
 * (during the boot, or inside a syscall with IF clear) is sitting in the port and
 * nothing else will come for it.
 */

#include "lm.h"

#define KBD_STATUS 0x64
#define KBD_DATA   0x60

static u8 inb_port(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int shift_down;
static int ctrl_down;
static int caps_on;

/* Scancode set 1, make codes. 0 means "not a character". */
static const char unshifted[64] = {
    0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',
    '-',  '=',  '\b', '\t', 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',  'd',  'f',  'g',  'h',
    'j',  'k',  'l',  ';',  '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',  0,    ' ',
};

static const char shifted[64] = {
    0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',
    '_',  '+',  '\b', '\t', 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',  'D',  'F',  'G',  'H',
    'J',  'K',  'L',  ':',  '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',  0,    ' ',
};

static int translate(u8 code)
{
    if (code == 0x2A || code == 0x36) {     /* left and right shift make */
        shift_down = 1;
        return 0;
    }
    if (code == 0xAA || code == 0xB6) {     /* ... and their releases */
        shift_down = 0;
        return 0;
    }
    if (code == 0x1D) {
        ctrl_down = 1;
        return 0;
    }
    if (code == 0x9D) {
        ctrl_down = 0;
        return 0;
    }
    if (code == 0x3A) {
        caps_on = !caps_on;
        return 0;
    }
    if ((code & 0x80) != 0) {
        return 0;                           /* any other release */
    }
    if (code >= 64) {
        return 0;
    }

    char c = shift_down ? shifted[code] : unshifted[code];
    if (c >= 'a' && c <= 'z' && caps_on) {
        c = (char)(c - 'a' + 'A');
    }
    if (ctrl_down && c >= 'a' && c <= 'z') {
        return c - 'a' + 1;                 /* Ctrl-C is 3, as terminals expect */
    }
    return (int)(u8)c;
}

#define KBD_RING_BYTES 64

static u8 ring[KBD_RING_BYTES];
static u64 irq_count;
static u32 ring_head;       /* where the handler writes */
static u32 ring_tail;       /* where kgetc reads */

static void ring_push(u8 code)
{
    u32 next = (ring_head + 1) % KBD_RING_BYTES;
    if (next == ring_tail) {
        return;                         /* full: drop the newest, keep the order */
    }
    ring[ring_head] = code;
    ring_head = next;
}

static int ring_pop(u8 *out)
{
    if (ring_head == ring_tail) {
        return 0;
    }
    *out = ring[ring_tail];
    ring_tail = (ring_tail + 1) % KBD_RING_BYTES;
    return 1;
}

/*
 * The controller's one output port carries both devices, and which one a byte
 * belongs to is bit 5 of the status register. So there is one drain for both
 * interrupts, and it is the only place in the driver that reads the port while
 * interrupts are on.
 */
u64 kbd_irqs(void)
{
    return irq_count;
}

void kbd_irq(void)
{
    irq_count++;

    for (int guard = 0; guard < 32; guard++) {
        u8 status = inb_port(KBD_STATUS);
        if ((status & 0x01) == 0) {
            return;
        }
        u8 data = inb_port(KBD_DATA);
        if ((status & 0x20) != 0) {
            mouse_feed(data);           /* the pointer's packet byte */
        } else {
            ring_push(data);
        }
    }
}

void kbd_init(void)
{
    /* Anything the firmware left in the buffer would be typed into the first
     * program that asks for a character. */
    for (int i = 0; i < 32; i++) {
        if ((inb_port(KBD_STATUS) & 0x01) == 0) {
            break;
        }
        (void)inb_port(KBD_DATA);
    }
    shift_down = 0;
    ctrl_down = 0;
    caps_on = 0;
    ring_head = 0;
    ring_tail = 0;
    irq_count = 0;
}

int kbd_selftest(void)
{
    u8 status = inb_port(KBD_STATUS);
    if (status == 0xFF) {
        kputs("[kbd] FAIL the controller does not answer\r\n");
        return -1;
    }
    /* The pointer's bytes arrive on the same port; the drain hands them to the
     * mouse driver and never reads one as a keystroke. */
    kputs("[kbd] ps/2 controller ready kbd=ok\r\n");
    return 0;
}

int kbd_getc(void)
{
    /* What the handler collected comes first; anything still in the port is
     * drained here for the case where interrupts were off when it arrived. */
    for (int guard = 0; guard < 32; guard++) {
        u8 code;
        if (!ring_pop(&code)) {
            u8 status = inb_port(KBD_STATUS);
            if ((status & 0x01) == 0) {
                return -1;                  /* nothing waiting */
            }
            code = inb_port(KBD_DATA);
            if ((status & 0x20) != 0) {
                mouse_feed(code);           /* it was the mouse */
                continue;
            }
        }
        int c = translate(code);
        if (c != 0) {
            return c;
        }
    }
    return -1;
}
