/*
 * mvprobe.c - the smallest program that can be built as an HBX image with an
 * import: it proves that a C payload survives the wrapper and that a library
 * call crosses the import slot. It exists because the M5 exit criterion asks
 * for an application that *uses shared libraries*, and before a whole
 * application can, the two halves - statics in an HBX payload, and a slot the
 * loader patches - have to be shown separately.
 */
#include <stdio.h>
#include <string.h>

int (*mvlib_add)(int a, int b) = 0;     /* the loader writes the address here */
static int calls;                       /* a static: proves .data/.bss survived */
static char text[64];                   /* and so does a writable array */

int main(void)
{
    int sum = -1;

    calls++;
    strcpy(text, "probe");
    printf("PROBE statics=%d text=%s slot=%d\n", calls, text,
           mvlib_add != 0);

    if (mvlib_add != 0) {
        __asm__ volatile("pushl %%ebx\n\t"
                         "movl %1, %%ebx\n\t"
                         "movl %2, %%ecx\n\t"
                         "call *%3\n\t"
                         "popl %%ebx\n\t"
                         "movl %%eax, %0"
                         : "=r"(sum)
                         : "r"(20), "r"(22), "r"(mvlib_add)
                         : "eax", "ecx", "memory");
    }
    printf("PROBE sum=%d\n", sum);
    printf(sum == 42 ? "MVPROBE_OK\n" : "MVPROBE_FAIL\n");
    return 0;
}
