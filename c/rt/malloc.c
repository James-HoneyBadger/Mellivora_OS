/*
 * malloc.c - a first-fit heap over SYS_SBRK
 *
 * The heap is the program's own break, so it lives where the loader already
 * said the image ends and it grows towards the user stack. Every block starts
 * with a 16-byte header - the payload size, a used flag, and the two links the
 * free list needs - which puts the payload on an 8-byte boundary because the
 * break is page-aligned and every header is a multiple of 8.
 *
 * Free blocks live in one list kept sorted by address. A free therefore looks
 * at its neighbours by walking that list: forward coalescing is what keeps a
 * loop of allocate/free/allocate from turning the heap into gravel, and
 * backward coalescing is what lets a whole arena collapse into one block once
 * the program lets go of it.
 *
 * When the list cannot satisfy a request the heap grows by at least a page, so
 * a program that asks for 12 bytes does not make a system call for every
 * allocation.
 */
#include <mv/stdlib.h>
#include <mv/string.h>
#include <mv/syscall.h>

#define BLK_USED 0x5A5A5A5Au
#define BLK_FREE 0x00000000u
#define HDR_SIZE 16u
#define MIN_SPLIT (HDR_SIZE + 8u)
#define GROW_MIN 4096u

typedef struct blk {
    u32         size;   /* payload bytes, a multiple of 8 */
    u32         magic;  /* BLK_USED or BLK_FREE */
    struct blk *next;   /* free list, by address */
    u32         pad;    /* keeps the payload 8-aligned */
} blk_t;

static blk_t *free_list;

static u32 align8(u32 n)
{
    return (n + 7u) & ~7u;
}

/* Ask the kernel for `bytes` more heap; NULL when the window has no room. */
static void *grow_heap(u32 bytes)
{
    i32 old = mv_sbrk((i32)bytes);

    if (old < 0)
        return NULL;
    return (void *)old;
}

static void list_insert(blk_t *block)
{
    blk_t **at = &free_list;

    block->magic = BLK_FREE;

    /* Sorted by address, so coalescing can look at the neighbour that follows
     * and only has to remember the one that came before. */
    while (*at != NULL && (char *)*at < (char *)block)
        at = &(*at)->next;
    block->next = *at;
    *at = block;
}

/* Merge `block` with the free block that follows it when they touch. */
static void merge_forward(blk_t *block)
{
    blk_t *next = block->next;

    if (next != NULL && (char *)block + HDR_SIZE + block->size == (char *)next) {
        block->size += HDR_SIZE + next->size;
        block->next = next->next;
    }
}

void *malloc(u32 size)
{
    blk_t *block;
    blk_t *prev;
    u32    need = align8(size == 0 ? 1 : size);

    for (;;) {
        /* First fit: the list is short, and a best-fit search would cost more
         * than the fragmentation it saves. */
        prev = NULL;
        block = free_list;
        while (block != NULL && block->size < need) {
            prev = block;
            block = block->next;
        }
        if (block != NULL)
            break;

        {
            u32    grow = need + HDR_SIZE;
            blk_t *fresh;

            if (grow < GROW_MIN)
                grow = GROW_MIN;
            grow = align8(grow);
            fresh = (blk_t *)grow_heap(grow);
            if (fresh == NULL)
                return NULL;
            fresh->size = grow - HDR_SIZE;
            list_insert(fresh);
        }
    }

    /* Split when the tail can hold a header and the smallest useful payload; a
     * smaller tail stays part of this block rather than becoming a fragment
     * nothing can ever use. */
    if (block->size >= need + MIN_SPLIT) {
        blk_t *rest = (blk_t *)((char *)block + HDR_SIZE + need);

        rest->size = block->size - need - HDR_SIZE;
        rest->pad = 0;
        block->size = need;
        if (prev == NULL)
            free_list = rest;
        else
            prev->next = rest;
        rest->next = block->next;
        rest->magic = BLK_FREE;
        merge_forward(rest);
    } else if (prev == NULL) {
        free_list = block->next;
    } else {
        prev->next = block->next;
    }

    block->next = NULL;
    block->magic = BLK_USED;
    return (char *)block + HDR_SIZE;
}

void free(void *ptr)
{
    blk_t *block;
    blk_t *prev;
    blk_t *walk;

    if (ptr == NULL)
        return;

    block = (blk_t *)((char *)ptr - HDR_SIZE);
    if (block->magic != BLK_USED)
        return;                 /* not ours, or already free: do not corrupt */

    list_insert(block);

    /* Backward merge: only the block just before this one in the sorted list
     * can end where this one begins. */
    prev = NULL;
    walk = free_list;
    while (walk != NULL && walk != block) {
        prev = walk;
        walk = walk->next;
    }
    if (prev != NULL && (char *)prev + HDR_SIZE + prev->size == (char *)block) {
        prev->size += HDR_SIZE + block->size;
        prev->next = block->next;
        block = prev;
    }
    merge_forward(block);
}

void *calloc(u32 count, u32 size)
{
    u32   bytes;
    void *p;

    if (count != 0 && size > 0xFFFFFFFFu / count)
        return NULL;            /* the product does not fit a u32 */
    bytes = count * size;
    p = malloc(bytes);
    if (p != NULL)
        memset(p, 0, bytes);
    return p;
}

void *realloc(void *ptr, u32 size)
{
    blk_t *block;
    void  *fresh;

    if (ptr == NULL)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block = (blk_t *)((char *)ptr - HDR_SIZE);
    if (block->magic != BLK_USED)
        return NULL;
    if (block->size >= align8(size))
        return ptr;             /* it already fits; the tail is not returned */

    fresh = malloc(size);
    if (fresh == NULL)
        return NULL;
    memcpy(fresh, ptr, block->size < size ? block->size : size);
    free(ptr);
    return fresh;
}

void exit(int status)
{
    mv_exit(status);
    for (;;)
        ;                       /* the kernel does not return from SYS_EXIT */
}

int atoi(const char *s)
{
    int sign = 1;
    int value = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}
