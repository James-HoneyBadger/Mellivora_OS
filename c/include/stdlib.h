/*
 * stdlib.h - the heap, conversions, searching and sorting
 *
 * The allocator is c/rt/malloc.c (a first-fit free list over SYS_SBRK, blocks
 * coalesced with their neighbours). Everything else here is c/rt/stdlib.c and
 * has no syscall in it, so the same code could run in ring 0.
 *
 * `qsort` is a bottom-up merge over a scratch buffer: the filesystem hands back
 * a directory listing in whatever order its entries sit in, and a utility that
 * sorts it wants a stable order it can test for.
 *
 * `getenv` reads the shell's variables through SYS_GETENV; `setenv` is not here
 * because a program cannot change its parent's environment - the shell owns it
 * (docs/POSIX.md).
 */
#ifndef ESTDLIB_H
#define ESTDLIB_H

#include <stddef.h>
#include <mv/types.h>

void  *malloc(size_t size);
void   free(void *ptr);
void  *calloc(size_t count, size_t size);
void  *realloc(void *ptr, size_t size);

void   exit(int status) __attribute__((noreturn));
void   abort(void) __attribute__((noreturn));
int    atexit(void (*fn)(void));

int    atoi(const char *s);
long   atol(const char *s);
long long atoll(const char *s);

long      strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
long long strtoll(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);

int  abs(int v);
long labs(long v);
long long llabs(long long v);

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;

div_t  div(int num, int den);
ldiv_t ldiv(long num, long den);

void  qsort(void *base, size_t count, size_t size,
            int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*cmp)(const void *, const void *));

#define RAND_MAX 0x7FFFFFFF
int  rand(void);
void srand(unsigned seed);

char *getenv(const char *name);

#endif /* ESTDLIB_H */
