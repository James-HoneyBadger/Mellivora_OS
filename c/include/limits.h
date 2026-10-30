/*
 * limits.h - the limits this system actually enforces
 *
 * Each number here is one the kernel or the filesystem enforces, not one that
 * was convenient to write down: HBFS names are 252 bytes, a path is resolved
 * to 512, and a program's command line is 512.
 */
#ifndef ELIMITS_H
#define ELIMITS_H

#define NAME_MAX    252
#define PATH_MAX    512
#define ARG_MAX     512
#define OPEN_MAX    64
#define CHAR_BIT    8
#define INT_MAX     2147483647
#define INT_MIN     (-2147483647 - 1)

#endif /* ELIMITS_H */
