/*
 * args.h - a program's command line, as argv
 *
 * The kernel hands a launched program the flattened command line EXEC.md
 * section 5.1 keeps for the compatibility view: the arguments, space separated,
 * with no argv[0]. A utility needs argv - getopt takes one, and every tool built
 * on it expects the program's own name in slot 0 - so mv_args() splits the
 * flattened string in place and puts the program's name in front.
 *
 * The split is on whitespace, in place: no quoting, no escapes, no globbing.
 * That is the shell's limit too (a filename with a space in it cannot be
 * passed), and it is recorded in docs/HBUTILS.md rather than hidden here.
 */
#ifndef MV_ARGS_H
#define MV_ARGS_H

#include <mv/types.h>

#define MV_ARGS_MAX 32

/* Fills argv (with prog as argv[0]) and returns argc. buf must be at least
 * MV_ARGS_BUFSZ bytes and is used for the flattened string. */
#define MV_ARGS_BUFSZ 512

int mv_args(const char *prog, char *buf, u32 size, char *argv[], int max);

#endif /* MV_ARGS_H */
