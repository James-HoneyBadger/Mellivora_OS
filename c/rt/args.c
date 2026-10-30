/*
 * args.c - the command line, split into argv
 *
 * See <mv/args.h> for what this does and does not do. It is a separate file from
 * the rest of the runtime because only a program wants it: the kernel module and
 * the libraries have no command line.
 */
#include <mv/args.h>
#include <mv/syscall.h>
#include <mv/string.h>

int mv_args(const char *prog, char *buf, u32 size, char *argv[], int max)
{
    int argc = 0;
    char *p = buf;

    if (max < 1)
        return 0;
    argv[argc++] = (char *)prog;

    (void)size;
    if (mv_getargs(buf, MV_ARGS_BUFSZ) < 0) {
        buf[0] = '\0';
        return argc;
    }

    while (*p != '\0' && argc < max) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0')
            break;
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
    }
    return argc;
}
