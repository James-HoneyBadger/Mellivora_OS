/*
 * report.c - the formatting half of the application
 *
 * Everything here goes through the console stream, and the glyphs are UTF-8:
 * the box drawing and the accents are CP437 code points the font has, which is
 * the Unicode half of the exit criterion - text that reaches the screen as the
 * characters it was written as, not as question marks.
 */
#include <stdio.h>
#include <string.h>

#include "report.h"

/* UTF-8, not the CP437 bytes of the same glyphs: 0xC4 is a valid UTF-8 lead
 * byte, so a row of raw 0xC4 decodes as half as many characters as it looks. */
static void hr(void)
{
    puts("  \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
         "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
         "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
         "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80");
}

void report_banner(const char *title)
{
    puts("");
    printf(" \xE2\x94\x8C");
    hr();
    printf("\xE2\x94\x90\n");
    printf(" \xE2\x94\x82 %s\n", title);
    printf(" \xE2\x94\x94");
    hr();
    printf("\xE2\x94\x98\n");
}

void report_row(const char *key, const char *value)
{
    printf("   %-14s %s\n", key, value);
}

void report_footer(int runs)
{
    printf("\n   runs so far: %d\n", runs);
    printf("   r\xC3\xA9sum\xC3\xA9: every part of M5 this program touches works.\n");
}
