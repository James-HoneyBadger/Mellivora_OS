/*
 * strings.h - the case-folding and byte helpers POSIX puts here rather than in
 * string.h
 */
#ifndef ESTRINGS_H
#define ESTRINGS_H

#include <stddef.h>

int  strcasecmp(const char *a, const char *b);
int  strncasecmp(const char *a, const char *b, size_t n);
void bzero(void *s, size_t n);

#endif /* ESTRINGS_H */
