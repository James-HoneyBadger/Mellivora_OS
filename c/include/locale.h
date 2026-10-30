/*
 * locale.h - the C locale, and only the C locale
 *
 * POSIX requires an implementation to support "C" (and "POSIX"), and requires
 * nothing else. setlocale() therefore answers "C" for the C request and NULL
 * for every other, which is a conforming answer rather than a missing feature:
 * a program that asks for a locale this system does not have must be told so.
 * strcoll() and strxfrm() are the C locale's answers - byte order - and
 * localeconv() reports the C locale's conventions.
 */
#ifndef ELOCALE_H
#define ELOCALE_H

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *currency_symbol;
    char *int_curr_symbol;
};

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);

#endif /* ELOCALE_H */
