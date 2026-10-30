/*
 * stdio.c - one formatter, two sinks
 *
 * `snprintf` copies into a caller's buffer and `printf` writes to the console;
 * both are the same code with a different sink, which is what keeps the two
 * from disagreeing about how a number is rendered. The console sink turns
 * '\n' into "\r\n" because the serial line and the VGA text buffer both want
 * the carriage return; a buffer gets exactly the bytes the format asked for,
 * so a program that writes its own file never finds stray CRs in it.
 *
 * The console is reached through SYS_WRITE on fd 1 and falls back to SYS_PRINT
 * when that descriptor is not open - a program launched without standard
 * handles still prints, which is what every other program in this tree relies
 * on.
 */
#include <mv/stdio.h>
#include <mv/string.h>
#include <mv/syscall.h>

#define OUT_CHUNK 128

typedef struct {
    char *buf;          /* destination, or NULL for the console */
    u32   cap;          /* bytes the buffer can hold, including the NUL */
    u32   len;          /* bytes the format produced, truncation included */
    char  tmp[OUT_CHUNK];
    u32   used;
} out_t;

/* The console, and the one place '\n' becomes "\r\n". */
static void console_write(const char *s, u32 n)
{
    char crlf[OUT_CHUNK * 2];
    u32  i;
    u32  k = 0;

    for (i = 0; i < n; i++) {
        if (s[i] == '\n' && k + 1 < sizeof(crlf)) {
            crlf[k++] = '\r';
            crlf[k++] = '\n';
        } else if (k < sizeof(crlf) - 1) {
            crlf[k++] = s[i];
        }
    }
    crlf[k] = '\0';

    if (mv_write(1, crlf, k) != (i32)k) {
        /* No fd 1: the legacy console call is still there. */
        mv_print(crlf);
    }
}

static void out_flush(out_t *o)
{
    if (o->used != 0) {
        console_write(o->tmp, o->used);
        o->used = 0;
    }
}

static void out_char(out_t *o, char c)
{
    if (o->buf != NULL) {
        if (o->len + 1 < o->cap)
            o->buf[o->len] = c;
        o->len++;
        return;
    }
    o->tmp[o->used++] = c;
    if (o->used == OUT_CHUNK)
        out_flush(o);
}

static void out_text(out_t *o, const char *s)
{
    while (*s != '\0')
        out_char(o, *s++);
}

/* One number: sign, digits, and the padding the conversion asked for. Padding
 * with '0' goes between the sign and the digits, padding with spaces goes on
 * the outside, and a left-justified conversion pads on the right. */
static void out_number(out_t *o, u32 magnitude, int negative, u32 base,
                       int upper, u32 width, char pad, int left)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char  tmp[16];
    u32   n = 0;
    u32   shown;
    u32   i;

    do {
        tmp[n++] = digits[magnitude % base];
        magnitude /= base;
    } while (magnitude != 0);

    shown = n + (negative ? 1u : 0u);

    if (!left && pad == ' ' && width > shown)
        for (i = shown; i < width; i++)
            out_char(o, ' ');

    if (negative)
        out_char(o, '-');

    if (!left && pad == '0' && width > shown)
        for (i = shown; i < width; i++)
            out_char(o, '0');

    while (n != 0)
        out_char(o, tmp[--n]);

    if (left && width > shown)
        for (i = shown; i < width; i++)
            out_char(o, ' ');
}

int vsnprintf(char *buf, u32 size, const char *fmt, va_list ap)
{
    out_t o;
    int   written;

    o.buf = buf;
    o.cap = size;
    o.len = 0;
    o.used = 0;

    while (*fmt != '\0') {
        u32  width = 0;
        char pad = ' ';
        int  left = 0;

        if (*fmt != '%') {
            out_char(&o, *fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '%') {
            out_char(&o, '%');
            fmt++;
            continue;
        }
        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (u32)(*fmt - '0');
            fmt++;
        }
        /* Width modifiers this runtime does not honour are consumed, so the
         * conversion that follows is still the right type of value. */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z')
            fmt++;

        switch (*fmt) {
        case 'd':
        case 'i': {
            i32 v = va_arg(ap, i32);
            /* -2147483648 has no positive counterpart in i32, so the
             * magnitude is taken in u32. */
            u32 m = v < 0 ? (u32)(-(i64)v) : (u32)v;
            out_number(&o, m, v < 0, 10, 0, width, pad, left);
            break;
        }
        case 'u':
            out_number(&o, va_arg(ap, u32), 0, 10, 0, width, pad, left);
            break;
        case 'x':
            out_number(&o, va_arg(ap, u32), 0, 16, 0, width, pad, left);
            break;
        case 'X':
            out_number(&o, va_arg(ap, u32), 0, 16, 1, width, pad, left);
            break;
        case 'p':
            out_text(&o, "0x");
            out_number(&o, va_arg(ap, u32), 0, 16, 0, 8, '0', 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            u32         n;
            u32         i;

            if (s == NULL)
                s = "(null)";
            n = strlen(s);
            if (!left)
                for (i = n; i < width; i++)
                    out_char(&o, ' ');
            out_text(&o, s);
            if (left)
                for (i = n; i < width; i++)
                    out_char(&o, ' ');
            break;
        }
        case 'c':
            out_char(&o, (char)va_arg(ap, int));
            break;
        case '\0':
            /* A trailing '%' prints itself rather than reading past the
             * format string for a conversion that is not there. The loop ends
             * because the conversion byte is the terminator. */
            out_char(&o, '%');
            break;
        default:
            out_char(&o, '%');
            out_char(&o, *fmt);
            break;
        }
        if (*fmt != '\0')
            fmt++;
    }

    written = (int)o.len;

    if (o.buf != NULL) {
        if (o.cap != 0) {
            u32 end = o.len < o.cap ? o.len : o.cap - 1;
            o.buf[end] = '\0';
        }
    } else {
        out_flush(&o);
    }
    return written;
}

int snprintf(char *buf, u32 size, const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s)
{
    int n = printf("%s\n", s);
    return n < 0 ? MV_EOF : n;
}

int putchar(int ch)
{
    char c = (char)ch;

    if (mv_putchar(c) < 0)
        return MV_EOF;
    return (u8)c;
}
