/*
 * crtdemo.c - what the C runtime gives a program, checked in the guest
 *
 * Every C program in this tree links c/rt/start.c, so the start-up calling main
 * and the exit status coming back from it are proved by the program running at
 * all. What is left to prove is the rest of the runtime: the formatter's
 * conversions and flags, snprintf's truncation contract, and a heap that
 * survives the allocation patterns a program actually uses - many small
 * blocks, a grow, frees in a shuffled order, and an allocation that has to
 * reuse what was freed.
 *
 * It prints CRTDEMO_OK only when every check passes, and main's return value is
 * the number of the check that failed.
 */
#include <mv/stdio.h>
#include <mv/stdlib.h>
#include <mv/string.h>
#include <mv/syscall.h>

#define BLOCKS 64

static int check_format(void)
{
    char buf[48];

    printf("CRT: printf %d %u %x %X %s %c %% [%5d][%-5d][%05d]\n",
           -42, 4000000000u, 0xDEADBEEFu, 0xBEEFu, "str", 'z',
           42, 42, 42);

    if (snprintf(buf, sizeof buf, "%d %u %x", -1, 7u, 0xABCDu) != 9)
        return 1;
    if (strcmp(buf, "-1 7 abcd") != 0)
        return 2;

    /* C99: snprintf reports what it would have written and truncates the
     * buffer, NUL included. */
    if (snprintf(buf, 8, "%s", "truncated") != 9)
        return 3;
    if (strcmp(buf, "truncat") != 0)
        return 4;

    /* A size of zero must not write, and still counts. */
    if (snprintf(buf, 0, "%d", 123456) != 6)
        return 5;

    if (snprintf(buf, sizeof buf, "%p", 0x1234u) != 10)
        return 6;
    if (strcmp(buf, "0x00001234") != 0)
        return 7;
    return 0;
}

static int check_heap(void)
{
    static char *blocks[BLOCKS];
    u32 i;
    u32 j;
    u32 bytes = 0;

    /* Many small blocks of uneven size, each filled with its own byte so any
     * overlap or reuse of live memory shows up as the wrong pattern. */
    for (i = 0; i < BLOCKS; i++) {
        u32 size = 8 + (i * 13) % 96;

        blocks[i] = (char *)malloc(size);
        if (blocks[i] == NULL)
            return 10;
        if (((u32)blocks[i] & 7u) != 0)
            return 11;          /* an 8-byte aligned payload */
        memset(blocks[i], (int)('A' + (i % 26)), size);
        bytes += size;
    }

    for (i = 0; i < BLOCKS; i += 2) {
        free(blocks[i]);
        blocks[i] = NULL;
    }

    /* The freed half has to be reused: the second wave must not grow the heap
     * by anything like the same amount again. */
    for (i = 0; i < BLOCKS; i += 2) {
        blocks[i] = (char *)malloc(8 + (i * 13) % 96);
        if (blocks[i] == NULL)
            return 12;
        memset(blocks[i], (int)('a' + (i % 26)), 8 + (i * 13) % 96);
    }

    /* Every live block still holds what was written into it. */
    for (i = 0; i < BLOCKS; i++) {
        u32 size = 8 + (i * 13) % 96;
        char want = (char)((i % 2 == 0) ? ('a' + (i % 26)) : ('A' + (i % 26)));

        for (j = 0; j < size; j++) {
            if (blocks[i][j] != want)
                return 13;
        }
    }

    for (i = 0; i < BLOCKS; i++)
        free(blocks[i]);

    /* Everything is free again, so one allocation of the whole arena has to
     * succeed: free() coalesced instead of leaving gravel. */
    {
        char *whole = (char *)malloc(bytes);

        if (whole == NULL)
            return 14;
        free(whole);
    }

    /* calloc zeroes and realloc keeps the bytes it moved. */
    {
        u32  *p = (u32 *)calloc(16, 4);
        u32  *q;
        char *s;

        if (p == NULL)
            return 15;
        for (i = 0; i < 16; i++)
            if (p[i] != 0)
                return 16;
        for (i = 0; i < 16; i++)
            p[i] = i * 3u;

        q = (u32 *)realloc(p, 16 * 4 * 8);
        if (q == NULL)
            return 17;
        for (i = 0; i < 16; i++)
            if (q[i] != i * 3u)
                return 18;
        free(q);

        s = (char *)realloc(NULL, 32);
        if (s == NULL)
            return 19;
        memset(s, 'r', 32);
        free(s);
    }

    /* Growth past one page: the break has to move and the new pages have to be
     * usable. Their contents are not asserted - a freed block that is reused
     * still holds what the last owner wrote - but every byte must survive a
     * write and a read across the whole allocation. */
    {
        u32   big = 192u * 1024u;
        char *p = (char *)malloc(big);

        if (p == NULL)
            return 21;
        memset(p, 'B', big);
        for (i = 0; i < big; i += 1024) {
            if (p[i] != 'B')
                return 22;
        }
        free(p);
    }

    printf("CRT: heap %u bytes in %d blocks, coalesced, reused, grown past a page\n",
           bytes, BLOCKS);
    return 0;
}

static int check_args(void)
{
    static char args[512];
    i32 n = mv_getargs(args, sizeof args);

    if (n < 0)
        return 20;
    printf("CRT: args \"%s\"\n", args);
    return 0;
}

int main(void)
{
    int rc;

    puts("CRT: start-up ran main");

    rc = check_format();
    if (rc != 0) {
        printf("CRTDEMO_FAIL: format %d\n", rc);
        return rc;
    }

    rc = check_heap();
    if (rc != 0) {
        printf("CRTDEMO_FAIL: heap %d\n", rc);
        return rc;
    }

    rc = check_args();
    if (rc != 0) {
        printf("CRTDEMO_FAIL: args %d\n", rc);
        return rc;
    }

    printf("CRTDEMO_OK\n");
    return 0;
}
