/*
 * string.h - the freestanding runtime every C build links against
 *
 * There is no libc in either address space. A compiler is entitled to call
 * memcpy, memmove, memset and memcmp for the code it generates (structure
 * copies, array initialisation), so these must exist before optimization is
 * turned on. c/rt/string.c is the only implementation, and both the kernel
 * module and user programs link it.
 */
#ifndef MV_STRING_H
#define MV_STRING_H

#include <mv/types.h>

void  *memcpy(void *dst, const void *src, u32 n);
void  *memmove(void *dst, const void *src, u32 n);
void  *memset(void *dst, int c, u32 n);
int    memcmp(const void *a, const void *b, u32 n);
u32    strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, u32 n);

#endif /* MV_STRING_H */
