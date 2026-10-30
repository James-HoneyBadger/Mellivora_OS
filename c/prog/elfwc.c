/*
 * elfwc.c - count lines, words and bytes
 *
 * The classic counter, built as an ELF32 image: line count is the number of
 * newlines, a word is a run of bytes that are not space, tab or newline, and
 * bytes counts every byte read. With none of -l, -w or -c given, all three are
 * printed, in that order.
 *
 * There is no stdin default: a program that is handed no file says so rather
 * than blocking on a console the shell still owns.
 *
 * Usage: elfwc [-l] [-w] [-c] file...
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mv/args.h>
#include <mv/types.h>

#define CHUNK 512

typedef struct {
    unsigned long lines;
    unsigned long words;
    unsigned long bytes;
} counts;

static int is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int count_file(const char *path, counts *out)
{
    FILE *f;
    unsigned char buf[CHUNK];
    size_t n, i;
    int in_word = 0;

    memset(out, 0, sizeof *out);

    f = fopen(path, "rb");
    if (f == 0) {
        fprintf(stderr, "elfwc: cannot open %s\n", path);
        return 1;
    }

    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        out->bytes += n;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n')
                out->lines++;
            if (is_space(buf[i])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                out->words++;
            }
        }
    }
    fclose(f);
    return 0;
}

static void report(const counts *c, int l, int w, int b, const char *name)
{
    if (l)
        printf("%8lu", c->lines);
    if (w)
        printf("%8lu", c->words);
    if (b)
        printf("%8lu", c->bytes);
    if (name != 0)
        printf(" %s", name);
    putchar('\n');
}

int main(void)
{
    static char args[MV_ARGS_BUFSZ];
    char *argv[MV_ARGS_MAX];
    int argc = mv_args("elfwc", args, sizeof args, argv, MV_ARGS_MAX);
    int want_l = 0, want_w = 0, want_b = 0;
    int files = 0;
    int rc = 0;
    int opt;
    counts total;

    memset(&total, 0, sizeof total);

    opterr = 0;
    while ((opt = getopt(argc, argv, "lwc")) != -1) {
        switch (opt) {
        case 'l': want_l = 1; break;
        case 'w': want_w = 1; break;
        case 'c': want_b = 1; break;
        default:
            fprintf(stderr, "elfwc: unknown option -%c\n", optopt);
            return 1;
        }
    }

    if (!want_l && !want_w && !want_b)
        want_l = want_w = want_b = 1;

    for (; optind < argc; optind++) {
        counts c;

        files++;
        if (count_file(argv[optind], &c) != 0) {
            rc = 1;
            continue;
        }
        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;
        report(&c, want_l, want_w, want_b, argv[optind]);
    }

    if (files == 0) {
        fprintf(stderr, "elfwc: no file named\n");
        return 1;
    }
    if (files > 1)
        report(&total, want_l, want_w, want_b, "total");
    return rc;
}
