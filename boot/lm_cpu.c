/*
 * lm_cpu.c - per-CPU state for the 64-bit nucleus
 *
 * Roadmap P2. A kernel needs, on every processor, a place to find "my" state
 * without looking at a global: on x86-64 that is the GS base, set through
 * MSR_GS_BASE (kernel) and MSR_KERNEL_GS_BASE (what SWAPGS trades with).
 *
 * The nucleus is single-CPU, so there is one block and cpu_this() could just
 * return its address - but the SYSCALL entry stub reaches the same block
 * through %gs without calling anything, because at that point it has no valid
 * C stack of its own. That is the property the port needs, so it is what the
 * nucleus sets up and tests.
 */

#include "lm.h"

#define MSR_GS_BASE 0xC0000101U

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

    /* GS base: what the entry stub reads with %gs:0x10 etc. */
    wrmsr_gs_base(MSR_GS_BASE, cpu->self);
    return 0;
}

struct lm_cpu *cpu_this(void)
{
    return &cpu0_block;
}
