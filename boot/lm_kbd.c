/*
 * lm_kbd.c - the PS/2 keyboard
 *
 * QEMU and real hardware both answer on the i8042: port 0x64 is the status
 * register (bit 0: a byte is waiting, bit 5: it came from the mouse) and 0x60 the
 * data. The controller's default is scancode set 1, one byte per key press with
 * 0x80 added for the release, so a shifted key needs the state of the shift key
 * to be tracked from those release codes.
 *
 * Polling, not interrupts, for the same reason the UART is polled: kgetc is
 * called from a syscall that can afford to look, and a ring buffer with a wake-up
 * path is the sort of thing to add when something actually waits on it.
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
}

int kbd_selftest(void)
{
    u8 status = inb_port(KBD_STATUS);
    if (status == 0xFF) {
        kputs("[kbd] FAIL the controller does not answer\r\n");
        return -1;
    }
    /* The mouse's bytes arrive on the same port; kgetc hands them to the mouse
     * driver and never reads one as a keystroke. */
    kputs("[kbd] ps/2 controller ready kbd=ok\r\n");
    return 0;
}

int kbd_getc(void)
{
    for (int guard = 0; guard < 16; guard++) {
        u8 status = inb_port(KBD_STATUS);
        if ((status & 0x01) == 0) {
            return -1;                      /* nothing waiting */
        }
        u8 data = inb_port(KBD_DATA);
        if ((status & 0x20) != 0) {
            mouse_feed(data);               /* it was the mouse */
            continue;
        }
        int c = translate(data);
        if (c != 0) {
            return c;
        }
    }
    return -1;
}
