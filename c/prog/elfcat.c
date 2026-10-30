/*
 * elfcat.c - print files, optionally with line numbers
 *
 * The same tool every Unix has, built here as an ELF32 image rather than a flat
 * binary: what changes is the container, not the source. It exists next to
 * elfinfo and elfwc so the tree has a small, real set of ELF programs to run -
 * and so the ELF loader has something to load besides a fixture.
 *
 * Usage: elfcat [-n] file...
 *
 *   -n   number the output lines, starting at 1
 *
 * Reading is chunked rather than whole-file: the C library's stdio hands a
 * whole file to fopen on this filesystem, but a tool that will one day read
 * something larger than a program window must not depend on that.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mv/args.h>
#include <mv/types.h>

#define CHUNK 512

static int cat(const char *path, int number, unsigned long *line)
{
    FILE *f;
    char buf[CHUNK];
    size_t n, i;
    int at_line_start = 1;

    f = fopen(path, "rb");
    if (f == 0) {
        fprintf(stderr, "elfcat: cannot open %s\n", path);
        return 1;
    }

    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        for (i = 0; i < n; i++) {
            if (at_line_start && number) {
                printf("%6lu  ", *line);
                at_line_start = 0;
            }
            putchar(buf[i]);
            if (buf[i] == '\n') {
                (*line)++;
                at_line_start = 1;
            }
        }
    }
    fclose(f);
    return 0;
}

int main(void)
{
    static char args[MV_ARGS_BUFSZ];
    char *argv[MV_ARGS_MAX];
    int argc = mv_args("elfcat", args, sizeof args, argv, MV_ARGS_MAX);
    int number = 0;
    int opt;
    int files = 0;
    int rc = 0;
    unsigned long line = 1;

    opterr = 0;
    while ((opt = getopt(argc, argv, "n")) != -1) {
        if (opt == 'n') {
            number = 1;
        } else {
            fprintf(stderr, "elfcat: unknown option -%c\n", optopt);
            return 1;
        }
    }

    for (; optind < argc; optind++) {
        files++;
        if (cat(argv[optind], number, &line) != 0)
            rc = 1;
    }

    if (files == 0) {
        fprintf(stderr, "elfcat: no file named\n");
        return 1;
    }
    return rc;
}
