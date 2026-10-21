/*
 * lm_mouse.c - the PS/2 mouse
 *
 * The second device on the same i8042 controller the keyboard uses: 0x64 is
 * still the status register and 0x60 the data, and bit 5 of the status byte
 * says the waiting byte came from the auxiliary port instead of the keyboard.
 * The mouse answers in three-byte packets - flags, delta X, delta Y - where the
 * flags carry the button bits and the sign of each delta, and bit 3 of the
 * flags is always set. That bit is the only thing that tells a packet's first
 * byte from its second, so it is checked on every resync after a lost byte.
 *
 * Polling, like the keyboard and for the same reason: the port is drained from
 * the timer tick a hundred times a second, which is fast enough that the
 * pointer keeps up with a hand and needs no interrupt handler, no IRQ12
 * unmasking and nothing to wake up. The coordinates are text cells, because
 * text mode is the only display this kernel has.
 */

#include "lm.h"

#define I8042_STATUS 0x64
#define I8042_DATA   0x60

#define ST_OBF 0x01             /* a byte is waiting in 0x60 */
#define ST_IBF 0x02             /* the controller is still taking the last one */
#define ST_AUX 0x20             /* ... and it came from the mouse */

#define MOUSE_COLS 80
#define MOUSE_ROWS 25

static u8 inb_port(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outb_port(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* The controller takes a byte only when it says it is ready; a fixed count is
 * enough - if the hardware is not there the loop simply gives up. */
static int wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if ((inb_port(I8042_STATUS) & ST_IBF) == 0) {
            return 0;
        }
    }
    return -1;
}

static int wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if ((inb_port(I8042_STATUS) & ST_OBF) != 0) {
            return 0;
        }
    }
    return -1;
}

static void command(u8 value)
{
    if (wait_write() == 0) {
        outb_port(I8042_STATUS, value);
    }
}

static int mouse_write(u8 value)
{
    if (wait_write() != 0) {
        return -1;
    }
    outb_port(I8042_STATUS, 0xD4);      /* next byte goes to the auxiliary port */
    if (wait_write() != 0) {
        return -1;
    }
    outb_port(I8042_DATA, value);
    return 0;
}

/* Every command the mouse accepts is answered with 0xFA. */
static int expect_ack(void)
{
    if (wait_read() != 0) {
        return -1;
    }
    return inb_port(I8042_DATA) == 0xFA ? 0 : -1;
}

static int present;
static int cursor_x = MOUSE_COLS / 2;
static int cursor_y = MOUSE_ROWS / 2;
static int buttons;
static int clicks;                      /* presses since the last read */
static u8 packet[3];
static int packet_len;

void mouse_init(void)
{
    /* Start in the middle of the screen, where a pointer is out of the way of
     * the prompt but visible. */
    cursor_x = MOUSE_COLS / 2;
    cursor_y = MOUSE_ROWS / 2;
    buttons = 0;
    clicks = 0;
    packet_len = 0;
    present = 0;

    command(0xA8);                      /* enable the auxiliary port */

    /* Clear the auxiliary interrupt in the controller's command byte: this
     * driver polls, and QEMU is happy to deliver IRQ12 anyway once reporting
     * starts. The rest of the byte (the keyboard's clock and translation) is
     * left exactly as the firmware set it. */
    command(0x20);
    if (wait_read() == 0) {
        u8 config = (u8)(inb_port(I8042_DATA) & (u8)~0x02);
        command(0x60);
        if (wait_write() == 0) {
            outb_port(I8042_DATA, config);
        }
    }

    if (mouse_write(0xF6) == 0 && expect_ack() == 0 &&      /* set defaults */
        mouse_write(0xF4) == 0 && expect_ack() == 0) {      /* start reporting */
        present = 1;
    }

    /* Anything the mouse sent while it was being configured is stale. */
    for (int i = 0; i < 32; i++) {
        if ((inb_port(I8042_STATUS) & ST_OBF) == 0) {
            break;
        }
        (void)inb_port(I8042_DATA);
    }
}

int mouse_selftest(void)
{
    if (!present) {
        kputs("[mouse] no auxiliary device mouse=none\r\n");
        return 0;
    }
    kputs("[mouse] ps/2 pointer at ");
    kput_dec((u64)cursor_x);
    kputc(',');
    kput_dec((u64)cursor_y);
    kputs(" mouse=ok\r\n");
    return 0;
}

/*
 * One byte of a packet. The first byte carries the sign bits, so a byte that
 * arrives where a first byte belongs and lacks bit 3 is a leftover from an
 * interrupted packet: dropping it puts the stream back in step.
 */
void mouse_feed(u8 byte)
{
    if (packet_len == 0 && (byte & 0x08) == 0) {
        return;
    }
    packet[packet_len++] = byte;
    if (packet_len < 3) {
        return;
    }
    packet_len = 0;

    u8 flags = packet[0];
    if ((flags & 0xC0) != 0) {
        return;                         /* both overflow bits: the deltas lie */
    }

    int dx = (int)packet[1];
    int dy = (int)packet[2];
    if ((flags & 0x10) != 0) {
        dx -= 256;
    }
    if ((flags & 0x20) != 0) {
        dy -= 256;
    }

    int now = flags & 0x07;
    clicks |= now & ~buttons;           /* a press survives a slow reader */
    buttons = now;

    cursor_x += dx;
    cursor_y -= dy;                     /* PS/2 counts up, the screen down */
    if (cursor_x < 0) {
        cursor_x = 0;
    }
    if (cursor_x >= MOUSE_COLS) {
        cursor_x = MOUSE_COLS - 1;
    }
    if (cursor_y < 0) {
        cursor_y = 0;
    }
    if (cursor_y >= MOUSE_ROWS) {
        cursor_y = MOUSE_ROWS - 1;
    }
}

/* Drain the auxiliary port, stopping at the first byte that is a keystroke:
 * that one belongs to kbd_getc. */
void mouse_poll(void)
{
    for (int guard = 0; guard < 32; guard++) {
        u8 status = inb_port(I8042_STATUS);
        if ((status & ST_OBF) == 0 || (status & ST_AUX) == 0) {
            return;
        }
        mouse_feed(inb_port(I8042_DATA));
    }
}

int mouse_x(void)
{
    return cursor_x;
}

int mouse_y(void)
{
    return cursor_y;
}

int mouse_buttons(void)
{
    return buttons;
}

/* The press latch is cleared by reading it, so a program that asks once a
 * second still sees every click the way a program that asks every millisecond
 * does. */
int mouse_clicks(void)
{
    int value = clicks;
    clicks = 0;
    return value;
}
