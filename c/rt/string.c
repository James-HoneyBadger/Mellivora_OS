/*
 * string.c - the C library's string and memory functions
 *
 * The first four are not decorative: with optimization on, the
 * compiler itself emits calls to memcpy, memmove, memset and memcmp for
 * structure copies and array initialisation, so a C subsystem that does not
 * provide them cannot be linked at all. The rest are what the shell-facing
 * code in this tree keeps needing.
 *
 * Written in plain C rather than inline assembly: correctness first, and the
 * kernel's own hot copies (block I/O, page clears) are `rep movsd`/`rep stosd`
 * in the assembly modules, not here.
 */
#include <string.h>
#include <strings.h>
#include <errno.h>

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

/* The C locale sorts by byte order, which is what strcmp already does. */
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* And its transformed form is the string unchanged: strxfrm copies at most
 * n bytes, and always reports the full length so a caller can size a buffer. */
size_t strxfrm(char *dst, const char *src, size_t n)
{
    size_t len = strlen(src);

    if (dst != NULL && n > 0) {
        size_t copy = (len < n - 1) ? len : n - 1;

        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
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


/* ------------------------------------------------------------------ */
/* Searching and copying                                              */
/* ------------------------------------------------------------------ */

void *memchr(const void *s, int c, size_t n)
{
    const u8 *p = (const u8 *)s;

    while (n-- != 0) {
        if (*p == (u8)c)
            return (void *)p;
        p++;
    }
    return 0;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && s[n] != '\0')
        n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;

    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;

    for (; i < n && src[i] != '\0'; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';              /* C requires the tail to be padded */
    return dst;
}

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);

    while (n-- != 0 && *src != '\0')
        *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == '\0')
            return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *found = 0;

    for (;; s++) {
        if (*s == (char)c)
            found = s;
        if (*s == '\0')
            return (char *)found;
    }
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n;

    if (*needle == '\0')
        return (char *)haystack;
    n = strlen(needle);
    for (; *haystack != '\0'; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, n) == 0)
            return (char *)haystack;
    }
    return 0;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;

    while (s[n] != '\0' && strchr(accept, s[n]) != 0)
        n++;
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;

    while (s[n] != '\0' && strchr(reject, s[n]) == 0)
        n++;
    return n;
}

char *strpbrk(const char *s, const char *accept)
{
    for (; *s != '\0'; s++) {
        if (strchr(accept, *s) != 0)
            return (char *)s;
    }
    return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *start;

    if (str != 0)
        *saveptr = str;
    start = *saveptr;
    if (start == 0)
        return 0;

    start += strspn(start, delim);   /* skip the separators before a token */
    if (*start == '\0') {
        *saveptr = 0;
        return 0;
    }
    *saveptr = start + strcspn(start, delim);
    if (**saveptr != '\0')
        *(*saveptr)++ = '\0';        /* end the token, step past its nul */
    else
        *saveptr = 0;
    return start;
}

char *strtok(char *str, const char *delim)
{
    static char *next;

    return strtok_r(str, delim, &next);
}

char *strerror(int errnum)
{
    static char unknown[] = "Unknown error 000";
    const char *text;

    switch (errnum) {
    case 0:      text = "Success"; break;
    case EPERM:  text = "Operation not permitted"; break;
    case ENOENT: text = "No such file or directory"; break;
    case ESRCH:  text = "No such process"; break;
    case EINTR:  text = "Interrupted system call"; break;
    case EIO:    text = "Input/output error"; break;
    case ENOEXEC: text = "Exec format error"; break;
    case EBADF:  text = "Bad file descriptor"; break;
    case ECHILD: text = "No child processes"; break;
    case EAGAIN: text = "Resource temporarily unavailable"; break;
    case ENOMEM: text = "Cannot allocate memory"; break;
    case EACCES: text = "Permission denied"; break;
    case EFAULT: text = "Bad address"; break;
    case EEXIST: text = "File exists"; break;
    case ENOTDIR: text = "Not a directory"; break;
    case EISDIR: text = "Is a directory"; break;
    case EINVAL: text = "Invalid argument"; break;
    case ENFILE: text = "Too many open files in system"; break;
    case EMFILE: text = "Too many open files"; break;
    case ENOTTY: text = "Inappropriate ioctl for device"; break;
    case EFBIG:  text = "File too large"; break;
    case ENOSPC: text = "No space left on device"; break;
    case ESPIPE: text = "Illegal seek"; break;
    case EROFS:  text = "Read-only file system"; break;
    case EPIPE:  text = "Broken pipe"; break;
    case ERANGE: text = "Numerical result out of range"; break;
    case ENOSYS: text = "Function not implemented"; break;
    default:
        /* The number is part of the message: a caller logs it, and an
         * "unknown error" with no number is a dead end. */
        unknown[14] = (char)('0' + (errnum / 100) % 10);
        unknown[15] = (char)('0' + (errnum / 10) % 10);
        unknown[16] = (char)('0' + errnum % 10);
        return unknown;
    }
    return (char *)text;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;

        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n-- != 0) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;

        if (ca != cb)
            return ca - cb;
        if (*a == '\0')
            return 0;               /* both ended together */
        a++;
        b++;
    }
    return 0;                       /* n characters agreed */
}

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}
