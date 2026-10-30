/*
 * stdlib.c - conversions, searching, sorting and the process end
 *
 * The heap is c/rt/malloc.c, which is a separate file because a kernel module
 * that only needs strings must not pull in SYS_SBRK. This file is user-only for
 * the same reason: `exit` and `abort` are syscalls, and `strdup` is the heap.
 *
 * `qsort` is a bottom-up merge, not a quicksort. The caller's array is one it
 * owns and the extra memory is a single allocation of the array's size, which
 * for this system's tools (a directory listing, a file's lines) is small. A
 * merge also keeps equal elements in the order they arrived, so `sort` can be
 * told to sort by one column and leave ties in file order - what `sort -k`
 * needs and what a quicksort would quietly not give.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <mv/types.h>
#include <mv/syscall.h>

/* ------------------------------------------------------------------ */
/* Conversions                                                         */
/* ------------------------------------------------------------------ */

static int digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' ||
           *s == '\f') {
        s++;
    }
    return s;
}

long strtol(const char *s, char **endptr, int base)
{
    const char *p = skip_space(s);
    int negative = 0;
    unsigned long value = 0;
    int digits = 0;

    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' &&
        (p[1] == 'x' || p[1] == 'X') && digit_value(p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;                   /* C: a leading 0 means octal */
    } else if (base == 0) {
        base = 10;
    }

    for (;; p++) {
        int d = digit_value(*p);

        if (d >= base)
            break;
        value = value * (unsigned long)base + (unsigned long)d;
        digits++;
    }

    if (endptr != 0)
        *endptr = (char *)(digits == 0 ? s : p);
    if (digits == 0) {
        errno = EINVAL;
        return 0;
    }
    return negative ? -(long)value : (long)value;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)strtol(s, endptr, base);
}

long long strtoll(const char *s, char **endptr, int base)
{
    return (long long)strtol(s, endptr, base);
}

unsigned long long strtoull(const char *s, char **endptr, int base)
{
    return (unsigned long long)strtol(s, endptr, base);
}

long atol(const char *s)
{
    return strtol(s, 0, 10);
}

long long atoll(const char *s)
{
    return (long long)strtol(s, 0, 10);
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

long long llabs(long long v)
{
    return v < 0 ? -v : v;
}

div_t div(int num, int den)
{
    div_t r;

    r.quot = num / den;
    r.rem = num % den;
    return r;
}

ldiv_t ldiv(long num, long den)
{
    ldiv_t r;

    r.quot = num / den;
    r.rem = num % den;
    return r;
}

/* ------------------------------------------------------------------ */
/* Searching and sorting                                               */
/* ------------------------------------------------------------------ */

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*cmp)(const void *, const void *))
{
    const char *lo = (const char *)base;
    size_t n = count;

    while (n != 0) {
        size_t half = n / 2;
        const char *mid = lo + half * size;
        int order = cmp(key, mid);

        if (order == 0)
            return (void *)mid;
        if (order > 0) {
            lo = mid + size;
            n -= half + 1;
        } else {
            n = half;
        }
    }
    return 0;
}

void qsort(void *base, size_t count, size_t size,
           int (*cmp)(const void *, const void *))
{
    char *src = (char *)base;
    char *scratch;
    char *dst;
    size_t width = 1;

    if (count < 2 || size == 0)
        return;
    scratch = (char *)malloc(count * size);
    if (scratch == 0) {
        /* No scratch: an insertion sort, swapping in place. It is O(n^2) and
         * only happens when the array is too big to allocate twice, which is
         * the case where sorting slowly still beats not sorting. */
        char *a = (char *)base;
        size_t i, j;

        for (i = 1; i < count; i++) {
            for (j = i; j > 0; j--) {
                char *left = a + (j - 1) * size;
                char *right = a + j * size;
                size_t k;

                if (cmp(left, right) <= 0)
                    break;
                for (k = 0; k < size; k++) {
                    char t = left[k];
                    left[k] = right[k];
                    right[k] = t;
                }
            }
        }
        return;
    }

    /* Bottom-up merge: width doubles each pass, alternating between the
     * caller's array and the scratch buffer, so the sorted run always ends up
     * in the caller's memory. */
    while (width < count) {
        size_t start = 0;

        for (start = 0; start < count; start += width * 2) {
            size_t left = start;
            size_t mid = start + width;
            size_t right = start + width * 2;
            size_t i = left, j = mid, k = left;

            if (mid > count)
                mid = count;
            if (right > count)
                right = count;
            while (i < mid && j < right) {
                if (cmp(src + j * size, src + i * size) < 0)
                    memcpy(scratch + k++ * size, src + j++ * size, size);
                else
                    memcpy(scratch + k++ * size, src + i++ * size, size);
            }
            while (i < mid)
                memcpy(scratch + k++ * size, src + i++ * size, size);
            while (j < right)
                memcpy(scratch + k++ * size, src + j++ * size, size);
        }
        dst = src;
        src = scratch;
        scratch = dst;
        width *= 2;
    }
    if (src != (char *)base)
        memcpy(base, src, count * size);
    free(src);
}

/* ------------------------------------------------------------------ */
/* Random numbers                                                      */
/* ------------------------------------------------------------------ */

static unsigned long rand_state = 1;

void srand(unsigned seed)
{
    rand_state = seed == 0 ? 1 : seed;
}

int rand(void)
{
    /* A 32-bit linear congruential generator (the constants are Numerical
     * Recipes'): what `shuf` needs is a cheap, repeatable stream, not
     * cryptographic quality. */
    rand_state = rand_state * 1664525UL + 1013904223UL;
    return (int)((rand_state >> 1) & RAND_MAX);
}

/* ------------------------------------------------------------------ */
/* Strings on the heap                                                 */
/* ------------------------------------------------------------------ */

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);

    if (copy == 0)
        return 0;
    memcpy(copy, s, n);
    return copy;
}

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *copy = (char *)malloc(len + 1);

    if (copy == 0)
        return 0;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

/* ------------------------------------------------------------------ */
/* The process end                                                     */
/* ------------------------------------------------------------------ */

char *getenv(const char *name)
{
    static char entry[128];
    static char value[128];
    size_t name_len;
    i32 i;

    if (name == 0 || *name == '\0')
        return 0;
    name_len = strlen(name);

    /* SYS_GETENV answers with a pointer into the kernel's own table, which a
     * ring-3 program cannot dereference; SYS_GETENV_SLOT copies the entry out.
     * So the table is walked and the entry whose name matches, followed by
     * '=', is the one returned - the value, not the "NAME=" prefix. */
    for (i = 0; i < 32; i++) {
        if (mv_syscall(SYS_GETENV_SLOT, (u32)i, (u32)entry, sizeof entry, 0, 0) < 0)
            continue;
        if (strncmp(entry, name, name_len) != 0 || entry[name_len] != '=')
            continue;
        strcpy(value, entry + name_len + 1);
        return value;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* atexit and abort                                                    */
/* ------------------------------------------------------------------ */

#define ATEXIT_MAX_FUNCS 8

static void (*atexit_funcs[ATEXIT_MAX_FUNCS])(void);
static int atexit_count;

int atexit(void (*fn)(void))
{
    if (fn == 0 || atexit_count >= ATEXIT_MAX_FUNCS)
        return -1;
    atexit_funcs[atexit_count++] = fn;
    return 0;
}

/* Called by exit() in malloc.c: the handlers run newest first, as C says. */
void mv_run_atexit(void)
{
    while (atexit_count > 0) {
        void (*fn)(void) = atexit_funcs[--atexit_count];

        atexit_funcs[atexit_count] = 0;
        fn();
    }
}

void abort(void)
{
    /* No signal machinery is reached from here on purpose: a program that has
     * called abort has already decided no more of it should run, and
     * SYS_KILL(self, SIGABRT) would hand control back to a handler. */
    exit(134);
    for (;;)
        ;
}
