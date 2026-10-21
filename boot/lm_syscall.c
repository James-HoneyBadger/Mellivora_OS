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

/* The mirror of copy_user_string: validate the pages first, then write under
 * the STAC/CLAC bracket so SMAP does not fault on the store. */
static int copy_to_user(u64 addr, const void *src, u64 count)
{
    const u8 *in = src;
    for (u64 i = 0; i < count; i++) {
        if (!vmm_user_mapped(addr + i)) {
            return -1;
        }
    }
    user_access_begin();
    for (u64 i = 0; i < count; i++) {
        *(u8 *)(unsigned long)(addr + i) = in[i];
    }
    user_access_end();
    return 0;
}

static int copy_from_user(u64 addr, void *dst, u64 count)
{
    u8 *out = dst;
    for (u64 i = 0; i < count; i++) {
        if (!vmm_user_mapped(addr + i)) {
            return -1;
        }
    }
    user_access_begin();
    for (u64 i = 0; i < count; i++) {
        out[i] = *(const u8 *)(unsigned long)(addr + i);
    }
    user_access_end();
    return 0;
}

/* seek(fd, offset, whence) - whence 0 from the start, 1 from the position,
 * 2 from the end. The new position comes back in RAX. */
static struct lm_fault_frame *sys_seek(struct lm_fault_frame *f)
{
    f->rax = (u64)(long)vfs_seek((int)f->rdi, (s64)f->rsi, (int)f->rdx);
    return f;
}

/* stat(path, struct vfs_stat *) - 0 on success, -1 if the path does not
 * resolve. Blocks are reported in the on-disk size, so a program can tell a
 * directory from a file without opening it. */
static struct lm_fault_frame *sys_stat(struct lm_fault_frame *f)
{
    char path[64];
    struct vfs_stat st;

    if (copy_user_string(f->rdi, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    if (vfs_stat(path, &st) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    if (copy_to_user(f->rsi, &st, sizeof(st)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = 0;
    return f;
}

/* readdir(path, index, struct vfs_stat *) - 1 entry, 0 empty slot, -1 done.
 * A NULL path lists the working directory, which is what the 32-bit ABI does. */
static struct lm_fault_frame *sys_readdir(struct lm_fault_frame *f)
{
    char path[64];
    struct vfs_stat st;
    const char *use = NULL;

    if (f->rdi != 0) {
        if (copy_user_string(f->rdi, path, sizeof(path)) != 0) {
            f->rax = (u64)-1;
            return f;
        }
        use = path;
    }

    int got = vfs_readdir(use, (u32)f->rsi, &st);
    if (got < 0) {
        f->rax = (u64)-1;
        return f;
    }
    if (copy_to_user(f->rdx, &st, sizeof(st)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = (u64)got;
    return f;
}

static struct lm_fault_frame *sys_mkdir(struct lm_fault_frame *f)
{
    char path[64];
    if (copy_user_string(f->rdi, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = (u64)(long)vfs_mkdir(path);
    return f;
}

static struct lm_fault_frame *sys_unlink(struct lm_fault_frame *f)
{
    char path[64];
    if (copy_user_string(f->rdi, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = (u64)(long)vfs_unlink(path);
    return f;
}

/* A page of file data: one page is the largest write this call takes, and a
 * page does not fit on a 4 KB kernel stack, so it is static. Creating a file is
 * a multi-step disk update, so the copy happens inside the same critical
 * section that keeps the filesystem to one task at a time. */
static u8 file_bounce[FRAME_SIZE];

/* create(path, buf, len) - write a whole file in one call, creating it or
 * replacing it. The bytes come in through that bounce buffer, so the disk write
 * never touches ring-3 memory. */
static struct lm_fault_frame *sys_create(struct lm_fault_frame *f)
{
    char path[64];
    s64 result = -1;

    if (f->rdx == 0 || f->rdx > sizeof(file_bounce)) {
        f->rax = (u64)-1;
        return f;
    }

    sched_lock();
    if (copy_user_string(f->rdi, path, sizeof(path)) == 0 &&
        copy_from_user(f->rsi, file_bounce, f->rdx) == 0 &&
        vfs_create(path, HBFS_FTYPE_FILE, file_bounce, (u32)f->rdx) == 0) {
        result = (s64)f->rdx;
    }
    sched_unlock();

    f->rax = (u64)result;
    return f;
}

static struct lm_fault_frame *sys_putchar(struct lm_fault_frame *f)
{
    kputc((char)f->rdi);
    f->rax = 1;
    return f;
}

/* getchar() - the next character from the console, or -1 when there is none.
 * Non-blocking on purpose: the caller decides whether to spin, and a spin that
 * yields lets the other tasks run. */
static struct lm_fault_frame *sys_getchar(struct lm_fault_frame *f)
{
    int c = kgetc();
    f->rax = (u64)(long)c;
    return f;
}

/* clear() - the ANSI sequence a terminal understands, since the console is the
 * UART and there is no text buffer to clear. */
static struct lm_fault_frame *sys_clear(struct lm_fault_frame *f)
{
    kputs("\x1b[2J\x1b[H");
    f->rax = 0;
    return f;
}

/* sleep(milliseconds) - counted in timer ticks. HWLT parks the CPU until the PIT
 * interrupts, and the tick's own path may switch tasks from here, so this is a
 * real sleep rather than a spin: the return frame is whatever the tick left to
 * run, and this task resumes at the hlt below when it is scheduled again. */
static struct lm_fault_frame *sys_sleep(struct lm_fault_frame *f)
{
    u64 want = f->rdi / 10;         /* the PIT runs at 100 Hz */
    u64 until = cpu_this()->ticks + want;

    while (cpu_this()->ticks < until) {
        __asm__ volatile("sti; hlt");
    }
    f->rax = 0;
    return f;
}

/*
 * date(buf): six bytes - second, minute, hour, day, month, two-digit year - and
 * the four-digit year in RAX, which is the 32-bit tree's shape for SYS_DATE.
 */
static struct lm_fault_frame *sys_date(struct lm_fault_frame *f)
{
    struct rtc_time t;
    u8 out[6];

    if (rtc_read(&t) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    out[0] = t.second;
    out[1] = t.minute;
    out[2] = t.hour;
    out[3] = t.day;
    out[4] = t.month;
    out[5] = t.year;
    if (copy_to_user(f->rdi, out, sizeof(out)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = t.full_year;
    return f;
}

static struct lm_fault_frame *sys_chdir(struct lm_fault_frame *f)
{
    char path[64];
    if (copy_user_string(f->rdi, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = (u64)(long)vfs_chdir(path);
    return f;
}

/* getcwd(buf, size) - copies the path with its NUL, returns its length. */
static struct lm_fault_frame *sys_getcwd(struct lm_fault_frame *f)
{
    const char *cwd = vfs_getcwd();
    u64 len = 0;

    while (cwd[len] != '\0') {
        len++;
    }
    if (f->rsi < len + 1) {
        f->rax = (u64)-1;
        return f;
    }
    if (copy_to_user(f->rdi, cwd, len + 1) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = len;
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
    [13] = {"seek", sys_seek, 0},
    [14] = {"stat", sys_stat, 0},
    [15] = {"readdir", sys_readdir, 0},
    [16] = {"chdir", sys_chdir, 1},
    [17] = {"getcwd", sys_getcwd, 0},
    [18] = {"mkdir", sys_mkdir, 1},
    [19] = {"unlink", sys_unlink, 1},
    [20] = {"create", sys_create, 1},
    [21] = {"putchar", sys_putchar, 0},
    [22] = {"getchar", sys_getchar, 0},
    [23] = {"clear", sys_clear, 0},
    [24] = {"sleep", sys_sleep, 1},
    [25] = {"date", sys_date, 0},
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
/* SYS_PRINT (the 32-bit tree's number 3): a NUL-terminated string in EBX. */
static struct lm_fault_frame *int80_print(struct lm_fault_frame *f)
{
    u64 addr = (u32)f->rbx;
    u64 n = 0;

    user_access_begin();
    for (; n < 4096; n++) {
        if (!vmm_user_mapped(addr + n)) {
            break;
        }
        char c = *(const char *)(unsigned long)(addr + n);
        if (c == 0) {
            break;
        }
        kputc(c);
    }
    user_access_end();

    f->rax = n;
    return f;
}

/*
 * The 32-bit stat buffer is 12 bytes - size, type, mtime - and a NULL buffer is
 * the legacy form that returns the size in EAX and the block count in ECX.
 */
static struct lm_fault_frame *int80_stat(struct lm_fault_frame *f)
{
    char path[64];
    struct vfs_stat st;
    u32 buf[3];

    if (copy_user_string((u32)f->rbx, path, sizeof(path)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    if (vfs_stat(path, &st) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    if ((u32)f->rcx == 0) {
        f->rax = st.size;
        f->rcx = st.blocks;
        return f;
    }

    buf[0] = st.size;
    buf[1] = st.type;
    buf[2] = 0;                     /* mtime: no clock in the 64-bit port yet */
    if (copy_to_user((u32)f->rcx, buf, sizeof(buf)) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = 0;
    return f;
}

/* READDIR over the working directory: the name goes to EBX, the type to EAX
 * (0 = free slot, -1 = past the end) and the size to ECX. */
static struct lm_fault_frame *int80_readdir(struct lm_fault_frame *f)
{
    struct vfs_stat st;
    u64 buf = (u32)f->rbx;

    int got = vfs_readdir(NULL, (u32)f->rcx, &st);
    if (got < 0) {
        if (vmm_user_mapped(buf)) {
            user_access_begin();
            *(char *)(unsigned long)buf = 0;
            user_access_end();
        }
        f->rax = (u64)-1;
        f->rcx = 0;
        return f;
    }
    if (copy_to_user(buf, st.name, sizeof(st.name)) != 0) {
        f->rax = (u64)-1;
        f->rcx = 0;
        return f;
    }
    f->rax = got == 1 ? st.type : 0;
    f->rcx = got == 1 ? st.size : 0;
    return f;
}

/* GETCWD copies the path and its NUL to EBX; EBX is unvalidated, so the copy
 * goes through copy_to_user rather than the caller's pointer directly. */
static struct lm_fault_frame *int80_getcwd(struct lm_fault_frame *f)
{
    const char *cwd = vfs_getcwd();
    u64 len = 0;
    while (cwd[len] != '\0') {
        len++;
    }
    if (copy_to_user((u32)f->rbx, cwd, len + 1) != 0) {
        f->rax = (u64)-1;
        return f;
    }
    f->rax = len;
    return f;
}

static struct lm_fault_frame *int80_call(struct lm_fault_frame *f, u32 nr);

/*
 * The handlers read their arguments from the LP64 registers, and those same
 * frame slots are what IRETQ loads back into the caller's registers. Writing the
 * arguments there is convenient but would hand a 32-bit program back its own
 * arguments in EDI/ESI/EDX - a program that keeps a handle in ESI across a call
 * would find the last syscall's count there instead. So the three slots are
 * saved and put back unless the handler returned another task's frame.
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
    u64 save_rdi = f->rdi;
    u64 save_rsi = f->rsi;
    u64 save_rdx = f->rdx;

    f->rdi = arg0;
    f->rsi = arg1;
    f->rdx = arg2;

    struct lm_fault_frame *out = int80_call(f, nr);
    if (out == f) {
        f->rdi = save_rdi;
        f->rsi = save_rsi;
        f->rdx = save_rdx;
    }
    return out;
}

static struct lm_fault_frame *int80_call(struct lm_fault_frame *f, u32 nr)
{
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
    case 3:                         /* SYS_PRINT */
        return int80_print(f);
    case 18:                        /* SYS_SETCOLOR: no text console yet */
        f->rax = 0;
        return f;
    case 1:                         /* SYS_PUTCHAR: EBX=char */
        return sys_putchar(f);
    case 2:                         /* SYS_GETCHAR -> EAX=char or -1 */
    case 4:                         /* SYS_READ_KEY: the same thing here */
    case 33:                        /* SYS_SERIAL_IN */
        return sys_getchar(f);
    case 17:                        /* SYS_CLEAR */
        return sys_clear(f);
    case 16:                        /* SYS_SLEEP: EBX=milliseconds */
        return sys_sleep(f);
    case 25:                        /* SYS_DATE: EBX=6-byte buffer */
        f->rdi = (u64)(u32)f->rbx;
        return sys_date(f);
    case 15:                        /* SYS_GETTIME: ticks since boot */
        return sys_ticks(f);
    case 10:                        /* SYS_SEEK: EBX=fd ECX=offset EDX=whence */
        f->rdi = (u64)(u32)f->rbx;
        f->rsi = (u64)(s64)(int)(u32)f->rcx;
        f->rdx = (u64)(u32)f->rdx;
        return sys_seek(f);
    case 11:                        /* SYS_STAT: EBX=name ECX=12-byte buffer */
        return int80_stat(f);
    case 13:                        /* SYS_READDIR: EBX=buf ECX=index */
        return int80_readdir(f);
    case 26:                        /* SYS_CHDIR: EBX=path */
        return sys_chdir(f);
    case 27:                        /* SYS_GETCWD: EBX=buf -> EAX=length */
        return int80_getcwd(f);
    case 12:                        /* SYS_MKDIR: EBX=path */
        return sys_mkdir(f);
    case 9:                         /* SYS_DELETE: EBX=path */
        return sys_unlink(f);
    case 31:                        /* SYS_FWRITE: EBX=name ECX=buf EDX=size
                                     * ESI=type (0 = text) */
        if ((u32)f->rsi != 0) {
            f->rdx = (u64)(u32)f->rdx;
            f->rsi = (u64)(u32)f->rcx;
            f->rdi = (u64)(u32)f->rbx;
            return sys_create(f);
        }
        f->rax = (u64)-1;
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
