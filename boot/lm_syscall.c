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

/*
 * Copy a NUL-terminated path out of user memory. Each byte's page is checked
 * before it is read, and the read itself runs under STAC/CLAC because the buffer
 * is user memory and SMAP would otherwise refuse it.
 */
static int copy_user_string(u64 addr, char *dst, u64 max)
{
    u64 i = 0;
    int ok = -1;

    user_access_begin();
    for (; i + 1 < max; i++) {
        if (!vmm_user_mapped(addr + i)) {
            break;
        }
        char c = *(const char *)(unsigned long)(addr + i);
        dst[i] = c;
        if (c == 0) {
            ok = 0;
            break;
        }
    }
    user_access_end();
    dst[max - 1] = 0;
    return ok;
}

static struct lm_fault_frame *sys_open(struct lm_fault_frame *f)
{
    char path[64];
    if (copy_user_string(f->rdi, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = (u64)(long)vfs_open(path);
    return f;
}

static struct lm_fault_frame *sys_close(struct lm_fault_frame *f)
{
    f->rax = (u64)(long)vfs_close((int)f->rdi);
    return f;
}

/* read(fd, buf, count): the bytes come off the disk into a kernel bounce buffer
 * and are then copied out under STAC/CLAC, one validated page range at a time. */
static struct lm_fault_frame *sys_read(struct lm_fault_frame *f)
{
    int fd = (int)f->rdi;
    u64 buf = f->rsi;
    u64 count = f->rdx;
    u8 bounce[256];

    if (count > 4096) {
        count = 4096;
    }

    u64 done = 0;
    while (done < count) {
        u64 want = count - done;
        if (want > sizeof(bounce)) {
            want = sizeof(bounce);
        }
        int got = vfs_read(fd, bounce, want);
        if (got <= 0) {
            break;
        }

        int mapped = 1;
        for (int i = 0; i < got; i++) {
            if (!vmm_user_mapped(buf + done + (u64)i)) {
                mapped = 0;
                break;
            }
        }
        if (!mapped) {
            break;                  /* short read rather than a kernel fault */
        }
        user_access_begin();
        for (int i = 0; i < got; i++) {
            *(u8 *)(unsigned long)(buf + done + (u64)i) = bounce[i];
        }
        user_access_end();
        done += (u64)got;
    }

    f->rax = done;
    return f;
}

/*
 * exec(path): load that file into this task and restart at the entry. The task
 * keeps its address space, its kernel stack and its pid; the code page is
 * replaced with a private frame (the old one is freed), the stack page is
 * zeroed, and the saved frame's RIP/RSP are rewritten so the syscall's IRETQ
 * lands in the new program.
 */
static struct lm_fault_frame *sys_exec(struct lm_fault_frame *f)
{
    char path[64];
    if ((f->cs & 3) != 3 ||
        copy_user_string(f->rdi, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }

    int handle = vfs_open(path);
    if (handle < 0) {
        f->rax = (u64)-1;
        return f;
    }
    u64 size = vfs_size(handle);
    if (size == 0 || size > FRAME_SIZE) {
        vfs_close(handle);
        f->rax = (u64)-1;
        return f;
    }

    u64 frame = pmm_alloc_frame();
    u8 *code = vmm_phys(frame);
    if (code == NULL) {
        if (frame != 0) {
            pmm_free_frame(frame);
        }
        vfs_close(handle);
        f->rax = (u64)-1;
        return f;
    }
    memset(code, 0, FRAME_SIZE);
    if (vfs_read(handle, code, size) != (int)size) {
        pmm_free_frame(frame);
        vfs_close(handle);
        f->rax = (u64)-1;
        return f;
    }
    vfs_close(handle);

    u64 old_code = vmm_translate(USER_CODE_ADDR);
    vmm_unmap(USER_CODE_ADDR);
    if (old_code != 0) {
        pmm_free_frame(old_code & ~(FRAME_SIZE - 1));
    }
    if (vmm_map(USER_CODE_ADDR, frame, VMM_U) != 0) {
        pmm_free_frame(frame);
        f->rax = (u64)-1;
        return f;
    }

    u64 stack = vmm_translate(USER_STACK_ADDR);
    if (stack != 0) {
        u8 *page = vmm_phys(stack & ~(FRAME_SIZE - 1));
        if (page != NULL) {
            memset(page, 0, FRAME_SIZE);
        }
    }

    kputs("[exec] ");
    kputs(path);
    kputs(" bytes=");
    kput_dec(size);
    kputs("\r\n");

    f->rip = USER_CODE_ADDR;
    f->rsp = USER_STACK_TOP;
    f->rax = 0;
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
    [9] = {"open", sys_open, 1},
    [10] = {"read", sys_read, 1},
    [11] = {"close", sys_close, 1},
    [12] = {"exec", sys_exec, 1},
};

#define SYSCALL_COUNT (sizeof(syscalls) / sizeof(syscalls[0]))

/*
 * INT 0x80 from a compatibility-mode task. The CPU pushed the *caller's* CS/SS
 * (0x3B/0x1B), so the IRETQ frame is already right - unlike SYSCALL, where the
 * hardware replaces them with kernel selectors and the dispatcher has to put the
 * user ones back.
 *
 * The 32-bit tree's convention is number in EAX, arguments in EBX/ECX/EDX and
 * the result in EAX, and its numbers are not the LP64 ones, so this translates
 * both before calling the same handlers.
 */
struct lm_fault_frame *lm_int80_dispatch(struct lm_fault_frame *f)
{
    if ((f->cs & 3) != 3) {
        f->rax = (u64)-1;           /* only ring 3 may enter this way */
        return f;
    }

    u32 nr = (u32)f->rax;
    u64 arg0 = (u32)f->rbx;         /* the 32-bit args are the low halves */
    u64 arg1 = (u32)f->rcx;
    u64 arg2 = (u32)f->rdx;

    /* The same accounting the LP64 dispatcher keeps, so a compatibility task is
     * visible in the scheduler's per-task record. */
    cpu_this()->syscall_count++;
    cpu_this()->user_calls++;
    cpu_this()->last_nr = nr;
    cpu_this()->last_arg = arg0;
    sched_note_syscall();

    /* The handlers read the LP64 registers; move the arguments there on the
     * same frame. The result comes back in RAX, which a 32-bit caller reads as
     * EAX. */
    f->rdi = arg0;
    f->rsi = arg1;
    f->rdx = arg2;

    switch (nr) {
    case 0:                         /* SYS_EXIT */
        return sys_exit(f);
    case 5:                         /* SYS_OPEN */
        return sys_open(f);
    case 6:                         /* SYS_READ */
        return sys_read(f);
    case 7:                         /* SYS_WRITE */
        return sys_write(f);
    case 8:                         /* SYS_CLOSE */
        return sys_close(f);
    case 54:                        /* SYS_GETPID */
        f->rax = sched_current_id();
        return f;
    default:
        f->rax = (u64)-1;
        return f;
    }
}

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
