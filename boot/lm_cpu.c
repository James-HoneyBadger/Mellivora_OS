/*
 * lm_cpu.c - per-CPU state for the 64-bit nucleus
 *
 * Roadmap P2. A kernel needs, on every processor, a place to find "my" state
 * without looking at a global: on x86-64 that is the GS base.
 *
 * The two halves are set up the way SWAPGS expects them: KERNEL_GS_BASE holds
 * the block, and the live GS base holds what the user program sees (0 here),
 * because SWAPGS *exchanges* the two and the entry stub runs it unconditionally
 * whether the SYSCALL came from ring 0 or ring 3. Ring 0 code therefore cannot
 * use %gs to reach the block - it calls cpu_this(), which is a direct address.
 */

#include "lm.h"

#define MSR_GS_BASE         0xC0000101U
#define MSR_KERNEL_GS_BASE  0xC0000102U

/* 8 KB is enough for the entry path plus a C handler; a ported kernel sizes
 * this per CPU at boot. */
#define KSTACK_BYTES 8192

static u8 cpu0_kstack[KSTACK_BYTES] __attribute__((aligned(16)));

static struct lm_cpu cpu0_block;

static void wrmsr_gs_base(u32 msr, u64 value)
{
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

int cpu_init(void)
{
    struct lm_cpu *cpu = &cpu0_block;

    cpu->self = (u64)(unsigned long)cpu;
    cpu->id = 0;
    cpu->kstack_top = (u64)(unsigned long)(cpu0_kstack + KSTACK_BYTES);
    cpu->saved_rsp = 0;
    cpu->syscall_count = 0;
    cpu->last_rsp = 0;
    cpu->last_nr = 0;
    cpu->last_arg = 0;
    cpu->user_mode = 0;
    cpu->user_calls = 0;
    cpu->exit_code = 0;
    cpu->exit_arg = 0;
    cpu->last_user_rip = 0;
    cpu->last_user_rsp = 0;

    /* SWAPGS trades these two, so the block goes in KERNEL_GS_BASE and the
     * live GS base keeps the value a user program would have (zero). */
    wrmsr_gs_base(MSR_GS_BASE, 0);
    wrmsr_gs_base(MSR_KERNEL_GS_BASE, cpu->self);
    return 0;
}

struct lm_cpu *cpu_this(void)
{
    return &cpu0_block;
}
