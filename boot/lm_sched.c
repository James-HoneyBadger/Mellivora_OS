/*
 * lm_sched.c - the 64-bit nucleus's task scheduler
 *
 * Roadmap P2. The pieces a task needs are already here: a frame allocator for
 * pages, a VMM for address spaces, a slab for small kernel objects, an IDT that
 * delivers faults and ticks on a per-CPU kernel stack, and a syscall entry that
 * can return to ring 3. This file puts a task object behind them and switches
 * between tasks at two points:
 *
 *   - a timer tick, which arrives from ring 3 with the task's interrupt frame
 *     sitting on that task's kernel stack;
 *   - SYS_EXIT, which retires the running task and returns another one's frame.
 *
 * The switch itself is the same in both cases: save the frame pointer of the
 * running task, load the next task's saved frame pointer into RSP, and let the
 * entry stub pop the registers and IRETQ. A task that has never run has a frame
 * built by task_new(), so "switch to a task" and "start a task" are one path.
 *
 * The policy is round-robin over the runnable tasks - enough to prove the
 * mechanism, with priorities and blocking left for the scheduler sprint that
 * brings task-owned address spaces.
 */

#include "lm.h"

#define MAX_TASKS 8

enum {
    TASK_FREE = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD,
};

struct lm_task {
    u64 id;
    u64 state;
    u64 kstack_base;
    u64 kstack_top;
    u64 rsp;            /* saved frame pointer while the task is not running */
    u64 user_stack;
    u64 calls;
    u64 ticks;
    u64 exit_code;
    u64 exit_whoami;
    u64 exit_ping;
    u64 cr3;            /* the task's address space */
    u64 brk;            /* program break, in the task's own space */
    char args[256];     /* the command line EXEC was given, for GETARGS */
    u64 heap;           /* next byte MALLOC hands out, 0 before the first call */
    u64 free_list;      /* blocks returned by FREE, threaded in user memory */
    u64 mode32;         /* non-zero when the task runs in 32-bit mode */
    u64 expect_exit;    /* the exit code the task must leave, or ANY */
    u64 self_report;    /* ends with PING/WHOAMI in RSI/RDX and its own id */
    void *kstack_obj;
};

static struct lm_task tasks[MAX_TASKS];
static int task_count;
static struct lm_task *current;
static u64 switches;
static int stack_cache = -1;

void sched_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        u8 *p = (u8 *)&tasks[i];
        for (u64 n = 0; n < sizeof(struct lm_task); n++) {
            p[n] = 0;
        }
        tasks[i].state = TASK_FREE;
    }
    task_count = 0;
    current = NULL;
    switches = 0;

    /* Kernel stacks are the slab's first real users: one page each, mapped in
     * the heap window, and they stay allocated for the life of the system. */
    stack_cache = kmem_cache(FRAME_SIZE);
}

struct lm_task *sched_current(void)
{
    return current;
}

static int lock_depth;

void sched_lock(void)
{
    lock_depth++;
}

void sched_unlock(void)
{
    if (lock_depth > 0) {
        lock_depth--;
    }
}

int sched_locked(void)
{
    return lock_depth;
}

u64 sched_current_id(void)
{
    return current != NULL ? current->id : 0;
}

void sched_note_syscall(void)
{
    if (current != NULL) {
        current->calls++;
    }
}

static struct lm_task *pick_next(void)
{
    if (task_count == 0) {
        return NULL;
    }
    int start = current != NULL ? (int)((current->id + 1) % (u64)task_count) : 0;
    for (int n = 0; n < task_count; n++) {
        int i = (start + n) % task_count;
        if (tasks[i].state == TASK_READY) {
            return &tasks[i];
        }
    }
    return NULL;
}

static void become(struct lm_task *next)
{
    struct lm_cpu *cpu = cpu_this();

    current = next;
    current->state = TASK_RUNNING;
    cpu->kstack_top = next->kstack_top;
    cpu->kstack_base = next->kstack_base;
    /* The next fault or tick from this task must land on its stack, and its
     * user pages must be the ones it can see. */
    lm_tss_set_rsp0(next->kstack_top);
    vmm_space_switch(next->cr3);
    switches++;
}

/*
 * Build the frame the entry stubs expect, so a task that has never run is
 * entered by the same pop-and-IRETQ path as one that was preempted.
 */
static struct lm_task *task_new(u64 entry, u64 user_stack, u64 space,
                                int compat, u64 expect_exit, int self_report)
{
    if (space == 0) {
        return NULL;
    }
    if (task_count >= MAX_TASKS || stack_cache < 0) {
        return NULL;
    }

    void *kstack = kmem_alloc(stack_cache);
    if (kstack == NULL) {
        return NULL;
    }
    struct lm_task *t = &tasks[task_count];

    t->id = (u64)task_count;
    t->state = TASK_READY;
    t->kstack_obj = kstack;
    t->kstack_base = (u64)(unsigned long)kstack;
    t->kstack_top = t->kstack_base + FRAME_SIZE;
    t->user_stack = user_stack;
    t->cr3 = space;
    t->brk = USER_BRK_BASE;
    t->args[0] = '\0';
    t->heap = 0;
    t->free_list = 0;
    t->mode32 = compat ? 1 : 0;
    t->expect_exit = expect_exit;
    t->self_report = self_report ? 1 : 0;
    t->calls = 0;
    t->ticks = 0;
    t->exit_code = 0;
    t->exit_whoami = 0;
    t->exit_ping = 0;

    u64 *sp = (u64 *)(unsigned long)t->kstack_top;
    *--sp = SEL_UDATA | RPL_USER;   /* SS: the same data descriptor serves both */
    *--sp = user_stack;             /* RSP */
    *--sp = 0x202;                  /* RFLAGS, IF set: ring 3 is preemptible */
    /* A compatibility task's IRETQ returns to a 32-bit code segment, so the
     * processor uses the low 32 bits of RIP and enters 32-bit mode. */
    *--sp = (compat ? SEL_UCODE32 : SEL_UCODE) | RPL_USER;
    *--sp = entry;                  /* RIP */
    *--sp = 0;                      /* vector - never seen by the dispatcher */
    *--sp = 0;                      /* error code */
    for (int i = 0; i < 15; i++) {
        *--sp = 0;                  /* r15 .. rax */
    }
    t->rsp = (u64)(unsigned long)sp;

    task_count++;
    return t;
}

int sched_create(u64 entry, u64 user_stack, u64 space, int compat,
                 u64 expect_exit, int self_report)
{
    return task_new(entry, user_stack, space, compat, expect_exit,
                    self_report) != NULL ? 0 : -1;
}

void sched_start(void)
{
    struct lm_task *first = pick_next();
    if (first == NULL) {
        kputs("[sched] FAIL no runnable task\r\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    cpu_this()->user_mode = 1;
    become(first);                  /* counts as the first switch */
    switches = 0;                   /* ... but "switches" is about preemption */
    lm_enter_task(first->rsp);
}

/* Round-robin, or stay put when this is the only task. */
struct lm_fault_frame *sched_yield(struct lm_fault_frame *f)
{
    if (current == NULL || (f->cs & 3) != 3) {
        return f;                   /* never switch away from the kernel */
    }

    struct lm_task *next = pick_next();
    if (next == NULL || next == current) {
        return f;
    }

    current->rsp = (u64)(unsigned long)f;
    current->state = TASK_READY;
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}

/* A timer tick is a yield that also charges the quantum. */
struct lm_fault_frame *sched_tick(struct lm_fault_frame *f)
{
    if (current != NULL && (f->cs & 3) == 3) {
        current->ticks++;
    }
    if (lock_depth > 0) {
        return f;               /* inside a critical section: keep running */
    }
    return sched_yield(f);
}

u64 sched_brk(void)
{
    return current != NULL ? current->brk : 0;
}

void sched_set_brk(u64 value)
{
    if (current != NULL) {
        current->brk = value;
    }
}

const char *sched_args(void)
{
    return current != NULL ? current->args : "";
}

void sched_set_args(const char *value)
{
    if (current == NULL) {
        return;
    }
    u32 i = 0;
    for (; value[i] != '\0' && i + 1 < sizeof(current->args); i++) {
        current->args[i] = value[i];
    }
    current->args[i] = '\0';
}

u64 sched_heap(void)
{
    return current != NULL ? current->heap : 0;
}

void sched_set_heap(u64 value)
{
    if (current != NULL) {
        current->heap = value;
    }
}

u64 sched_free_list(void)
{
    return current != NULL ? current->free_list : 0;
}

void sched_set_free_list(u64 value)
{
    if (current != NULL) {
        current->free_list = value;
    }
}

static void sched_report(void)
{
    struct lm_cpu *cpu = cpu_this();
    int ok = 1;

    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state != TASK_DEAD || tasks[i].calls == 0) {
            ok = 0;
        }
    }
    if (switches == 0 || task_count < 2) {
        ok = 0;                     /* nothing was ever preempted */
    }

    /* Every tick must land on the task's own kernel stack, and ticks must reach
     * ring 3 - but not *only* ring 3: a task inside a blocking syscall (the
     * shell's sleep, waiting for a key) is counted in the kernel, so the two
     * counters differ by design. */
    int timer_ok = cpu->ticks > 0 && cpu->user_ticks > 0 &&
                   cpu->tick_stack == cpu->ticks;
    if (!timer_ok) {
        ok = 0;                     /* nothing preempted anything */
    }

    kputs("[sched] tasks=");
    kput_dec((u64)task_count);
    kputs(" switches=");
    kput_dec(switches);
    kputs("\r\n");

    kputs("[timer] hz=");
    kput_dec(LM_TIMER_HZ);
    kputs(" ticks=");
    kput_dec(cpu->ticks);
    kputs(" user=");
    kput_dec(cpu->user_ticks);
    kputs(" kernel=");
    kput_dec(cpu->ticks - cpu->user_ticks);
    kputs(" stack=");
    kputs(cpu->tick_stack == cpu->ticks ? "ok" : "FAIL");
    kputs("\r\n");
    kputs(timer_ok ? "[timer] timer=ok\r\n"
                   : "[timer] FAIL timer never reached ring 3\r\n");

    kputs(ok ? "[sched] sched=ok\r\n" : "[sched] FAIL scheduler round trip\r\n");
    kputs("MELLIVORA64_OK\r\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

/*
 * SYS_EXIT. The caller's frame carries the exit code in RDI, the WHOAMI answer
 * in RSI and the PING result in RDX, so one task's record proves the round trip
 * through its own stack and its own kernel entry. Returns the frame to IRETQ
 * into, or NULL when there is nothing left to run.
 */
struct lm_fault_frame *sched_exit(struct lm_fault_frame *f)
{
    struct lm_task *t = current;
    if (t == NULL) {
        return NULL;
    }

    t->exit_code = f->rdi;
    t->exit_whoami = f->rsi;
    t->exit_ping = f->rdx;

    /* A self-reporting program is the one that exercises brk: it grows the
     * break, writes through the new page and shrinks it back, so the mapping
     * must be gone when it exits. A task that used the heap is allowed to leave
     * its break where it stands - the pages go with the address space. */
    int brk_ok = !t->self_report || vmm_translate(USER_BRK_BASE) == 0;

    int ok;
    /* The tree's programs do not agree on an exit code, so each task says what
     * it expects; TASK_ANY_EXIT means "made calls and stopped". A task that
     * reports itself additionally has to agree with the CPU it ran on. */
    ok = t->calls > 0 &&
         (t->expect_exit == TASK_ANY_EXIT || t->exit_code == t->expect_exit);
    if (t->self_report) {
        ok = ok && brk_ok && t->exit_code == t->id &&
             t->exit_whoami == cpu_this()->id && t->exit_ping == 0x1234;
    }

    kputs("[ring3] task=");
    kput_dec(t->id);
    kputs(" calls=");
    kput_dec(t->calls);
    kputs(" ticks=");
    kput_dec(t->ticks);
    kputs(" exit=0x");
    kput_hex(t->exit_code, 4);
    if (t->self_report) {
        kputs(" ping=0x");
        kput_hex(t->exit_ping, 4);
        kputs(" whoami=");
        kput_dec(t->exit_whoami);
    }
    kputs(" mode=");
    kput_dec(t->mode32 ? 32 : 64);
    if (t->self_report) {
        kputs(" brk=");
        kputs(brk_ok ? "ok" : "FAIL");
    }
    kputs(ok ? " ring3=ok\r\n" : " FAIL ring3\r\n");

    t->state = TASK_DEAD;
    vfs_release_task(t->id);

    struct lm_task *next = pick_next();
    if (next == NULL) {
        sched_report();             /* does not return */
    }
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}
