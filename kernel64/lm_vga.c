/*
 * lm_vga.c - the VGA text console
 *
 * 80x25 characters at 0xB8000, two bytes each: the character and an attribute
 * (background in the high nibble, foreground in the low one). It is the oldest
 * display on a PC and it is still the one that answers "is this thing alive"
 * without a serial cable, so the console fans out: everything kputc writes goes
 * to COM1 and to this buffer.
 *
 * Text mode gives no way to ask where the cursor is, so it is tracked here and
 * written to the CRTC; the self-test reads it back out of the CRTC to prove the
 * two agree.
 */

#include "lm.h"

/*
 * The screen is reached through the kernel's direct map, not at its physical
 * address: a task's own address space leaves the identity map out on purpose, so
 * 0xB8000 is not there when the console is written from inside a syscall - and a
 * fault while printing a fault is a very short way to lose the machine. The
 * direct map is in every space.
 */
#define VGA_BASE 0xB8000UL
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_DEFAULT_ATTR 0x07

static u16 *const screen = (u16 *)(LM_DIRECT_BASE + VGA_BASE);
static int ready;
static u32 cursor;
static u8 attribute = VGA_DEFAULT_ATTR;

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

static void crtc_write(u8 reg, u8 value)
{
    outb_port(0x3D4, reg);
    outb_port(0x3D5, value);
}

static u8 crtc_read(u8 reg)
{
    outb_port(0x3D4, reg);
    return inb_port(0x3D5);
}

static void push_cursor(void)
{
    crtc_write(0x0E, (u8)(cursor >> 8));
    crtc_write(0x0F, (u8)(cursor & 0xFF));
}

void vga_clear(void)
{
    for (u32 i = 0; i < (u32)VGA_COLS * VGA_ROWS; i++) {
        screen[i] = (u16)(' ' | (attribute << 8));
    }
    cursor = 0;
    push_cursor();
}

void vga_set_color(u8 attr)
{
    attribute = attr;
}

void vga_set_cursor(u32 column, u32 row)
{
    if (column >= VGA_COLS) {
        column = VGA_COLS - 1;
    }
    if (row >= VGA_ROWS) {
        row = VGA_ROWS - 1;
    }
    cursor = row * VGA_COLS + column;
    push_cursor();
}

static void scroll(void)
{
    for (u32 i = 0; i < (u32)(VGA_ROWS - 1) * VGA_COLS; i++) {
        screen[i] = screen[i + VGA_COLS];
    }
    for (u32 i = (u32)(VGA_ROWS - 1) * VGA_COLS; i < (u32)VGA_COLS * VGA_ROWS;
         i++) {
        screen[i] = (u16)(' ' | (attribute << 8));
    }
    cursor -= VGA_COLS;
}

void vga_putc(char c)
{
    if (!ready) {
        return;
    }

    if (c == '\n') {
        cursor = ((cursor / VGA_COLS) + 1) * VGA_COLS;
    } else if (c == '\r') {
        cursor -= cursor % VGA_COLS;
    } else if (c == '\b') {
        if (cursor > 0) {
            cursor--;
            screen[cursor] = (u16)(' ' | (attribute << 8));
        }
    } else if (c == '\x1b') {
        /* The clear sequence the console call sends: swallow it, the screen is
         * handled by vga_clear itself. */
        return;
    } else {
        screen[cursor] = (u16)((u8)c | (attribute << 8));
        cursor++;
    }

    if (cursor >= (u32)VGA_COLS * VGA_ROWS) {
        scroll();
    }
    push_cursor();
}

int vga_selftest(void)
{
    vga_clear();

    /* Write through the driver, then look at the memory the display reads. */
    const char *probe = "vga";
    for (int i = 0; i < 3; i++) {
        vga_putc(probe[i]);
    }
    for (int i = 0; i < 3; i++) {
        u16 cell = screen[i];
        if ((u8)(cell & 0xFF) != (u8)probe[i] ||
            (u8)(cell >> 8) != attribute) {
            kputs("[vga] FAIL text memory\r\n");
            return -1;
        }
    }

    /* The cursor register must agree with where the driver thinks it is. */
    vga_set_cursor(2, 3);
    u32 expected = 3 * VGA_COLS + 2;
    u32 actual = ((u32)crtc_read(0x0E) << 8) | crtc_read(0x0F);
    if (actual != expected) {
        kputs("[vga] FAIL cursor register\r\n");
        return -1;
    }

    vga_set_color(0x0A);
    vga_putc('!');
    if ((u8)(screen[cursor - 1] >> 8) != 0x0A) {
        kputs("[vga] FAIL attribute\r\n");
        return -1;
    }
    vga_set_color(VGA_DEFAULT_ATTR);

    vga_clear();
    if ((u8)(screen[0] & 0xFF) != ' ' || cursor != 0) {
        kputs("[vga] FAIL clear\r\n");
        return -1;
    }

    kputs("[vga] 80x25 at 0xB8000 vga=ok\r\n");
    return 0;
}

void vga_init(void)
{
    /* The buffer is already identity-mapped; the first clear is what makes the
     * screen ours rather than whatever the firmware left. */
    cursor = 0;
    attribute = VGA_DEFAULT_ATTR;
    ready = 1;
    vga_clear();
}

int vga_cursor(void)
{
    return (int)cursor;
}
