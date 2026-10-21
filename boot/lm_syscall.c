/*
 * lm_syscall.c - the 64-bit syscall table
 *
 * The 32-bit tree dispatches 187 calls from an int 0x80 handler with the
 * registers of that ABI. This is the LP64 half it will be bridged to: number in
 * RAX, arguments in RDI, RSI, RDX, R10, R8, R9, result in RAX - and a table
 * rather than a switch, so the ported calls are entries and the dispatcher does
 * not grow with them.
 *
 * Five calls are the demo set the boot test drives (PING, WHOAMI, TICKS,
 * GETTID, EXIT); the three that make this a real ABI rather than a stub are
 * WRITE, YIELD and BRK:
 *
 *   - WRITE reads a user buffer, which is exactly what SMAP forbids without
 *     STAC/CLAC. The bracket here is the one copy_from_user will use.
 *   - YIELD goes through the same switch as a timer tick, so a task can give
 *     the CPU up voluntarily.
 *   - BRK grows and shrinks a per-task mapping through the VMM; shrinking frees
 *     the frame, and the scheduler checks the mapping is gone when the task
 *     exits.
 */

#include "lm.h"

/* The table is indexed by number; a hole is an unimplemented call. */
struct lm_syscall {
    const char *name;
    struct lm_fault_frame *(*fn)(struct lm_fault_frame *);
    int from_user;      /* ring 0 may not end or yield the kernel context */
};

static struct lm_fault_frame *sys_ping(struct lm_fault_frame *f)
{
    f->rax = f->rdi;
    return f;
}

static struct lm_fault_frame *sys_whoami(struct lm_fault_frame *f)
{
    f->rax = cpu_this()->id;
    return f;
}

static struct lm_fault_frame *sys_ticks(struct lm_fault_frame *f)
{
    f->rax = cpu_this()->ticks;
    return f;
}

static struct lm_fault_frame *sys_gettid(struct lm_fault_frame *f)
{
    f->rax = cpu_this()->user_mode ? sched_current_id() : 0;
    return f;
}

static struct lm_fault_frame *sys_exit(struct lm_fault_frame *f)
{
    if ((f->cs & 3) != 3) {
        f->rax = (u64)-1;
        return f;
    }
    return sched_exit(f);           /* another task's frame, or NULL */
}

static void user_access_begin(void)
{
    if (cpu_smap_enabled()) {
        __asm__ volatile("stac" : : : "memory");
    }
}

static void user_access_end(void)
{
    if (cpu_smap_enabled()) {
        __asm__ volatile("clac" : : : "memory");
    }
}

/*
 * write(fd, buf, count) to the console. Arguments are checked before anything is
 * touched: only fd 1 and 2, a bounded count, and every byte must be in a page
 * the VMM says is user-mapped (that check walks page tables, so it needs no
 * STAC). A short count ends the write rather than faulting the kernel.
 */
static struct lm_fault_frame *sys_write(struct lm_fault_frame *f)
{
    u64 fd = f->rdi;
    u64 buf = f->rsi;
    u64 count = f->rdx;

    if ((fd != 1 && fd != 2) || count > 4096) {
        f->rax = (u64)-1;
        return f;
    }

    u64 done = 0;
    user_access_begin();
    for (; done < count; done++) {
        if (!vmm_user_mapped(buf + done)) {
            break;
        }
        kputc(*(const char *)(unsigned long)(buf + done));
    }
    user_access_end();

    f->rax = done;
    return f;
}

static struct lm_fault_frame *sys_yield(struct lm_fault_frame *f)
{
    if ((f->cs & 3) != 3) {
        f->rax = (u64)-1;
        return f;
    }
    return sched_yield(f);
}

/*
 * brk(0) reports the break; brk(addr) moves it, rounded up to a page. Growing
 * maps fresh zeroed frames, shrinking unmaps and frees them. Anything below the
 * base or beyond the window is refused, and the old break comes back.
 */
static struct lm_fault_frame *sys_brk(struct lm_fault_frame *f)
{
    u64 cur = sched_brk();
    u64 want = f->rdi;

    if (want == 0) {
        f->rax = cur;
        return f;
    }
    want = (want + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    if (want < USER_BRK_BASE || want > USER_BRK_BASE + 16 * FRAME_SIZE) {
        f->rax = cur;
        return f;
    }

    if (want > cur) {
        for (u64 addr = cur; addr < want; addr += FRAME_SIZE) {
            u64 frame = pmm_alloc_frame();
            u8 *page = vmm_phys(frame);
            if (page == NULL) {
                if (frame != 0) {
                    pmm_free_frame(frame);
                }
                f->rax = cur;
                return f;
            }
            memset(page, 0, FRAME_SIZE);
            if (vmm_map(addr, frame, VMM_W | VMM_U | VMM_NX) != 0) {
                pmm_free_frame(frame);
                f->rax = cur;
                return f;
            }
        }
    } else {
        for (u64 addr = want; addr < cur; addr += FRAME_SIZE) {
            u64 frame = vmm_translate(addr);
            vmm_unmap(addr);
            if (frame != 0) {
                pmm_free_frame(frame & ~(FRAME_SIZE - 1));
            }
        }
    }

    sched_set_brk(want);
    f->rax = want;
    return f;
}

static const struct lm_syscall syscalls[] = {
    [1] = {"ping", sys_ping, 0},
    [2] = {"whoami", sys_whoami, 0},
    [3] = {"exit", sys_exit, 1},
    [4] = {"ticks", sys_ticks, 0},
    [5] = {"gettid", sys_gettid, 0},
    [6] = {"write", sys_write, 1},
    [7] = {"yield", sys_yield, 1},
    [8] = {"brk", sys_brk, 1},
};

#define SYSCALL_COUNT (sizeof(syscalls) / sizeof(syscalls[0]))

void syscall_init(void)
{
    kputs("[sys] abi=lp64 table=");
    kput_dec(SYSCALL_COUNT);
    kputs("\r\n");
}

/*
 * The C half of a SYSCALL. Everything the ported calls will need to see is in
 * the frame: the number in `rax`, the arguments in the ABI registers, and the
 * caller's ring in `cs`. Returns the frame to IRETQ into - the caller's, or
 * another task's when the call switched - or NULL when there is nothing left.
 */
struct lm_fault_frame *lm_syscall_dispatch(struct lm_fault_frame *f)
{
    struct lm_cpu *cpu = cpu_this();
    u64 rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    cpu->syscall_count++;
    sched_note_syscall();
    cpu->last_rsp = rsp;         /* must be inside the per-CPU kernel stack */
    cpu->last_nr = f->rax;
    cpu->last_arg = f->rdi;

    int from_user = cpu->user_mode != 0;
    if (from_user) {
        cpu->user_calls++;
        cpu->last_user_rip = f->rip;
        cpu->last_user_rsp = f->rsp;
    }

    /* Whatever ring the caller was in, SYSCALL has already put kernel
     * selectors in CS/SS; the frame is what IRETQ returns with, so it has to
     * carry the caller's ring back. */
    f->cs = from_user ? (SEL_UCODE | RPL_USER) : SEL_KCODE;
    f->ss = from_user ? (SEL_UDATA | RPL_USER) : SEL_KDATA;

    u64 nr = f->rax;
    if (nr == 0 || nr >= SYSCALL_COUNT || syscalls[nr].fn == NULL ||
        (syscalls[nr].from_user && !from_user)) {
        f->rax = (u64)-1;
        return f;
    }

    return syscalls[nr].fn(f);
}
