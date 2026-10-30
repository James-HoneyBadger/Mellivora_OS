/*
 * stdio.h - streams, formatted output and the standard handles
 *
 * One formatter serves printf, fprintf, sprintf and snprintf (c/rt/stdio.c, the
 * same code the kernel's C module does not use). The conversions are d, i, u,
 * o, x, X, s, c, p and %% with the -, 0, +, space and # flags, an optional
 * width, an optional precision and the * forms of both. Length modifiers are
 * accepted and mean nothing: this target has one integer width, so %ld and %d
 * are the same conversion and saying so is better than refusing them.
 *
 * Floating point is not in the formatter. There is no floating-point formatting
 * routine in this tree at all - the kernel's own C is compiled -msoft-float and
 * nothing has needed to print a real yet - so %f, %e and %g are refused with
 * their literal text rather than rounded to something wrong.
 *
 * A FILE here is a descriptor and a mode, unbuffered: every fwrite reaches the
 * VFS in one call. That is honest for a tool that copies a file and it keeps
 * the failure path simple - there is no buffer to flush at exit, so a program
 * that forgets to fclose cannot lose its last line.
 */
#ifndef ESTDIO_H
#define ESTDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <mv/types.h>

#define EOF      (-1)
#define BUFSIZ   512
#define FILENAME_MAX 512
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct mv_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int   fclose(FILE *f);
int   fflush(FILE *f);
int   fileno(FILE *f);

size_t fread(void *buf, size_t size, size_t count, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t count, FILE *f);
int    fgetc(FILE *f);
int    getc(FILE *f);
int    getchar(void);
int    ungetc(int c, FILE *f);
char  *fgets(char *s, int size, FILE *f);
int    fputs(const char *s, FILE *f);
int    puts(const char *s);
int    putchar(int c);
int    fputc(int c, FILE *f);

int    fseek(FILE *f, long offset, int whence);
long   ftell(FILE *f);
void   rewind(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
void   clearerr(FILE *f);
int    remove(const char *path);
int    rename(const char *from, const char *to);

int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, size_t size, const char *fmt, ...);
int    vprintf(const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int    vsprintf(char *buf, const char *fmt, va_list ap);
void   perror(const char *prefix);

/* POSIX getline: a line of any length from a stream, reusing the buffer. */
long   getline(char **lineptr, size_t *n, FILE *f);
long   getdelim(char **lineptr, size_t *n, int delim, FILE *f);

#endif /* ESTDIO_H */
