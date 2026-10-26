/*
 * kmod.c - the first C-built kernel module
 *
 * This is the proof of the C half of docs/spec/ABI.md, not a feature. It runs
 * in ring 0, in the kernel's address space, and it passes only if:
 *
 *   - the assembler, the linker script and this file agree on where the blob
 *     lands and what its header looks like (init is called at all, and the
 *     host table it receives has the size it was compiled against);
 *   - the freestanding runtime works, since the compiler is free to rewrite
 *     this file's loops into calls to memcpy/memset;
 *   - data, stack and rodata are all addressable, which no amount of linking
 *     proves on its own;
 *   - the C-to-assembly direction of the boundary works: host->add is a NASM
 *     routine reached through a function pointer with cdecl arguments.
 *
 * It prints nothing itself except through host->print (vga_print), so the
 * same code would run whether the console is text or serial.
 */
#include <mv/cmod.h>
#include <mv/string.h>
#include <mv/types.h>

static const cmod_host *host;

/* Reasons, in the order the checks run. The kernel prints the code, so a
 * failure names the check that failed instead of "something went wrong". */
enum {
    CMOD_ERR_NONE = 0,
    CMOD_ERR_HOST = 1,
    CMOD_ERR_STRLEN = 2,
    CMOD_ERR_MEMSET = 3,
    CMOD_ERR_MEMCPY = 4,
    CMOD_ERR_MEMMOVE = 5,
    CMOD_ERR_MEMCMP = 6,
    CMOD_ERR_STRCMP = 7,
    CMOD_ERR_HOST_CALL = 8,
};

static const char *const reasons[] = {
    "ok",
    "host table rejected",
    "strlen",
    "memset",
    "memcpy",
    "memmove",
    "memcmp",
    "strcmp",
    "host->add",
};

static const char banner[] =
    "CMOD: C module v1 in ring 0 (runtime ok, asm boundary ok)\r\n";

static i32 check_strlen(void)
{
    if (strlen("") != 0)
        return -1;
    if (strlen("Mellivora") != 9)
        return -1;
    if (strlen("a") != 1)
        return -1;
    return 0;
}

static i32 check_memset(void)
{
    u8 buf[37];
    u32 i;

    memset(buf, 0x5A, sizeof(buf));
    for (i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0x5A)
            return -1;
    }
    /* A partial fill must not touch the byte past it. */
    memset(buf, 0, 4);
    for (i = 0; i < 4; i++) {
        if (buf[i] != 0)
            return -1;
    }
    if (buf[4] != 0x5A)
        return -1;
    return 0;
}

static i32 check_memcpy(void)
{
    static const u8 src[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    };
    u8 dst[16];

    memset(dst, 0, sizeof(dst));
    memcpy(dst, src, sizeof(dst));
    if (memcmp(dst, src, sizeof(dst)) != 0)
        return -1;
    /* Copying nothing must change nothing. */
    memcpy(dst, src, 0);
    if (memcmp(dst, src, sizeof(dst)) != 0)
        return -1;
    return 0;
}

/* memmove is the one with a direction: the destination may start above the
 * source, where a forward copy would overwrite bytes it has not read yet. */
static i32 check_memmove(void)
{
    u8 buf[12];
    static const u8 pattern[12] = {
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    };
    static const u8 right[12] = {
        'a', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k',
    };
    static const u8 left[12] = {
        'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'k', 'l',
    };

    memcpy(buf, pattern, sizeof(buf));
    memmove(buf + 1, buf, 11);          /* overlapping, destination above */
    if (memcmp(buf, right, sizeof(buf)) != 0)
        return -1;

    memcpy(buf, pattern, sizeof(buf));
    memmove(buf, buf + 2, 10);          /* overlapping, destination below */
    if (memcmp(buf, left, sizeof(buf)) != 0)
        return -1;

    memcpy(buf, pattern, sizeof(buf));
    memmove(buf, buf, sizeof(buf));     /* no-op must not corrupt */
    if (memcmp(buf, pattern, sizeof(buf)) != 0)
        return -1;
    return 0;
}

static i32 check_memcmp(void)
{
    static const u8 a[4] = { 1, 2, 3, 4 };
    static const u8 b[4] = { 1, 2, 3, 4 };
    static const u8 c[4] = { 1, 2, 3, 5 };

    if (memcmp(a, b, sizeof(a)) != 0)
        return -1;
    if (memcmp(a, c, sizeof(a)) >= 0)
        return -1;
    if (memcmp(c, a, sizeof(a)) <= 0)
        return -1;
    if (memcmp(a, c, 3) != 0)           /* equal over the first three */
        return -1;
    return 0;
}

static i32 check_strcmp(void)
{
    if (strcmp("", "") != 0)
        return -1;
    if (strcmp("abc", "abc") != 0)
        return -1;
    if (strcmp("abc", "abd") >= 0)
        return -1;
    if (strcmp("abd", "abc") <= 0)
        return -1;
    if (strcmp("abc", "ab") <= 0)
        return -1;
    if (strncmp("abcdef", "abcxyz", 3) != 0)
        return -1;
    if (strncmp("abcdef", "abcxyz", 4) >= 0)
        return -1;
    return 0;
}

/* The C-to-assembly half. 20 + 22 = 42 is small enough to read off the
 * console and specific enough that a wrong argument order gives something
 * else. */
static i32 check_host_call(void)
{
    if (host->add(20, 22) != 42)
        return -1;
    if (host->add(0, 0) != 0)
        return -1;
    if (host->add(0xFFFFFFFEu, 1) != 0xFFFFFFFFu)
        return -1;
    return 0;
}

static i32 cmod_init(const cmod_host *h)
{
    if (h == NULL)
        return -1;
    if (h->size != sizeof(cmod_host))
        return -1;
    if (h->api_version != CMOD_HOST_API_VERSION)
        return -1;
    if (h->add == NULL || h->print == NULL)
        return -1;

    host = h;
    return 0;
}

static i32 cmod_selftest(void)
{
    i32 rc = CMOD_ERR_NONE;

    if (host == NULL) {
        rc = CMOD_ERR_HOST;
    } else if (check_strlen() != 0) {
        rc = CMOD_ERR_STRLEN;
    } else if (check_memset() != 0) {
        rc = CMOD_ERR_MEMSET;
    } else if (check_memcpy() != 0) {
        rc = CMOD_ERR_MEMCPY;
    } else if (check_memmove() != 0) {
        rc = CMOD_ERR_MEMMOVE;
    } else if (check_memcmp() != 0) {
        rc = CMOD_ERR_MEMCMP;
    } else if (check_strcmp() != 0) {
        rc = CMOD_ERR_STRCMP;
    } else if (check_host_call() != 0) {
        rc = CMOD_ERR_HOST_CALL;
    }

    if (rc == CMOD_ERR_NONE) {
        host->print(banner);
    } else {
        host->print("CMOD: check failed: ");
        host->print(reasons[rc]);
        host->print("\r\n");
    }
    return rc;
}

/* The linker script places this object's .cmod_head first, so the header is
 * at offset 0 of the blob where kernel/cmod.inc expects it. */
__attribute__((section(".cmod_head"), used))
const cmod_header cmod_export = {
    CMOD_MAGIC,
    CMOD_VERSION,
    (u32)sizeof(cmod_header),
    cmod_init,
    cmod_selftest,
    "kmod",
};
