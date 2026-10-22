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
#define MSR_EFER            0xC0000080U
#define EFER_NXE            (1U << 11)      /* no-execute enable */

static int nx_on;
static int smep_on;
static int smap_on;

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

/* The MSR pair, for the parts of the kernel that own a per-task MSR rather than
 * a per-CPU one - the thread-local storage base, which changes with the task. */
void cpu_write_msr(u32 msr, u64 value)
{
    wrmsr_gs_base(msr, value);
}

u64 cpu_read_msr(u32 msr);

static u64 rdmsr(u32 msr)
{
    u32 lo;
    u32 hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static void cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(subleaf));
}

int cpu_nx_enabled(void)
{
    return nx_on;
}

int cpu_smap_enabled(void)
{
    return smap_on;
}

/*
 * Everything here is gated on CPUID: writing EFER.NXE or a CR4 bit an older CPU
 * does not implement is a #GP, and installing an NX leaf without NXE is a
 * reserved-bit #PF. What was turned on is returned so the log says which parts
 * of the hardening this machine actually got.
 */
struct cpu_features cpu_harden(void)
{
    struct cpu_features f = {0, 0, 0};
    u32 a, b, c, d;

    cpuid(0x80000000U, 0, &a, &b, &c, &d);
    if (a >= 0x80000001U) {
        cpuid(0x80000001U, 0, &a, &b, &c, &d);
        if ((d & (1U << 20)) != 0) {        /* NX */
            wrmsr_gs_base(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);
            nx_on = 1;
            f.nx = 1;
        }
    }

    cpuid(0, 0, &a, &b, &c, &d);
    if (a >= 7U) {
        cpuid(7U, 0, &a, &b, &c, &d);
        if ((b & (1U << 7)) != 0) {         /* SMEP */
            u64 cr4;
            __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
            cr4 |= 1ULL << 20;
            __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
            smep_on = 1;
            f.smep = 1;
        }
        if ((b & (1U << 20)) != 0) {        /* SMAP */
            u64 cr4;
            __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
            cr4 |= 1ULL << 21;
            __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
            smap_on = 1;
            f.smap = 1;
        }
    }
    /* SMAP would stop the kernel touching a user address without AC; nothing in
     * this nucleus does, and the page-table walkers in the VMM read tables, not
     * user memory. copy_from_user will bracket its loads with STAC/CLAC. */
    (void)smep_on;
    (void)smap_on;
    return f;
}

u64 cpu_read_msr(u32 msr)
{
    return rdmsr(msr);
}

int cpu_init(void)
{
    struct lm_cpu *cpu = &cpu0_block;

    cpu->self = (u64)(unsigned long)cpu;
    cpu->id = 0;
    cpu->kstack_base = (u64)(unsigned long)cpu0_kstack;
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
    cpu->ticks = 0;
    cpu->user_ticks = 0;
    cpu->tick_stack = 0;

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
