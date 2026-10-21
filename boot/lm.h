/*
 * lm.h - shared declarations for the 64-bit nucleus (roadmap P2)
 *
 * Freestanding C11: no headers, no libc. Everything the nucleus and the
 * subsystems it grows use is declared here.
 */
#ifndef LM_H
#define LM_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define NULL ((void *)0)

/* Console: COM1, initialised by the nucleus before anything else prints. */
void kputc(char c);
void kputs(const char *s);
void kput_hex(u64 value, int digits);
void kput_dec(u64 value);

/*---------------------------------------
 * Physical memory manager
 *---------------------------------------*/

#define FRAME_SIZE 4096ULL

struct pmm_stats {
    u64 usable_bytes;  /* usable RAM below the managed ceiling */
    u64 highest_addr;  /* end of the highest usable range */
    u64 free_frames;
    u64 total_frames;  /* frames the bitmap covers */
};

/*
 * Build the frame bitmap from the E820 map stage2 left at 0x504/0x508.
 * Returns 0 on success, -1 when there is no usable memory or the map is not
 * where stage2 says it is.
 */
int pmm_init(void);
struct pmm_stats pmm_get_stats(void);

/* Physical address of a free 4 KB frame, or 0 when memory is exhausted. */
u64 pmm_alloc_frame(void);

/* Give a frame back. Returns 0 when it was ours, -1 when it was not (a double
 * free or an address outside the managed range). */
int pmm_free_frame(u64 addr);

#endif /* LM_H */
