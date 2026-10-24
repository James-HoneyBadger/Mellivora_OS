/*
 * lm_kmem.c - the kernel slab allocator for the 64-bit nucleus
 *
 * Roadmap P2. The 32-bit tree's kernel/kmem.inc has the same shape: a cache
 * hands out one fixed-size object at a time and takes a page only when its free
 * list runs dry, free objects are threaded through their own first eight bytes,
 * pages are never given back (that is the point of a slab), and every object is
 * zeroed before it is handed out so a recycled slot cannot leak the previous
 * owner's contents.
 *
 * The 64-bit difference is where the pages come from. A slab page is a frame
 * from the PMM mapped by the VMM at a dedicated kernel-heap window, not a
 * physical address used directly: the heap has an address range of its own, so
 * growing it never collides with the identity map, and every page is reached
 * through a real mapping that `vmm_translate` can explain.
 */

#include "lm.h"

#define KMEM_CACHES 8

struct kmem_cache {
    u64 object_size;
    u64 per_slab;
    u64 free;        /* first free object, 0 when the list is empty */
    u64 used;        /* objects currently handed out */
    u64 slabs;       /* pages this cache has taken */
};

static struct kmem_cache caches[KMEM_CACHES];
static int cache_count;
static u64 heap_pages_used;

void kmem_init(void)
{
    for (int i = 0; i < KMEM_CACHES; i++) {
        caches[i].object_size = 0;
        caches[i].per_slab = 0;
        caches[i].free = 0;
        caches[i].used = 0;
        caches[i].slabs = 0;
    }
    cache_count = 0;
    heap_pages_used = 0;
}

/*
 * Register (or find) a cache for objects of `size` bytes. The free list lives
 * in the object itself, so anything smaller than a pointer cannot be a slab
 * object. Returns the cache id, or -1 when the size is out of range or the
 * table is full.
 */
int kmem_cache(u64 size)
{
    if (size < sizeof(u64) || size > FRAME_SIZE) {
        return -1;
    }
    for (int i = 0; i < cache_count; i++) {
        if (caches[i].object_size == size) {
            return i;
        }
    }
    if (cache_count == KMEM_CACHES) {
        return -1;
    }

    struct kmem_cache *c = &caches[cache_count];
    c->object_size = size;
    c->per_slab = FRAME_SIZE / size;
    c->free = 0;
    c->used = 0;
    c->slabs = 0;
    return cache_count++;
}

/* Thread a fresh page's objects onto the cache's free list. */
static int kmem_new_slab(int id)
{
    struct kmem_cache *c = &caches[id];

    if (heap_pages_used >= KHEAP_PAGES) {
        return 0;
    }
    u64 frame = pmm_alloc_frame();
    if (frame == 0) {
        return 0;
    }

    u64 virt = KHEAP_BASE + heap_pages_used * FRAME_SIZE;
    if (vmm_map(virt, frame, VMM_W | VMM_NX) != 0) {
        pmm_free_frame(frame);
        return 0;
    }

    u8 *page = (u8 *)(unsigned long)virt;
    memset(page, 0, FRAME_SIZE);
    for (u64 i = c->per_slab; i-- > 0;) {
        u64 obj = virt + i * c->object_size;
        *(u64 *)(unsigned long)obj = c->free;
        c->free = obj;
    }

    heap_pages_used++;
    c->slabs++;
    return 1;
}

void *kmem_alloc(int id)
{
    if (id < 0 || id >= cache_count) {
        return NULL;
    }
    struct kmem_cache *c = &caches[id];

    if (c->free == 0 && !kmem_new_slab(id)) {
        return NULL;
    }

    u64 obj = c->free;
    c->free = *(u64 *)(unsigned long)obj;
    memset((void *)(unsigned long)obj, 0, c->object_size);
    c->used++;
    return (void *)(unsigned long)obj;
}

void kmem_free(int id, void *obj)
{
    if (obj == NULL || id < 0 || id >= cache_count) {
        return;
    }
    u64 addr = (u64)(unsigned long)obj;
    if (addr < KHEAP_BASE || addr >= KHEAP_BASE + KHEAP_PAGES * FRAME_SIZE) {
        return;                     /* not this heap: refuse rather than corrupt */
    }

    struct kmem_cache *c = &caches[id];
    *(u64 *)(unsigned long)addr = c->free;
    c->free = addr;
    if (c->used > 0) {
        c->used--;
    }
}

struct kmem_stats kmem_get_stats(void)
{
    struct kmem_stats s = {0, 0, 0, 0};

    s.caches = (u64)cache_count;
    for (int i = 0; i < cache_count; i++) {
        s.slabs += caches[i].slabs;
        s.used += caches[i].used;
        s.objects += caches[i].per_slab * caches[i].slabs;
    }
    return s;
}

/*
 * Self-test: two slabs of a 64-byte cache, all objects distinct and zeroed,
 * every object reachable through the VMM and out of ring 3's reach, a write to
 * each landing in its own object, and a recycled object coming back zeroed.
 */
int kmem_selftest(void)
{
    int id = kmem_cache(64);
    if (id < 0) {
        kputs("[kmem] FAIL no cache\r\n");
        return -1;
    }

    struct kmem_cache *c = &caches[id];
    u64 count = c->per_slab + 8;    /* enough to force a second slab */
    void *obj[128];

    if (count > sizeof(obj) / sizeof(obj[0])) {
        kputs("[kmem] FAIL cache too large for the test\r\n");
        return -1;
    }

    int ok = 1;
    u64 i;

    for (i = 0; i < count; i++) {
        obj[i] = kmem_alloc(id);
        if (obj[i] == NULL) {
            ok = 0;
            break;
        }
        for (u64 j = 0; j < i; j++) {
            if (obj[j] == obj[i]) {
                ok = 0;             /* the same object handed out twice */
            }
        }
        u64 addr = (u64)(unsigned long)obj[i];
        if (vmm_translate(addr) == 0 || vmm_user_mapped(addr) != 0) {
            ok = 0;                 /* not mapped, or reachable from ring 3 */
        }
        if (*(u64 *)obj[i] != 0) {
            ok = 0;                 /* a fresh object was not zeroed */
        }
        *(u64 *)obj[i] = 0x5A5A000000000000ULL | i;
    }

    if (c->slabs < 2) {
        ok = 0;                     /* the second slab was never taken */
    }

    for (i = 0; i < count; i++) {
        if (obj[i] == NULL) {
            continue;
        }
        if (*(u64 *)obj[i] != (0x5A5A000000000000ULL | i)) {
            ok = 0;                 /* two objects share a slot */
        }
        kmem_free(id, obj[i]);
    }
    if (c->used != 0) {
        ok = 0;
    }

    /* A recycled object must look new: the cache zeroes on alloc. */
    void *again = kmem_alloc(id);
    if (again == NULL || *(u64 *)again != 0) {
        ok = 0;
    }
    kmem_free(id, again);

    struct kmem_stats s = kmem_get_stats();
    kputs("[kmem] caches=");
    kput_dec(s.caches);
    kputs(" slabs=");
    kput_dec(s.slabs);
    kputs(" objects=");
    kput_dec(s.objects);
    kputs("\r\n");
    kputs(ok ? "[kmem] kmem=ok\r\n" : "[kmem] FAIL slab round trip\r\n");
    return ok ? 0 : -1;
}
