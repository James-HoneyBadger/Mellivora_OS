/*
 * stddef.h - the types the standard headers themselves need
 *
 * This is the C library's own stddef rather than the compiler's: on this target
 * size_t, ptrdiff_t and a pointer are all 32 bits (ABI.md section 3), so there
 * is nothing for the compiler to know that this file does not say.
 */
#ifndef ESTDDEF_H
#define ESTDDEF_H

typedef unsigned int   size_t;
typedef signed int     ptrdiff_t;
typedef unsigned short wchar_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* ESTDDEF_H */
