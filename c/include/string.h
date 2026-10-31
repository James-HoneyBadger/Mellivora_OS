/*
 * string.h - the C library's string and memory functions
 *
 * c/rt/string.c is the one implementation, and the kernel module links it too:
 * a compiler is entitled to emit calls to memcpy, memmove, memset and memcmp
 * for the code it generates, so those must exist before any optimisation
 * happens. Nothing here calls a syscall, which is why the kernel can use it.
 *
 * The <mv/string.h> spellings in the tree's older sources are kept as aliases
 * of these: one function, one name a POSIX program expects, one name the tree
 * already used.
 */
#ifndef ESTRING_H
#define ESTRING_H

#include <stddef.h>
#include <mv/types.h>

void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);

char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);

int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);

/*
 * The C locale's collation: byte order. locale.h promises these, and a program
 * that sorts with them gets an order it can predict; a locale whose collation
 * order is not byte order is a later wave (docs/POSIX.md).
 */
int    strcoll(const char *a, const char *b);

/* strxfrm for the C locale: the transformed string is the string itself. */
size_t strxfrm(char *dst, const char *src, size_t n);

char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char  *strpbrk(const char *s, const char *accept);
char  *strtok(char *str, const char *delim);
char  *strtok_r(char *str, const char *delim, char **saveptr);

char  *strdup(const char *s);
char  *strndup(const char *s, size_t n);
char  *strerror(int errnum);

#endif /* ESTRING_H */
