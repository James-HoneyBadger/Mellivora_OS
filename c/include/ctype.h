/*
 * ctype.h - character classes, in the C locale
 *
 * These are functions rather than the macros a libc usually uses, because the
 * table behind them is one it does not need to expose. Only the C locale
 * exists (locale.h), so these are the only classes there are.
 */
#ifndef ECTYPE_H
#define ECTYPE_H

int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isxdigit(int c);
int iscntrl(int c);
int toupper(int c);
int tolower(int c);

#endif /* ECTYPE_H */
