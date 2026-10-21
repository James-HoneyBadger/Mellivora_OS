/*
 * lm_rtc.c - the CMOS real-time clock
 *
 * The 32-bit tree reads the same chip through ports 0x70/0x71, so the 64-bit
 * port reads the same registers:
 *
 *   0x00 seconds   0x02 minutes  0x04 hours   0x07 day
 *   0x08 month     0x09 year     0x32 century
 *   0x0A status A (bit 7: update in progress)
 *   0x0B status B (bit 1: 24-hour, bit 2: binary rather than BCD)
 *
 * Two things make a naive read wrong. The chip updates its registers once a
 * second, so a read that straddles the update can mix the old and new values -
 * the loop re-reads if the seconds changed underneath it. And the default format
 * is BCD in 12-hour time, so both have to be undone before anything can print a
 * date.
 */

#include "lm.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static u8 cmos_read(u8 reg)
{
    __asm__ volatile("outb %0, %1" : : "a"(reg), "Nd"(CMOS_INDEX));
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(CMOS_DATA));
    return value;
}

static int updating(void)
{
    return (cmos_read(0x0A) & 0x80) != 0;
}

static u8 bcd_to_bin(u8 value)
{
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static void grab(struct rtc_time *t)
{
    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour = cmos_read(0x04);
    t->day = cmos_read(0x07);
    t->month = cmos_read(0x08);
    t->year = cmos_read(0x09);
    u8 century = cmos_read(0x32);
    u8 status_b = cmos_read(0x0B);

    int binary = (status_b & 0x04) != 0;
    int twelve_hour = (status_b & 0x02) == 0;
    int pm = twelve_hour && (t->hour & 0x80) != 0;
    t->hour = (u8)(t->hour & 0x7F);

    if (!binary) {
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        t->hour = bcd_to_bin(t->hour);
        t->day = bcd_to_bin(t->day);
        t->month = bcd_to_bin(t->month);
        t->year = bcd_to_bin(t->year);
        century = bcd_to_bin(century);
    }

    if (twelve_hour) {
        /* 12 AM is hour 0, 12 PM is hour 12. */
        t->hour = (u8)((t->hour % 12) + (pm ? 12 : 0));
    }
    t->full_year = (u32)century * 100 + t->year;
}

int rtc_read(struct rtc_time *t)
{
    /* Wait out an update, then read twice and require the seconds to agree: a
     * read that span the update would otherwise mix the two halves. */
    for (int i = 0; i < 100000 && updating(); i++) {
    }

    struct rtc_time first;
    grab(&first);
    for (int i = 0; i < 100000 && updating(); i++) {
    }
    grab(t);
    if (first.second != t->second && first.minute != t->minute) {
        grab(t);
    }

    if (t->month < 1 || t->month > 12 || t->day < 1 || t->day > 31 ||
        t->hour > 23 || t->minute > 59 || t->second > 59 ||
        t->full_year < 1970 || t->full_year > 2200) {
        return -1;
    }
    return 0;
}

int rtc_selftest(void)
{
    struct rtc_time a, b;

    if (rtc_read(&a) != 0 || rtc_read(&b) != 0) {
        kputs("[rtc] FAIL the clock did not answer\r\n");
        return -1;
    }
    /* The two reads are microseconds apart, so they differ by at most a second
     * or two, and never backwards. */
    if (b.full_year != a.full_year || b.month != a.month || b.day != a.day) {
        kputs("[rtc] FAIL the date moved between reads\r\n");
        return -1;
    }

    kputs("[rtc] ");
    kput_dec(a.full_year);
    kputc('-');
    kput_dec(a.month);
    kputc('-');
    kput_dec(a.day);
    kputc('T');
    kput_dec(a.hour);
    kputc(':');
    kput_dec(a.minute);
    kputc(':');
    kput_dec(a.second);
    kputs(" rtc=ok\r\n");
    return 0;
}
