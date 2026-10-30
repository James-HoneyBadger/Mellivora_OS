/*
 * assert.h - the one diagnostic every C program assumes
 *
 * It writes to stderr and calls abort, so a failed assumption in a utility
 * prints rather than continuing with a value nobody checked.
 */
#ifndef EASSERT_H
#define EASSERT_H

int printf(const char *fmt, ...);

void __assert_fail(const char *expr, const char *file, int line);

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__))
#endif

#endif /* EASSERT_H */
