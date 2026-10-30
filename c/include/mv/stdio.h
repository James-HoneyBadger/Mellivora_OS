/*
 * stdio.h - the tree's original name for the C library's stdio
 *
 * The declarations live in <stdio.h> now; this header is the spelling the
 * older sources in this tree use, and it is kept so they do not have to be
 * rewritten to gain what the library gained.
 */
#ifndef MV_STDIO_H
#define MV_STDIO_H

#include <stdio.h>

/* The runtime's own console calls live in <mv/syscall.h>: a program that only
 * prints does not need a FILE to do it. */
#define MV_EOF EOF

#endif /* MV_STDIO_H */
