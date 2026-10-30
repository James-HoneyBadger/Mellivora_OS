/*
 * utf.c - the Unicode boundary (c/include/mv/utf.h, EXEC.md section 5.1)
 *
 * UTF-16 is what the parity target's ABI passes and UTF-8 is what this system
 * stores and prints, so every crossing needs a real conversion. The three
 * things this file gets right, and which a cast or a truncation does not:
 *
 *   * a code point above the BMP becomes a surrogate pair, and comes back as
 *     one code point;
 *   * a lone surrogate is replaced (U+FFFD) rather than emitted as a broken
 *     UTF-8 sequence - the target platform tolerates them in a string, UTF-8
 *     cannot represent them at all;
 *   * a decoder always makes progress, so a malformed byte cannot spin a loop
 *     or run past the terminator.
 */
#include <mv/utf.h>
#include <mv/stdio.h>
#include <mv/syscall.h>

u32 mv_utf8_decode(const char *s, u32 *len)
{
    const unsigned char *p = (const unsigned char *)s;
    u32 cp;
    u32 need;
    u32 i;

    if (p[0] < 0x80) {
        if (len) {
            *len = 1;
        }
        return p[0];
    }

    if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        need = 1;
        if (cp < 0x02) {
            goto malformed;             /* overlong */
        }
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        need = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        need = 3;
    } else {
        goto malformed;
    }

    for (i = 1; i <= need; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            goto malformed;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }

    /* Reject what UTF-8 cannot mean, whatever the bytes say. */
    if ((need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
        cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        goto malformed;
    }
    if (len) {
        *len = need + 1;
    }
    return cp;

malformed:
    if (len) {
        *len = 1;
    }
    return MV_REPLACEMENT;
}

u32 mv_utf8_encode(u32 cp, char *out)
{
    unsigned char *p = (unsigned char *)out;

    if (cp < 0x80) {
        p[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        p[0] = (unsigned char)(0xC0 | (cp >> 6));
        p[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        p[0] = (unsigned char)(0xE0 | (cp >> 12));
        p[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        p[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    p[0] = (unsigned char)(0xF0 | (cp >> 18));
    p[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    p[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    p[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

u32 mv_utf16_len(const char *utf8)
{
    u32 units = 0;
    u32 used;

    while (*utf8 != '\0') {
        u32 cp = mv_utf8_decode(utf8, &used);
        utf8 += used;
        units += (cp > 0xFFFF) ? 2u : 1u;
    }
    return units;
}

u32 mv_utf8_to_utf16(const char *utf8, u16 *out, u32 cap)
{
    u32 written = 0;

    while (*utf8 != '\0') {
        u32 used;
        u32 cp = mv_utf8_decode(utf8, &used);
        utf8 += used;

        if (cp > 0xFFFF) {
            /* A pair needs both halves present or neither is written: a
             * truncated pair is not a character, it is corruption. */
            if (written + 2u >= cap) {
                break;
            }
            cp -= 0x10000;
            out[written++] = (u16)(0xD800 + (cp >> 10));
            out[written++] = (u16)(0xDC00 + (cp & 0x3FF));
        } else {
            if (written + 1u >= cap) {
                break;
            }
            out[written++] = (u16)cp;
        }
    }
    if (cap != 0) {
        out[written] = 0;
    }
    return written;
}

u32 mv_utf16_to_utf8(const u16 *wide, char *out, u32 cap)
{
    u32 written = 0;

    while (*wide != 0) {
        u32 cp = *wide++;
        char buf[MV_UTF8_MAX];
        u32 size;
        u32 i;

        if (cp >= 0xD800 && cp <= 0xDBFF) {
            u32 low = *wide;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                wide++;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else {
                cp = MV_REPLACEMENT;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = MV_REPLACEMENT;
        }

        size = mv_utf8_encode(cp, buf);
        if (written + size >= cap) {
            break;
        }
        for (i = 0; i < size; i++) {
            out[written++] = buf[i];
        }
    }
    if (cap != 0) {
        out[written] = '\0';
    }
    return written;
}

void mv_printw(const u16 *wide)
{
    char buf[128];
    u32 at = 0;

    while (*wide != 0) {
        u32 cp = *wide++;
        char one[MV_UTF8_MAX];
        u32 size;
        u32 i;

        if (cp >= 0xD800 && cp <= 0xDBFF) {
            u32 low = *wide;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                wide++;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else {
                cp = MV_REPLACEMENT;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = MV_REPLACEMENT;
        }

        size = mv_utf8_encode(cp, one);
        if (at + size >= sizeof buf) {
            buf[at] = '\0';
            printf("%s", buf);
            at = 0;
        }
        for (i = 0; i < size; i++) {
            buf[at++] = one[i];
        }
    }
    buf[at] = '\0';
    printf("%s", buf);
}

void mv_putwc(u32 cp)
{
    char buf[MV_UTF8_MAX];
    u32 size = mv_utf8_encode(cp, buf);
    u32 i;

    for (i = 0; i < size; i++) {
        putchar(buf[i]);
    }
}
