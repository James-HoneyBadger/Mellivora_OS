/*
 * stdlib.h - the heap and the exit path of the freestanding runtime
 *
 * The allocator (c/rt/malloc.c) is a first-fit free list over SYS_SBRK: the
 * heap is the program's own break, grown in pages, which is why a program that
 * never allocates never pays for this file. Blocks are 8-byte aligned, freed
 * blocks are coalesced with their neighbours, and a size of zero still returns
 * a unique pointer that `free` accepts.
 *
 * `exit` does not run destructors, flush streams or call atexit handlers -
 * there are none yet - but it is where those will live, so programs can call
 * it from the start.
 */
#ifndef MV_STDLIB_H
#define MV_STDLIB_H

#include <mv/types.h>

void *malloc(u32 size);
void  free(void *ptr);
void *calloc(u32 count, u32 size);
void *realloc(void *ptr, u32 size);

void exit(int status) __attribute__((noreturn));

int atoi(const char *s);

#endif /* MV_STDLIB_H */
