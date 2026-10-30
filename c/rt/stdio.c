/*
 * stdio.c - streams and formatted output, the C library's stdio
 *
 * One formatter serves printf, fprintf, sprintf and snprintf: the sink changes
 * and nothing else does, which is what keeps four entry points from disagreeing
 * about how a number is rendered. The console sink turns '\n' into "\r\n",
 * because the serial line and the VGA text buffer both want the carriage
 * return; a buffer or a file gets exactly the bytes the format asked for, so a
 * program that writes its own file never finds stray CRs in it.
 *
 * Reads are a descriptor and a little state. Writes are not: HBFS stores a file
 * as one run of blocks and SYS_FWRITE replaces a whole file, so a stream that
 * writes collects its bytes and hands them over when the stream is flushed or
 * closed. That is why fflush and fclose return a status worth checking - the
 * write happens there. A tool that opens without a mode that writes and then
 * reads is unaffected.
 *
 * Standard input is the exception, and it is the kernel's shape showing through:
 * a program launched with piped input is handed the whole of it by
 * SYS_STDIN_READ, once. stdin is therefore a buffer that is filled on the first
 * read and served from memory afterwards.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <mv/syscall.h>

#define OUT_CHUNK 256

struct mv_file {
    int   fd;
    int   mode;              /* O_RDONLY / O_WRONLY / O_RDWR */
    int   eof;
    int   error;
    int   ungot;             /* one pushed-back character, or -1 */
    int   is_console;        /* fd 1 or 2: writes go to the console, not the VFS */
    char  path[260];         /* the name a write is handed to at flush time */
    char *wbuf;              /* bytes written so far, waiting for the flush */
    u32   wlen;
    u32   wcap;
    char *rbuf;              /* a read stream's copy of the file */
    u32   rlen;
    u32   rpos;
};

static FILE console_out = { 1, O_WRONLY, 0, 0, -1, 1, "", 0, 0, 0, 0, 0, 0 };
static FILE console_err = { 2, O_WRONLY, 0, 0, -1, 1, "", 0, 0, 0, 0, 0, 0 };
static FILE console_in  = { 0, O_RDONLY, 0, 0, -1, 0, "", 0, 0, 0, 0, 0, 0 };

FILE *stdin  = &console_in;
FILE *stdout = &console_out;
FILE *stderr = &console_err;

/* ------------------------------------------------------------------ */
/* The console                                                         */
/* ------------------------------------------------------------------ */

static void console_write(const char *s, u32 n)
{
    char crlf[OUT_CHUNK * 2];
    u32  i;
    u32  k = 0;

    for (i = 0; i < n; i++) {
        if (s[i] == '\n' && k + 1 < sizeof(crlf)) {
            crlf[k++] = '\r';
            crlf[k++] = '\n';
        } else if (k < sizeof(crlf) - 1) {
            crlf[k++] = s[i];
        }
    }
    crlf[k] = '\0';

    if (mv_write(1, crlf, k) != (i32)k) {
        /* No fd 1: the legacy console call is still there. */
        mv_print(crlf);
    }
}

/* ------------------------------------------------------------------ */
/* Streams                                                             */
/* ------------------------------------------------------------------ */

int fileno(FILE *f)
{
    return f == 0 ? -1 : f->fd;
}

static FILE *stream_new(int fd, int mode)
{
    FILE *f = (FILE *)malloc(sizeof *f);

    if (f == 0) {
        errno = ENOMEM;
        return 0;
    }
    f->fd = fd;
    f->mode = mode;
    f->eof = 0;
    f->error = 0;
    f->ungot = -1;
    f->is_console = 0;
    f->path[0] = '\0';
    f->wbuf = 0;
    f->wlen = 0;
    f->wcap = 0;
    f->rbuf = 0;
    f->rlen = 0;
    f->rpos = 0;
    return f;
}

static int mode_flags(const char *mode, int *append)
{
    *append = 0;
    switch (mode[0]) {
    case 'r':
        return (mode[1] == '+') ? O_RDWR : O_RDONLY;
    case 'w':
        return ((mode[1] == '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    case 'a':
        *append = 1;
        return ((mode[1] == '+') ? O_RDWR : O_WRONLY) | O_CREAT;
    default:
        errno = EINVAL;
        return -1;
    }
}

FILE *fopen(const char *path, const char *mode)
{
    int append;
    int flags = mode_flags(mode, &append);
    int fd;

    if (flags < 0)
        return 0;
    fd = open(path, flags);
    if (fd < 0)
        return 0;
    {
        FILE *f = stream_new(fd, flags & O_ACCMODE);

        if (f == 0) {
            close(fd);
            return 0;
        }
        {
            u32 i = 0;

            while (path[i] != '\0' && i < sizeof f->path - 1) {
                f->path[i] = path[i];
                i++;
            }
            f->path[i] = '\0';
        }
        if ((flags & O_ACCMODE) != O_WRONLY) {
            /* A read stream is the file, copied in once. SYS_FREAD is the
             * whole-file door this filesystem has, and the descriptor's stream
             * read path is not: see docs/POSIX.md's note on SYS_READ. */
            struct stat st;

            if (stat(path, &st) == 0) {
                f->rbuf = (char *)malloc(st.st_size + 1);
                if (f->rbuf != 0) {
                    i32 got = mv_syscall(SYS_FREAD, (u32)path, (u32)f->rbuf, 0, 0, 0);

                    f->rlen = got > 0 ? (u32)got : 0;
                    f->rpos = 0;
                }
            }
        }
        if (append) {
            /* Append has to carry the existing bytes with it, because the flush
             * replaces the file rather than adding to it. */
            static char old[4096];
            i32 got = mv_syscall(SYS_FREAD, (u32)path, (u32)old, 0, 0, 0);

            if (got > 0)
                (void)fwrite(old, 1, (size_t)got, f);
        }
        return f;
    }
}

FILE *fdopen(int fd, const char *mode)
{
    int append;
    int flags = mode_flags(mode, &append);

    if (flags < 0 || fd < 0) {
        errno = EBADF;
        return 0;
    }
    return stream_new(fd, flags & O_ACCMODE);
}

int fclose(FILE *f)
{
    int rc;

    if (f == 0)
        return EOF;
    if (f == stdin || f == stdout || f == stderr) {
        /* The standard handles are the console's, not this program's to close:
         * close() on 0/1/2 is EBADF anyway, and pretending otherwise would make
         * a tool that fcloses stdout look like it had succeeded. */
        f->error = 1;
        return EOF;
    }
    rc = (fflush(f) == 0) ? 0 : -1;
    if (f->fd >= 0 && close(f->fd) != 0)
        rc = -1;
    f->fd = -1;
    if (f->wbuf != 0)
        free(f->wbuf);
    if (f->rbuf != 0)
        free(f->rbuf);
    free(f);
    return rc == 0 ? 0 : EOF;
}

int fflush(FILE *f)
{
    if (f == 0 || f->wbuf == 0 || f->wlen == 0)
        return 0;
    if (f->is_console || f->path[0] == '\0') {
        f->error = 1;
        return EOF;
    }
    /* SYS_FWRITE replaces the file: it is the whole-file door this filesystem
     * has, and it is why the bytes were collected rather than written out. */
    if (mv_syscall(SYS_FWRITE, (u32)f->path, (u32)f->wbuf, f->wlen, 1, 0) < 0) {
        f->error = 1;
        errno = EIO;
        return EOF;
    }
    f->wlen = 0;
    return 0;
}

size_t fread(void *buf, size_t size, size_t count, FILE *f)
{
    size_t want;
    i32    got;
    char  *dst = (char *)buf;
    size_t done = 0;

    if (f == 0 || size == 0 || count == 0)
        return 0;
    want = size * count;

    /* One pushed-back character, then the rest of the request. */
    if (f->ungot >= 0 && done < want) {
        dst[done++] = (char)f->ungot;
        f->ungot = -1;
    }

    if (f->rbuf != 0) {
        while (done < want && f->rpos < f->rlen)
            dst[done++] = f->rbuf[f->rpos++];
        if (f->rpos >= f->rlen)
            f->eof = 1;
        return done / size;
    }

    if (f->fd == 0) {
        /* Standard input: one slurp, then memory. */
        static char piped[4096];
        static i32  piped_len = -2;   /* -2 = not asked yet */
        static i32  piped_pos;

        if (piped_len == -2) {
            piped_len = mv_syscall(SYS_STDIN_READ, (u32)piped, 0, 0, 0, 0);
            piped_pos = 0;
            if (piped_len < 0)
                piped_len = 0;
        }
        while (done < want && piped_pos < piped_len)
            dst[done++] = piped[piped_pos++];
        if (done < want)
            f->eof = 1;
        return done / size;
    }

    while (done < want) {
        got = mv_syscall(SYS_READ, (u32)f->fd, (u32)(dst + done),
                         (u32)(want - done), 0, 0);
        if (got < 0) {
            f->error = 1;
            break;
        }
        if (got == 0) {
            f->eof = 1;
            break;
        }
        done += (size_t)got;
    }
    return done / size;
}

size_t fwrite(const void *buf, size_t size, size_t count, FILE *f)
{
    size_t want;
    size_t done = 0;

    if (f == 0 || size == 0 || count == 0)
        return 0;
    want = size * count;

    if (f->is_console) {
        console_write((const char *)buf, (u32)want);
        return count;
    }

    /* Collect: see the file comment. The buffer is the file's new contents. */
    while (done < want) {
        if (f->wlen == f->wcap) {
            u32   bigger = f->wcap == 0 ? 1024 : f->wcap * 2;
            char *grown = f->wbuf == 0 ? (char *)malloc(bigger)
                                       : (char *)realloc(f->wbuf, bigger);

            if (grown == 0) {
                f->error = 1;
                errno = ENOMEM;
                break;
            }
            f->wbuf = grown;
            f->wcap = bigger;
        }
        f->wbuf[f->wlen++] = ((const char *)buf)[done++];
    }
    return done / size;
}

int fgetc(FILE *f)
{
    char c;

    if (f == 0)
        return EOF;
    if (f->ungot >= 0) {
        int ch = f->ungot;

        f->ungot = -1;
        return ch;
    }
    if (fread(&c, 1, 1, f) != 1)
        return EOF;
    return (u8)c;
}

int getc(FILE *f)
{
    return fgetc(f);
}

int getchar(void)
{
    return fgetc(stdin);
}

int ungetc(int c, FILE *f)
{
    if (f == 0 || c == EOF)
        return EOF;
    f->ungot = (u8)c;
    f->eof = 0;
    return (u8)c;
}

int fputc(int c, FILE *f)
{
    char ch = (char)c;

    if (fwrite(&ch, 1, 1, f) != 1)
        return EOF;
    return (u8)c;
}

char *fgets(char *s, int size, FILE *f)
{
    int i = 0;

    if (s == 0 || size <= 0 || f == 0)
        return 0;
    while (i < size - 1) {
        int c = fgetc(f);

        if (c == EOF)
            break;
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return 0;
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);

    if (fwrite(s, 1, n, f) != n)
        return EOF;
    return 0;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF)
        return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 1;
}

int putchar(int c)
{
    if (fputc(c, stdout) == EOF)
        return EOF;
    return (u8)c;
}

int fseek(FILE *f, long offset, int whence)
{
    if (f == 0)
        return -1;
    f->ungot = -1;
    f->eof = 0;
    if (f->rbuf != 0) {
        long target;

        if (whence == SEEK_SET)
            target = offset;
        else if (whence == SEEK_CUR)
            target = (long)f->rpos + offset;
        else
            target = (long)f->rlen + offset;
        if (target < 0 || (u32)target > f->rlen) {
            errno = EINVAL;
            return -1;
        }
        f->rpos = (u32)target;
        return 0;
    }
    if (lseek(f->fd, (off_t)offset, whence) < 0) {
        f->error = 1;
        return -1;
    }
    return 0;
}

long ftell(FILE *f)
{
    if (f == 0)
        return -1;
    if (f->rbuf != 0)
        return (long)f->rpos;
    return (long)lseek(f->fd, 0, SEEK_CUR);
}

void rewind(FILE *f)
{
    if (f != 0)
        (void)fseek(f, 0, SEEK_SET);
}

int feof(FILE *f)
{
    return f == 0 ? 0 : f->eof;
}

int ferror(FILE *f)
{
    return f == 0 ? 0 : f->error;
}

void clearerr(FILE *f)
{
    if (f != 0) {
        f->eof = 0;
        f->error = 0;
    }
}

int remove(const char *path)
{
    if (unlink(path) == 0)
        return 0;
    return -1;
}

int rename(const char *from, const char *to)
{
    return mv_syscall(SYS_RENAME, (u32)from, (u32)to, 0, 0, 0) < 0 ? -1 : 0;
}

void perror(const char *prefix)
{
    char line[96];
    int  n = 0;

    if (prefix != 0 && *prefix != '\0') {
        while (*prefix != '\0' && n < 64)
            line[n++] = *prefix++;
        line[n++] = ':';
        line[n++] = ' ';
    }
    {
        char *msg = strerror(errno);

        while (*msg != '\0' && n < 90)
            line[n++] = *msg++;
    }
    line[n++] = '\n';
    (void)fwrite(line, 1, (size_t)n, stderr);
}

/* ------------------------------------------------------------------ */
/* The formatter                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;          /* destination buffer, or NULL for a stream        */
    u32   cap;          /* bytes the buffer can hold, including the NUL     */
    u32   len;          /* bytes the format produced, truncation included   */
    FILE *stream;       /* destination stream, or NULL for the console      */
    char  tmp[OUT_CHUNK];
    u32   used;
    int   failed;
} out_t;

static void out_flush(out_t *o)
{
    if (o->used == 0)
        return;
    if (o->stream != 0) {
        if (fwrite(o->tmp, 1, o->used, o->stream) != o->used)
            o->failed = 1;
    } else {
        console_write(o->tmp, o->used);
    }
    o->used = 0;
}

static void out_char(out_t *o, char c)
{
    if (o->buf != NULL) {
        if (o->len + 1 < o->cap)
            o->buf[o->len] = c;
        o->len++;
        return;
    }
    o->tmp[o->used++] = c;
    if (o->used == OUT_CHUNK)
        out_flush(o);
}

static void out_text(out_t *o, const char *s)
{
    while (*s != '\0')
        out_char(o, *s++);
}

static void out_repeat(out_t *o, char c, u32 count)
{
    while (count-- != 0)
        out_char(o, c);
}

/* One number: prefix, digits, and the padding the conversion asked for.
 * Padding with '0' goes between the prefix and the digits, padding with spaces
 * goes outside, and a left-justified conversion pads on the right. Precision
 * is a minimum digit count and is met with leading zeros. */
static void out_number(out_t *o, u32 magnitude, const char *prefix, u32 base,
                       int upper, u32 width, u32 precision, char pad, int left)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char  tmp[32];
    u32   n = 0;
    u32   prefix_len = (u32)strlen(prefix);
    u32   shown;
    u32   zeros;

    do {
        tmp[n++] = digits[magnitude % base];
        magnitude /= base;
    } while (magnitude != 0);

    zeros = (precision > n) ? precision - n : 0;
    shown = n + zeros + prefix_len;

    if (!left && pad == ' ' && width > shown)
        out_repeat(o, ' ', width - shown);

    out_text(o, prefix);

    if (!left && pad == '0' && precision == 0 && width > shown)
        out_repeat(o, '0', width - shown);
    out_repeat(o, '0', zeros);

    while (n != 0)
        out_char(o, tmp[--n]);

    if (left && width > shown)
        out_repeat(o, ' ', width - shown);
}

static int format_into(out_t *o, const char *fmt, va_list ap)
{
    int written;

    while (*fmt != '\0') {
        u32  width = 0;
        u32  precision = 0;
        char pad = ' ';
        int  left = 0;
        int  plus = 0;
        int  space = 0;
        int  alt = 0;

        if (*fmt != '%') {
            out_char(o, *fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '%') {
            out_char(o, '%');
            fmt++;
            continue;
        }

        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else if (*fmt == '+')
                plus = 1;
            else if (*fmt == ' ')
                space = 1;
            else if (*fmt == '#')
                alt = 1;
            else
                break;
        }
        if (*fmt == '*') {
            int w = va_arg(ap, int);

            if (w < 0) {
                left = 1;
                w = -w;
            }
            width = (u32)w;
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (u32)(*fmt - '0');
                fmt++;
            }
        }
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                int p = va_arg(ap, int);

                precision = p < 0 ? 0 : (u32)p;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (u32)(*fmt - '0');
                    fmt++;
                }
            }
            if (precision == 0)
                precision = 1;      /* "%.0d" is still one digit here */
        }
        /* Length modifiers are consumed and mean nothing: this target has one
         * integer width, so %ld and %d take the same argument. */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't' ||
               *fmt == 'j' || *fmt == 'q')
            fmt++;

        switch (*fmt) {
        case 'd':
        case 'i': {
            i32  v = va_arg(ap, i32);
            u32  m = v < 0 ? (u32)(-(i64)v) : (u32)v;
            char prefix[2];

            prefix[0] = '\0';
            if (v < 0)
                prefix[0] = '-';
            else if (plus)
                prefix[0] = '+';
            else if (space)
                prefix[0] = ' ';
            prefix[1] = '\0';
            out_number(o, m, prefix, 10, 0, width, precision, pad, left);
            break;
        }
        case 'u':
            out_number(o, va_arg(ap, u32), "", 10, 0, width, precision, pad, left);
            break;
        case 'o':
            out_number(o, va_arg(ap, u32), alt ? "0" : "", 8, 0, width,
                       precision, pad, left);
            break;
        case 'x':
            out_number(o, va_arg(ap, u32), alt ? "0x" : "", 16, 0, width,
                       precision, pad, left);
            break;
        case 'X':
            out_number(o, va_arg(ap, u32), alt ? "0X" : "", 16, 1, width,
                       precision, pad, left);
            break;
        case 'p':
            out_text(o, "0x");
            out_number(o, va_arg(ap, u32), "", 16, 0, 8, 1, '0', 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            u32         n;
            u32         i;

            if (s == 0)
                s = "(null)";
            n = strlen(s);
            if (precision != 0 && precision < n)
                n = precision;      /* "%.3s" prints at most three bytes */
            if (!left)
                for (i = n; i < width; i++)
                    out_char(o, ' ');
            for (i = 0; i < n; i++)
                out_char(o, s[i]);
            if (left)
                for (i = n; i < width; i++)
                    out_char(o, ' ');
            break;
        }
        case 'c':
            out_char(o, (char)va_arg(ap, int));
            break;
        case 'f':
        case 'e':
        case 'g':
        case 'E':
        case 'G':
            /* No floating point in this formatter, and a wrong number is worse
             * than a visible refusal: the conversion prints as it was written. */
            out_text(o, "%");
            out_char(o, *fmt);
            break;
        case '\0':
            out_char(o, '%');
            break;
        default:
            out_char(o, '%');
            out_char(o, *fmt);
            break;
        }
        if (*fmt == '\0')
            break;
        fmt++;
    }

    if (o->buf != NULL) {
        u32 end = o->len < o->cap ? o->len : o->cap - 1;

        o->buf[end] = '\0';
    } else {
        out_flush(o);
    }
    written = (int)o->len;
    return o->failed ? -1 : written;
}

int vsnprintf(char *buf, u32 size, const char *fmt, va_list ap)
{
    out_t o;

    o.buf = buf;
    o.cap = size;
    o.len = 0;
    o.used = 0;
    o.stream = 0;
    o.failed = 0;
    return format_into(&o, fmt, ap);
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, 0xFFFFFFFFu, fmt, ap);
}

int snprintf(char *buf, u32 size, const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    out_t o;

    if (f == 0)
        return -1;
    o.buf = 0;
    o.cap = 0;
    o.len = 0;
    o.used = 0;
    o.stream = f;
    o.failed = 0;
    return format_into(&o, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

/* ------------------------------------------------------------------ */
/* getline                                                             */
/* ------------------------------------------------------------------ */

long getdelim(char **lineptr, size_t *n, int delim, FILE *f)
{
    size_t used = 0;

    if (lineptr == 0 || n == 0 || f == 0) {
        errno = EINVAL;
        return -1;
    }
    if (*lineptr == 0 || *n == 0) {
        *n = 64;
        *lineptr = (char *)malloc(*n);
        if (*lineptr == 0) {
            errno = ENOMEM;
            return -1;
        }
    }

    for (;;) {
        int c = fgetc(f);

        if (c == EOF) {
            if (used == 0)
                return -1;
            break;
        }
        if (used + 1 >= *n) {
            char *bigger = (char *)realloc(*lineptr, *n * 2);

            if (bigger == 0) {
                errno = ENOMEM;
                return -1;
            }
            *lineptr = bigger;
            *n *= 2;
        }
        (*lineptr)[used++] = (char)c;
        if (c == delim)
            break;
    }
    (*lineptr)[used] = '\0';
    return (long)used;
}

long getline(char **lineptr, size_t *n, FILE *f)
{
    return getdelim(lineptr, n, '\n', f);
}
