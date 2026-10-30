/*
 * unicodedemo.c - Unicode end to end, the way EXEC.md section 5.1 asks for it.
 *
 * The system stores and prints UTF-8; the ABI a program from the parity target
 * uses passes UTF-16. So the interesting cases are the crossings:
 *
 *   * UTF-8 in source and on the console - accented Latin, Greek, a box, an
 *     arrow - each rendered as the glyph the font actually has;
 *   * UTF-8 to UTF-16 and back, including a code point above the BMP, which is
 *     two units going out and one coming back;
 *   * a lone surrogate, which UTF-8 cannot represent and which therefore has
 *     to become U+FFFD instead of a broken sequence;
 *   * a truncated conversion that must not split a surrogate pair.
 *
 * It prints UNICODE_OK only when every one of them holds.
 */
#include <mv/stdio.h>
#include <mv/string.h>
#include <mv/utf.h>

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("UNICODE_FAIL %s\n", what);
        fails++;
    }
}

int main(void)
{
    /* One of each kind, in UTF-8. */
    static const char latin[] = "caf\xC3\xA9 na\xC3\xAFve \xC3\x9C" "ber";
    static const char greek[] = "\xCE\xB1\xCE\xB2\xCE\xB3 \xCF\x80 \xCE\xA3\xCE\xA9";
    static const char box[]   = "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\x90";
    static const char arrow[] = "a \xE2\x86\x92 b";
    static const char smile[] = "\xE2\x98\xBA";

    u16 wide[64];
    char back[64];
    u32 units;
    u32 bytes;
    u32 used;
    u32 cp;

    printf("unicode: %s | %s\n", latin, greek);
    printf("unicode: %s %s %s\n", box, arrow, smile);

    /* Every multi-byte character here is three bytes or fewer. */
    check(mv_utf8_decode(greek, &used) == 0x3B1 && used == 2, "decode alpha");
    check(mv_utf8_decode(box, &used) == 0x250C && used == 3, "decode corner");
    check(mv_utf8_decode(arrow + 2, &used) == 0x2192 && used == 3, "decode arrow");

    /* A malformed sequence is replaced and still consumes one byte. */
    check(mv_utf8_decode("\xC3", &used) == MV_REPLACEMENT && used == 1,
          "truncated sequence");
    check(mv_utf8_decode("\xE2\x94", &used) == MV_REPLACEMENT && used == 1,
          "truncated three-byte sequence");

    /* UTF-8 -> UTF-16 -> UTF-8 is the identity for text the BMP can hold. */
    units = mv_utf16_len(latin);
    check(units == strlen(latin) - 3, "utf16 length counts characters");
    check(mv_utf8_to_utf16(latin, wide, 64) == units, "utf8 to utf16");
    check(wide[units] == 0, "utf16 is terminated");
    bytes = mv_utf16_to_utf8(wide, back, sizeof back);
    check(bytes == strlen(latin), "utf16 to utf8 length");
    check(strcmp(back, latin) == 0, "round trip");

    /* Above the BMP: one code point, two units, and back again. */
    cp = 0x1F600;                       /* grinning face */
    check(mv_utf8_encode(cp, back) == 4, "encode four bytes");
    back[4] = '\0';                     /* encode does not terminate */
    units = mv_utf16_len(back);
    check(units == 2, "a surrogate pair is two units");
    mv_utf8_to_utf16(back, wide, 64);
    check(wide[0] >= 0xD800 && wide[0] <= 0xDBFF, "high surrogate first");
    check(wide[1] >= 0xDC00 && wide[1] <= 0xDFFF, "low surrogate second");
    check(mv_utf16_to_utf8(wide, back, sizeof back) == 4, "pair back to four bytes");
    check(mv_utf8_decode(back, &used) == cp && used == 4, "pair decodes to one point");

    /* A lone surrogate cannot be UTF-8, so it becomes the replacement. */
    wide[0] = 0xD800;
    wide[1] = 0;
    check(mv_utf16_to_utf8(wide, back, sizeof back) == 3, "lone surrogate is three bytes");
    check(mv_utf8_decode(back, &used) == MV_REPLACEMENT, "lone surrogate replaced");

    /* Truncation never splits a pair: a capacity of one unit holds nothing. */
    wide[0] = 0x41;
    mv_utf8_encode(cp, back);
    check(mv_utf8_to_utf16(back, wide, 1) == 0 && wide[0] == 0,
          "no room for a pair writes nothing");

    /* The boundary's own print: a wide string, converted on the way out. */
    {
        static const u16 greeting[] = {
            'w', 'i', 'd', 'e', ':', ' ',
            0x00E9,                     /* Latin small e with acute */
            0x03B1,                     /* Greek alpha */
            0x2500,                     /* box drawing light horizontal */
            0x2192,                     /* rightwards arrow */
            0, 0
        };
        printf("unicode: ");
        mv_printw(greeting);
        mv_putwc('\n');
    }

    if (fails != 0) {
        return 1;
    }
    printf("UNICODE_OK\n");
    return 0;
}
