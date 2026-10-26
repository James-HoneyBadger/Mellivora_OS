/*
 * string.c - the freestanding runtime
 *
 * Four functions here, and they are not decorative: with optimization on, the
 * compiler itself emits calls to memcpy, memmove, memset and memcmp for
 * structure copies and array initialisation, so a C subsystem that does not
 * provide them cannot be linked at all. The rest are what the shell-facing
 * code in this tree keeps needing.
 *
 * Written in plain C rather than inline assembly: correctness first, and the
 * kernel's own hot copies (block I/O, page clears) are `rep movsd`/`rep stosd`
 * in the assembly modules, not here.
 */
#include <mv/string.h>

void *memcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    while (n--)
        *d++ = *s++;
    return dst;
}

/*
 * Overlap-safe in both directions. The forward loop would corrupt a
 * destination that starts above the source, so that case copies backwards.
 */
void *memmove(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, u32 n)
{
    u8 *d = (u8 *)dst;

    while (n--)
        *d++ = (u8)c;
    return dst;
}

int memcmp(const void *a, const void *b, u32 n)
{
    const u8 *p = (const u8 *)a;
    const u8 *q = (const u8 *)b;

    while (n--) {
        if (*p != *q)
            return (int)*p - (int)*q;
        p++;
        q++;
    }
    return 0;
}

u32 strlen(const char *s)
{
    u32 n = 0;

    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, u32 n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int)(u8)*a - (int)(u8)*b;
}
