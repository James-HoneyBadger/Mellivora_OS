/*
 * types.h - the types that cross the NASM/C boundary
 *
 * docs/spec/ABI.md fixes these: fixed-width types only, `int` never used for a
 * value assembly reads, and no compiler-dependent layouts. Everything in this
 * tree that is shared with a .inc file uses these names.
 */
#ifndef MV_TYPES_H
#define MV_TYPES_H

typedef unsigned char      u8;
typedef signed char        i8;
typedef unsigned short     u16;
typedef signed short       i16;
typedef unsigned int       u32;
typedef signed int         i32;
typedef unsigned long long u64;
typedef signed long long   i64;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Compile-time assertion usable at file scope (ABI.md section 3). */
#define MV_STATIC_ASSERT(cond, name) _Static_assert((cond), #name)

#define MV_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#endif /* MV_TYPES_H */
