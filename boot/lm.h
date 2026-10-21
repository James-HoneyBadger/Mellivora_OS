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

#define FRAME_SIZE 4096ULL   /* the page size everything here assumes */

/* The compiler may emit calls to these for a struct copy even freestanding. */
void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int value, unsigned long n);

/* Console: COM1, initialised by the nucleus before anything else prints. */
void kputc(char c);
void kputs(const char *s);
void kput_hex(u64 value, int digits);
void kput_dec(u64 value);

/*---------------------------------------
 * Per-CPU state
 *
 * One block per processor, reachable through the KERNEL_GS_BASE MSR and made
 * visible by SWAPGS at the top of the entry path: the entry stub needs
 * somewhere to keep its own stack pointer without assuming anything about the
 * register state the caller had. Fields are u64 and in this order because the
 * entry stub and the ring-3 exit path address them by byte offset.
 *
 * While ring 0 code runs, GS base is the value the user program sees (0 here)
 * and KERNEL_GS_BASE holds this block; SWAPGS in the entry stub makes the block
 * current, and the exit paths swap it back. That one arrangement serves a
 * SYSCALL taken from ring 0 and from ring 3 alike.
 *---------------------------------------*/

struct lm_cpu {
    u64 self;             /* 0x00  address of this block */
    u64 id;               /* 0x08  processor index */
    u64 kstack_top;       /* 0x10  kernel stack this CPU enters on */
    u64 saved_rsp;        /* 0x18  caller's stack pointer, saved by the entry stub */
    u64 syscall_count;    /* 0x20  calls taken */
    u64 last_rsp;         /* 0x28  stack the handler ran on (must be the kernel one) */
    u64 last_nr;          /* 0x30  last syscall number */
    u64 last_arg;         /* 0x38  last syscall argument */
    u64 user_mode;        /* 0x40  non-zero while the CPU runs a user program */
    u64 user_calls;       /* 0x48  calls taken from ring 3 */
    u64 exit_code;        /* 0x50  argument of the last SYS_EXIT */
    u64 exit_arg;         /* 0x58  second word SYS_EXIT was given */
    u64 last_user_rip;    /* 0x60  user instruction that entered the kernel */
    u64 last_user_rsp;    /* 0x68  user stack pointer it entered with */
};

#define CPU_KSTACK_TOP_OFF 0x10
#define CPU_SAVED_RSP_OFF  0x18
#define CPU_EXIT_CODE_OFF  0x50

/* The processor this nucleus is running on. */
struct lm_cpu *cpu_this(void);
int cpu_init(void);

/*---------------------------------------
 * SYSCALL frame
 *
 * What the entry stub pushes, in the order it is laid out in memory: the five
 * values IRETQ pops last, then every general register the C dispatcher may
 * clobber. The dispatcher may rewrite `rax` (the result) and the return
 * selectors; the stub pops the rest back unchanged.
 *---------------------------------------*/

struct lm_syscall_frame {
    u64 r15, r14, r13, r12, rbp, rbx, r10, r9, r8;
    u64 rdx, rsi, rdi, rax;
    u64 rip, cs, rflags, rsp, ss;
};

/* GDT selectors. The order matters twice over: SYSCALL loads CS/SS from
 * STAR[47:32] and STAR[47:32]+8, and SYSRET loads CS/SS from
 * STAR[63:48]+16 and STAR[63:48]+8, so user data and user code must sit at
 * KDATA+8 and KDATA+16 for the SYSRET form to agree with the IRETQ one. */
#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UDATA 0x18
#define SEL_UCODE 0x20
#define SEL_TSS   0x28
#define RPL_USER  3

/*---------------------------------------
 * Ring-3 path
 *---------------------------------------*/

/*
 * Map the user program and its stack, load the user GDT and TSS, and drop to
 * ring 3. Returns -1 if the address space could not be built; on success it
 * never returns - the program leaves through SYS_EXIT, which reports and halts
 * in lm_user_exited().
 */
int lm_user_run(void);

/* Enter ring 3 at `entry` with the given stack and RFLAGS. Never returns. */
void lm_user_enter(u64 entry, u64 user_rsp, u64 rflags) __attribute__((noreturn));

/* Called by the entry stub once SYS_EXIT has been dispatched. Never returns. */
void lm_user_exited(u64 code) __attribute__((noreturn));

/*---------------------------------------
 * Physical memory manager
 *---------------------------------------*/

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
