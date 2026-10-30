/*
 * stdio.h - formatted output for the freestanding runtime
 *
 * There is no file-stream layer yet: `printf` writes to the console and
 * `snprintf` writes to a buffer, and both share one formatter (c/rt/stdio.c).
 * The conversions are the ones a console program actually uses - d, i, u, x,
 * X, s, c, p and %% - with '-' and '0' flags and a width. Precision, floats
 * and the length modifiers are not here: a 32-bit integer is the widest value
 * this tree passes, and a formatter that silently rounds a `long long` into
 * the wrong digits is worse than one that says what it does not do.
 *
 * `printf` returns the number of bytes written, or -1 when the console
 * refused; `snprintf` follows C99 and returns the length the output would
 * have had, so a caller can measure before it writes.
 */
#ifndef MV_STDIO_H
#define MV_STDIO_H

#include <mv/types.h>
#include <stdarg.h>

#define MV_EOF (-1)

int vsnprintf(char *buf, u32 size, const char *fmt, va_list ap);
int snprintf(char *buf, u32 size, const char *fmt, ...);
int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int ch);

#endif /* MV_STDIO_H */
