/*
 * lm_pmm.c - physical frame allocator for the 64-bit nucleus
 *
 * Roadmap P2, first ported subsystem. It is deliberately the simplest thing
 * that can be correct on 64-bit hardware: a bitmap with one bit per 4 KB frame,
 * filled in from the E820 map the firmware produced, with the low megabyte and
 * the nucleus's own image taken out.
 *
 * 64-bit specifics this has to get right, and a 32-bit allocator would not:
 *   - E820 entries have 64-bit base/length, and a guest with 4 GB or more RAM
 *     has usable ranges above 0x100000000 (there is a hole below 4 GB), so the
 *     arithmetic is done in u64 throughout;
 *   - physical addresses do not fit in 32 bits, so every frame value returned
 *     here is a u64 and 0 is a valid-looking address - callers must use the
 *     "0 means failure" convention this API documents;
 *   - the bitmap is sized for the largest configuration the roadmap gates on
 *     (8 GB = 2 M frames = 256 KB of bitmap), which lives in .bss.
 *
 * A bitmap over the whole 8 GB is 256 KB and never grows, which is fine for a
 * boot allocator; the buddy allocator with orders replaces it when the port
 * needs contiguous runs for DMA.
 */

#include "lm.h"

/* stage2 leaves these at 0x500 in the BIOS-safe area. */
#define BOOTINFO_MMAP_CNT 0x504U
#define BOOTINFO_MMAP_PTR 0x508U

#define MAX_FRAMES     (2ULL * 1024 * 1024)     /* 8 GB of 4 KB frames */
#define BITMAP_BYTES   (MAX_FRAMES / 8)
#define LOW_MEMORY_END 0x00100000ULL            /* BIOS, stage2, page tables */
#define KERNEL_LOAD_ADDR 0x00100000ULL          /* stage2 drops the image here */

/* E820 entry as the BIOS fills it in (ECX = 24). */
struct e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 attributes;
} __attribute__((packed));

#define E820_TYPE_USABLE 1U

/* Declared by boot/lm64.ld. */
extern u8 lm_image_start[];
extern u8 lm_image_end[];

static u8 frame_used[BITMAP_BYTES];
static struct pmm_stats stats;
static u32 frame_count;
static u32 alloc_hint;

static int bit_test(const u8 *map, u32 frame)
{
    return (map[frame >> 3] >> (frame & 7)) & 1;
}

static void bit_set(u8 *map, u32 frame)
{
    map[frame >> 3] = (u8)(map[frame >> 3] | (1U << (frame & 7)));
}

static void bit_clear(u8 *map, u32 frame)
{
    map[frame >> 3] = (u8)(map[frame >> 3] & ~(1U << (frame & 7)));
}

/* Mark [first, end) frames used, counting the ones that were free. */
static void frames_reserve(u32 first, u32 end)
{
    if (end > frame_count) {
        end = frame_count;
    }
    for (u32 f = first; f < end; f++) {
        if (!bit_test(frame_used, f)) {
            bit_set(frame_used, f);
            stats.free_frames--;
        }
    }
}

/* Mark [first, end) frames free, counting the ones that were used. */
static void frames_release(u32 first, u32 end)
{
    if (end > frame_count) {
        end = frame_count;
    }
    for (u32 f = first; f < end; f++) {
        if (!bit_test(frame_used, f)) {
            continue; /* already free: never count a frame twice */
        }
        bit_clear(frame_used, f);
        stats.free_frames++;
    }
}

int pmm_init(void)
{
    u32 count = lm_read32(BOOTINFO_MMAP_CNT);
    u32 ptr = lm_read32(BOOTINFO_MMAP_PTR);
    const struct e820_entry *map = (const struct e820_entry *)(u64)ptr;

    stats.usable_bytes = 0;
    stats.highest_addr = 0;
    stats.free_frames = 0;
    stats.total_frames = 0;
    frame_count = 0;
    alloc_hint = 0;

    if (count == 0 || ptr == 0) {
        return -1;
    }

    /* Pass 1: how much of it can this allocator manage? */
    u64 usable = 0;
    u64 top = 0;
    for (u32 i = 0; i < count; i++) {
        if (map[i].type != E820_TYPE_USABLE || map[i].length == 0) {
            continue;
        }
        u64 base = map[i].base;
        u64 end = base + map[i].length;
        if (end > MAX_FRAMES * FRAME_SIZE) {
            end = MAX_FRAMES * FRAME_SIZE;
        }
        if (end <= base) {
            continue;
        }
        usable += end - base;
        if (end > top) {
            top = end;
        }
    }
    if (top == 0) {
        return -1;
    }

    frame_count = (u32)(top / FRAME_SIZE);
    stats.total_frames = frame_count;
    stats.usable_bytes = usable;
    stats.highest_addr = top;

    /* Everything starts reserved; the usable ranges are then handed back. */
    for (u32 i = 0; i < BITMAP_BYTES; i++) {
        frame_used[i] = 0xFF;
    }
    stats.free_frames = 0;

    for (u32 i = 0; i < count; i++) {
        if (map[i].type != E820_TYPE_USABLE || map[i].length == 0) {
            continue;
        }
        u64 base = map[i].base;
        u64 end = base + map[i].length;
        if (base < LOW_MEMORY_END) {
            base = LOW_MEMORY_END;      /* BIOS data, stage2, page tables */
        }
        if (end > (u64)frame_count * FRAME_SIZE) {
            end = (u64)frame_count * FRAME_SIZE;
        }
        if (end <= base) {
            continue;
        }
        frames_release((u32)(base / FRAME_SIZE),
                       (u32)((end + FRAME_SIZE - 1) / FRAME_SIZE));
    }

    /* The nucleus itself: image, .bss (the stack lives there) and all. */
    u64 image_phys = KERNEL_LOAD_ADDR;
    u64 image_bytes = (u64)(unsigned long)(lm_image_end - lm_image_start);
    frames_reserve((u32)(image_phys / FRAME_SIZE),
                   (u32)((image_phys + image_bytes + FRAME_SIZE - 1) / FRAME_SIZE));

    return stats.free_frames == 0 ? -1 : 0;
}

struct pmm_stats pmm_get_stats(void)
{
    return stats;
}

u64 pmm_alloc_frame(void)
{
    if (stats.free_frames == 0) {
        return 0;
    }
    /* Rotate through the bitmap so successive allocations walk memory instead
     * of rescanning frame 0 every time. */
    for (u32 scanned = 0; scanned < frame_count; scanned++) {
        u32 frame = alloc_hint;
        alloc_hint++;
        if (alloc_hint >= frame_count) {
            alloc_hint = 0;
        }
        if (bit_test(frame_used, frame)) {
            continue;
        }
        bit_set(frame_used, frame);
        stats.free_frames--;
        return (u64)frame * FRAME_SIZE;
    }
    return 0;
}

int pmm_free_frame(u64 addr)
{
    if ((addr & (FRAME_SIZE - 1)) != 0) {
        return -1;                       /* not a frame boundary */
    }
    u64 frame = addr / FRAME_SIZE;
    if (frame == 0 || frame >= frame_count) {
        return -1;                       /* outside what we manage */
    }
    if (!bit_test(frame_used, (u32)frame)) {
        return -1;                       /* already free: double free */
    }
    bit_clear(frame_used, (u32)frame);
    stats.free_frames++;
    return 0;
}
