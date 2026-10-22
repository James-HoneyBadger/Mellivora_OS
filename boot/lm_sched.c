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

#define MAX_TASKS 16

enum {
    TASK_FREE = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,       /* waiting on a futex word, and not to be picked */
    TASK_DEAD,
};

/* Who is waiting on which word. A table rather than a list per word: sixteen
 * tasks can wait on at most sixteen addresses, and a scan of sixteen entries is
 * cheaper than the bookkeeping it would save. */
#define FUTEX_SLOTS MAX_TASKS

struct futex_waiter {
    u64 address;        /* 0 when the slot is free */
    u64 task;           /* the task id waiting on it */
};

static struct futex_waiter futex_waiters[FUTEX_SLOTS];

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
    u64 parent;         /* the task that forked this one, or ~0 for a boot task */
    u64 heap;           /* next byte MALLOC hands out, 0 before the first call */
    u64 fs_base;        /* thread-local storage: loaded into IA32_FS_BASE */
    u64 free_list;      /* blocks returned by FREE, threaded in user memory */
    u64 mode32;         /* non-zero when the task runs in 32-bit mode */
    u64 expect_exit;    /* the exit code the task must leave, or ANY */
    u64 self_report;    /* ends with PING/WHOAMI in RSI/RDX and its own id */
    u64 waiting_for;    /* the child this task is parked waiting for, or 0 */
    void *kstack_obj;
};

/* The stack lm_start switched to before any C ran, defined in lm_nucleus.c. It
 * is where the idle loop runs. */
extern u8 lm_stack[];
extern u8 lm_stack_top[];

struct lm_fault_frame *sched_idle_wait(void);
void lm_sched_idle(void) __attribute__((noreturn));
void lm_frame_resume(struct lm_fault_frame *frame) __attribute__((noreturn));

__asm__(
    ".text\n"
    ".globl lm_sched_idle\n"
    "lm_sched_idle:\n"
    "    leaq lm_stack_top(%rip), %rsp\n"
    "1:\n"
    "    call sched_idle_wait\n"
    "    testq %rax, %rax\n"
    "    jz 1b\n"
    "    jmp lm_frame_resume\n"
    ".text\n");

static struct lm_task tasks[MAX_TASKS];
static int task_count;
static struct lm_task *current;
static int need_resched;            /* a wake-up asked for a reschedule */
static u64 switches;
static u64 wait_blocks;             /* parents that parked for a child */
static u64 wait_wakes;              /* ... and were woken because it stopped */

static int stack_cache = -1;

/*
 * The critical section that keeps the filesystem - and everything else that owns
 * a static - to one task at a time.
 *
 * Two things have to be true, and with more than one CPU they are different
 * facts. The spinlock keeps another processor out, and it is taken only by the
 * outermost acquisition, because the lock nests and a word cannot count. Turning
 * interrupts off keeps the timer out, and it is what makes a tick unable to
 * observe a half-written directory; the flag that was there before the outermost
 * acquisition is the one that gets put back, so a section entered from ring 3
 * (where interrupts are on) leaves them on and one entered from a syscall (where
 * they are already off) leaves them off.
 */
static lm_spinlock lock_guard;
static int lock_depth;
static u64 lock_flags;

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
    spin_init_named(&lock_guard, "sched");
}

struct lm_task *sched_current(void)
{
    return current;
}

void sched_lock(void)
{
    u64 flags = irq_save();
    if (lock_depth == 0) {
        lock_flags = flags;
        spin_lock(&lock_guard);
    }
    lock_depth++;
}

void sched_unlock(void)
{
    if (lock_depth == 0) {
        return;
    }
    lock_depth--;
    if (lock_depth == 0) {
        spin_unlock(&lock_guard);
        irq_restore(lock_flags);
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
    /* Thread-local storage is a task's, not a CPU's, so it is part of the
     * switch. A thread that set its own base must not see its creator's, and the
     * creator must get its own back. */
    cpu_write_msr(MSR_FS_BASE, next->fs_base);
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
    t->parent = TASK_NO_PARENT;
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
/*
 * Preemption works from ring 0 as well as ring 3. It has to: a task inside a
 * blocking syscall - the shell waiting for a child, a program waiting for a key -
 * is in the kernel, and a tick that refused to switch there would starve every
 * other task for the whole wait. The frame is a ring-0 one (three trailing words
 * instead of five), which both entry stubs' epilogues handle unchanged.
 *
 * The one place a switch must not happen is a critical section, and that is what
 * the lock is for: a filesystem update keeps a task on the CPU until it is done.
 */
/*
 * May this frame be switched away from? Ring 3 always; a frame in the kernel,
 * never - and the reason is the GS base rather than the frame itself.
 *
 * A frame says what ring it returns to, so both stubs can swap the GS base only
 * when they are going back to ring 3 (which they now do). What is *not* settled is
 * when a task's own kernel side may be suspended: a syscall's C stack cannot be
 * resumed by anyone, and an interrupt frame inside the kernel can - but then the
 * task is resumed by whatever path is running, which may be another task's syscall
 * epilogue, and the two have to agree about the GS base and about who else might
 * have been mid-update. That agreement is the work of making the kernel
 * preemptible, and the experiment that tried it here (a thread holding a note in a
 * syscall while another thread ran) showed it is not a one-line change: with the
 * epilogue made ring-aware the machine booted, took eight ring-0 switches, and the
 * second thread still observed only one slice in four hundred milliseconds.
 */
static int may_preempt(const struct lm_fault_frame *f)
{
    return (f->cs & 3) == 3;
}

/*
 * A futex wait that actually waits. The task is marked BLOCKED, recorded against
 * the word and switched away from - and because the frame being switched away
 * from is the *caller's* ring-3 frame, the switch is allowed (the ring-0 refusal
 * only applies to a frame that has no RSP/SS). When the task is picked again it
 * resumes by returning from the syscall it is inside, with the answer already in
 * RAX, which is exactly what a waiter wants to see.
 *
 * If nothing else can run the task keeps the CPU: a machine with one runnable
 * task and a waiter is a machine with nothing to wait for.
 */
struct lm_fault_frame *sched_block(struct lm_fault_frame *f, u64 address)
{
    if (current == NULL || sched_locked() || !may_preempt(f)) {
        f->rax = (u64)-1;
        return f;
    }

    int slot = -1;
    for (int i = 0; i < FUTEX_SLOTS; i++) {
        if (futex_waiters[i].address == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        f->rax = (u64)-1;               /* too many waiters */
        return f;
    }

    futex_waiters[slot].address = address;
    futex_waiters[slot].task = current->id;

    struct lm_task *next = pick_next();
    if (next == NULL) {
        futex_waiters[slot].address = 0;
        f->rax = (u64)-1;               /* nobody else could run: do not park */
        return f;
    }

    f->rax = 0;                         /* woken: the answer is "you are awake" */
    current->rsp = (u64)(unsigned long)f;
    current->state = TASK_BLOCKED;
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}

/* Wake up to `count` of them. Returns how many were woken. */
int sched_wake(u64 address, int count)
{
    int woken = 0;

    for (int i = 0; i < FUTEX_SLOTS && woken < count; i++) {
        if (futex_waiters[i].address != address) {
            continue;
        }
        u64 id = futex_waiters[i].task;
        futex_waiters[i].address = 0;
        if (id < (u64)task_count && tasks[id].state == TASK_BLOCKED) {
            tasks[id].state = TASK_READY;
            need_resched = 1;
            woken++;
        }
    }
    return woken;
}

/*
 * Park the running task until somebody else makes it runnable. This is the piece
 * a sleep needs and a futex does not: there is no word to wait on, only a promise
 * that a timer will call sched_wake_task() later. The frame saved is the caller's
 * ring-3 one, so the task resumes by returning from the syscall it is inside.
 *
 * If nothing else can run the task keeps the CPU: parking the only runnable task
 * is a machine that stops. The caller is told that by getting its own frame back
 * and is expected to do the waiting itself.
 */
struct lm_fault_frame *sched_park(struct lm_fault_frame *f)
{
    if (current == NULL || sched_locked() || !may_preempt(f)) {
        return f;
    }

    struct lm_task *next = pick_next();
    if (next == NULL) {
        return f;
    }

    current->rsp = (u64)(unsigned long)f;
    current->state = TASK_BLOCKED;
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}

/* A wake-up from a timer interrupt: the task was parked, and is now ready to be
 * picked. The lock is taken here because the caller is an interrupt handler and
 * has no idea what the interrupted code was holding. */
int sched_wake_task(u64 id)
{
    if (id == 0 || id >= (u64)task_count) {
        return 0;
    }

    sched_lock();
    int woken = 0;
    if (tasks[id].state == TASK_BLOCKED) {
        tasks[id].state = TASK_READY;
        woken = 1;
        need_resched = 1;
    }
    sched_unlock();
    return woken;
}

/*
 * A wake-up happened during an interrupt: the running task should be asked to
 * step aside rather than being allowed to finish its quantum. Without this a
 * sleeper that is woken one millisecond into its wait would not run until the
 * next tick - the accuracy of the timer would be wasted on a scheduler that
 * still thought in ten-millisecond steps, and a task that asked to sleep for a
 * millisecond would observe fifteen.
 */
int sched_need_resched(void)
{
    int pending = need_resched;
    need_resched = 0;
    return pending;
}

/*
 * The same, parked for a particular child. A parent waiting for a child is a wait
 * with an event behind it, so it does not need to be asked again every few
 * milliseconds: it is parked, and the child's exit wakes it. The id is recorded
 * on the task so that an exit wakes the parent that is waiting for *it* and not
 * one parked for a futex or a sleep.
 */
struct lm_fault_frame *sched_park_for(struct lm_fault_frame *f, u64 child)
{
    if (current == NULL || sched_locked() || !may_preempt(f)) {
        return f;
    }

    struct lm_task *next = pick_next();
    if (next == NULL) {
        return f;               /* nothing else can run: do not park */
    }

    current->waiting_for = child;
    current->rsp = (u64)(unsigned long)f;
    current->state = TASK_BLOCKED;
    wait_blocks++;
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}

/* A child stopped. If its parent is parked waiting for exactly this one, that is
 * the event it is asleep for; a parent parked for another reason is left alone. */
static void wake_waiting_parent(u64 child_id)
{
    if (child_id >= (u64)task_count) {
        return;
    }
    struct lm_task *child = &tasks[child_id];
    if (child->parent == TASK_NO_PARENT || child->parent >= (u64)task_count) {
        return;
    }

    struct lm_task *parent = &tasks[child->parent];
    if (parent->state == TASK_BLOCKED && parent->waiting_for == child_id) {
        parent->waiting_for = 0;
        parent->state = TASK_READY;
        wait_wakes++;
    }
}

struct lm_fault_frame *sched_yield(struct lm_fault_frame *f)
{
    /* Switching away from ring 0 is not allowed: the frame there has no RSP/SS
     * and the kernel is not reentrant. So a syscall that waits has to return to
     * its caller and be asked again - which is why getchar and waitpid answer
     * "nothing yet" instead of blocking. */
    if (current == NULL || sched_locked() || !may_preempt(f)) {
        return f;
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
    /* The pointer is drained here whether or not a switch follows: a hundred
     * polls a second is what keeps it in step with a hand. */
    mouse_poll();

    if (current != NULL && (f->cs & 3) == 3) {
        current->ticks++;
    }
    if (lock_depth > 0) {
        return f;               /* inside a critical section: keep running */
    }
    return sched_yield(f);
}

/*
 * The heap belongs to the address space, not to the task: a thread shares its
 * creator's memory, so it has to share the bump pointer and the free list too.
 * Two of each would hand out the same block twice, which looks like a correct
 * program corrupting itself.
 *
 * The owner is the lowest-numbered live task in the space. That choice is
 * arbitrary but stable for as long as the space exists - it does not move when a
 * thread exits, which is what matters: a heap that changed owner would change
 * its idea of where the free memory starts.
 */
static struct lm_task *heap_owner(void)
{
    if (current == NULL) {
        return NULL;
    }
    struct lm_task *owner = current;
    for (int i = 0; i < task_count; i++) {
        struct lm_task *t = &tasks[i];
        if (t->cr3 != current->cr3 || t->state == TASK_FREE ||
            t->state == TASK_DEAD) {
            continue;
        }
        if (t->id < owner->id) {
            owner = t;
        }
    }
    return owner;
}

u64 sched_brk(void)
{
    struct lm_task *owner = heap_owner();
    return owner != NULL ? owner->brk : 0;
}

void sched_set_brk(u64 value)
{
    struct lm_task *owner = heap_owner();
    if (owner != NULL) {
        owner->brk = value;
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

/*
 * The thread-local base. User code reads it through FS, and the ABI is the one
 * `arch_prctl(ARCH_SET_FS)` established: whatever this task last asked for, or 0.
 */
int sched_set_tls(u64 base)
{
    if (current == NULL) {
        return -1;
    }
    current->fs_base = base;
    cpu_write_msr(MSR_FS_BASE, base);
    return 0;
}

u64 sched_tls(void)
{
    return current != NULL ? current->fs_base : 0;
}

u64 sched_heap(void)
{
    struct lm_task *owner = heap_owner();
    return owner != NULL ? owner->heap : 0;
}

void sched_set_heap(u64 value)
{
    struct lm_task *owner = heap_owner();
    if (owner != NULL) {
        owner->heap = value;
    }
}

u64 sched_free_list(void)
{
    struct lm_task *owner = heap_owner();
    return owner != NULL ? owner->free_list : 0;
}

void sched_set_free_list(u64 value)
{
    struct lm_task *owner = heap_owner();
    if (owner != NULL) {
        owner->free_list = value;
    }
}

/*
 * What runs when every task is waiting for something. Parking a sleeper and a
 * waiter at the same time is not a deadlock - the timer will wake one and the
 * child will wake the other - but it does mean the CPU has nothing to do until an
 * interrupt arrives, and a machine that halted there would have to be told the
 * difference between "waiting" and "finished".
 *
 * The idle loop runs on the boot stack and not on the dead task's: that task's
 * slot can be reaped while the loop is still running, which frees its kernel
 * stack from underneath. The boot stack belongs to no task and nothing can free
 * it, and the per-CPU block is pointed at it for the same reason - a tick that
 * arrives while idling is on the kernel stack like any other.
 */
static void sched_report(void);

static int live_tasks(void)
{
    int live = 0;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING ||
            tasks[i].state == TASK_BLOCKED) {
            live++;
        }
    }
    return live;
}

/* Called from the idle loop, on the boot stack, with the frame it should enter. */
struct lm_fault_frame *sched_idle_wait(void)
{
    struct lm_cpu *cpu = cpu_this();

    if (live_tasks() == 0) {
        sched_report();             /* every task is gone: say so and stop */
    }

    cpu->kstack_base = (u64)(unsigned long)lm_stack;
    cpu->kstack_top = (u64)(unsigned long)lm_stack_top;

    __asm__ volatile("sti; hlt" ::: "memory");

    struct lm_task *next = pick_next();
    if (next == NULL) {
        return NULL;
    }
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}

static void sched_report(void)
{
    struct lm_cpu *cpu = cpu_this();
    int ok = 1;

    for (int i = 0; i < task_count; i++) {
        /* A reaped slot (FREE) is as finished as a dead one. Anything else being
         * READY or RUNNING here would mean a task was left behind. */
        if (tasks[i].state == TASK_DEAD || tasks[i].state == TASK_FREE) {
            continue;
        }
        ok = 0;
    }
    if (switches == 0 || task_count < 2) {
        ok = 0;                     /* nothing was ever preempted */
    }

    /* Every tick must land on the task's own kernel stack, must reach ring 3 -
     * but not *only* ring 3: a task inside a blocking syscall (the shell's
     * sleep, waiting for a key) is counted in the kernel, so the two counters
     * differ by design - and must come from the local APIC. A single tick from
     * the 8259 would mean the old clock is still running alongside the new one,
     * which doubles the rate and makes every sleep and timeout wrong. */
    int from_apic = cpu->ticks > 0 && cpu->apic_ticks == cpu->ticks;
    int timer_ok = from_apic && cpu->ticks > 0 && cpu->user_ticks > 0 &&
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
    kputs(" source=");
    kputs(from_apic ? "lapic" : "pit+mixed");
    kputs("\r\n");

    kputs(timer_ok ? "[timer] timer=ok\r\n"
                   : "[timer] FAIL timer never reached ring 3\r\n");

    /* Not a scheduler fact, but this is the one place that prints after every
     * task: the input driver's interrupt count proves the I/O APIC's routing
     * works, which polling would never show. */
    lockdep_report();

    /* Every parent that parked for a child had to be woken by it, so the two
     * counts agree or a wait was lost - which is the one failure mode of a
     * blocking wait and the reason both are counted. */
    kputs("[wait] blocks=");
    kput_dec(wait_blocks);
    kputs(" wakes=");
    kput_dec(wait_wakes);
    kputs(wait_blocks == wait_wakes ? " wait=ok\r\n" : " wait=FAIL\r\n");


    kputs("[ps2] irqs=");
    kput_dec(kbd_irqs());
    kputs("\r\n");

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
/*
 * Fork. The child gets a copy of the caller's address space and a copy of the
 * frame the syscall was entered on, so it resumes at the same instruction with
 * the same registers - except RAX, which is the child's id 0. Both tasks return
 * from the same syscall into the same code with their own memory.
 */
s64 sched_fork(struct lm_fault_frame *parent_frame)
{
    if (current == NULL || stack_cache < 0 || parent_frame == NULL) {
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state == TASK_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (task_count >= MAX_TASKS) {
            return -1;
        }
        slot = task_count++;
    }

    u64 space = vmm_space_copy(current->cr3);
    if (space == 0) {
        return -1;
    }
    void *kstack = kmem_alloc(stack_cache);
    if (kstack == NULL) {
        vmm_space_destroy(space);
        return -1;
    }

    struct lm_task *child = &tasks[slot];
    struct lm_task *parent = current;

    *child = *parent;
    child->id = (u64)slot;
    /* READY is the last thing set: a tick that preempts this syscall (kernel
     * preemption is on) must not pick a child whose frame pointer is still the
     * parent's. */
    child->state = TASK_FREE;
    child->kstack_obj = kstack;
    child->kstack_base = (u64)(unsigned long)kstack;
    child->kstack_top = child->kstack_base + FRAME_SIZE;
    child->cr3 = space;
    child->parent = parent->id;
    /* A child's exit code belongs to its parent's waitpid, not to the boot
     * report: the record for a boot task is the demo's, and this is not one. */
    child->expect_exit = TASK_ANY_EXIT;
    child->self_report = 0;
    child->calls = 0;
    child->ticks = 0;
    child->exit_code = 0;
    child->exit_whoami = 0;
    child->exit_ping = 0;

    struct lm_fault_frame *copy =
        (struct lm_fault_frame *)(unsigned long)(child->kstack_top -
                                                 sizeof(struct lm_fault_frame));
    *copy = *parent_frame;
    copy->rax = 0;                  /* fork() returns 0 in the child */
    child->rsp = (u64)(unsigned long)copy;

    child->state = TASK_READY;      /* now it can be picked */
    return (s64)slot;
}

/*
 * How many tasks are living in one address space. A thread shares its creator's,
 * so the space belongs to whichever of them is still there and is only released
 * when the last one has been reaped - a task that has exited but not yet been
 * collected still owns it.
 */
static int space_users(u64 space)
{
    int users = 0;
    for (int i = 0; i < task_count; i++) {
        u64 state = tasks[i].state;
        if (tasks[i].cr3 == space &&
            (state == TASK_READY || state == TASK_RUNNING ||
             state == TASK_BLOCKED || state == TASK_DEAD)) {
            users++;
        }
    }
    return users;
}

/*
 * A thread: a task that runs in the caller's address space rather than a copy of
 * it, starting at `entry` on `user_stack`. That is the whole difference from
 * fork - there is no new space and no copied memory, so what one thread writes
 * the other sees, which is the point and also the hazard: nothing here
 * serialises two of them, so a thread that allocates while another allocates is
 * still a race. What it does get right is the lifetime: the space outlives the
 * first of them to exit.
 */
s64 sched_clone(u64 entry, u64 user_stack)
{
    if (current == NULL || current->mode32) {
        return -1;
    }
    if (user_stack == 0 || !vmm_user_mapped(entry) ||
        !vmm_user_mapped(user_stack - 1)) {
        return -1;                  /* the entry or the stack is not in the space */
    }

    int slot = -1;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state == TASK_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (task_count >= MAX_TASKS) {
            return -1;
        }
        slot = task_count++;
    }

    void *kstack = kmem_alloc(stack_cache);
    if (kstack == NULL) {
        return -1;
    }

    struct lm_task *child = &tasks[slot];
    struct lm_task *parent = current;

    *child = *parent;
    child->id = (u64)slot;
    child->state = TASK_FREE;       /* the frame is not there yet */
    child->kstack_obj = kstack;
    child->kstack_base = (u64)(unsigned long)kstack;
    child->kstack_top = child->kstack_base + FRAME_SIZE;
    child->cr3 = parent->cr3;       /* the same space, not a copy of it */
    child->parent = parent->id;
    child->expect_exit = TASK_ANY_EXIT;
    child->self_report = 0;
    child->calls = 0;
    child->ticks = 0;
    child->exit_code = 0;
    child->exit_whoami = 0;
    child->exit_ping = 0;

    /* A frame of its own, built rather than copied: it enters ring 3 at `entry`
     * on the stack it was given, with interrupts on, which is what a thread's
     * first instruction in the world looks like. */
    struct lm_fault_frame *frame =
        (struct lm_fault_frame *)(unsigned long)(child->kstack_top -
                                                 sizeof(struct lm_fault_frame));
    memset(frame, 0, sizeof(*frame));
    frame->rip = entry;
    /* The selectors need their ring in the low two bits: IRETQ refuses a CS whose
     * RPL is not the privilege it is returning to, so these are the same values
     * the syscall entry stub pushes (0x23 and 0x1B) and not the raw table
     * indexes. */
    frame->cs = SEL_UCODE | 3;
    frame->rflags = 0x202;          /* IF, and the bit that is always set */
    frame->rsp = user_stack;
    frame->ss = SEL_UDATA | 3;
    child->rsp = (u64)(unsigned long)frame;

    child->state = TASK_READY;
    return (s64)slot;
}

/*
 * 0 when the child has been reaped and *code is its exit code, 1 while it is
 * still running, -1 when pid is not this task's child or is already gone.
 *
 * It does not block. A syscall cannot: the tick refuses to switch away from the
 * kernel, and a syscall that yields returns to the *caller* rather than resuming
 * inside the kernel, so "wait" here means the same thing "getchar" means - ask
 * again after a sleep.
 */
int sched_wait(s64 pid, u64 *code)
{
    if (current == NULL || pid < 0 || pid >= (s64)task_count) {
        return -1;
    }

    struct lm_task *child = &tasks[pid];
    if (child->state == TASK_FREE || child->parent != current->id ||
        child->id != (u64)pid) {
        return -1;                  /* not ours, or already reaped */
    }
    if (child->state != TASK_DEAD) {
        return 1;                   /* still running */
    }

    if (code != NULL) {
        *code = child->exit_code;
    }

    /* Reap: the slot, the kernel stack and - when this was the last task living
     * in it - the whole address space go back. A thread's space is still in use
     * by whatever created it. */
    u64 id = child->id;
    if (space_users(child->cr3) <= 1) {
        vmm_space_destroy(child->cr3);
    }
    if (child->kstack_obj != NULL) {
        kmem_free(stack_cache, child->kstack_obj);
    }
    memset(child, 0, sizeof(*child));
    child->id = id;
    child->state = TASK_FREE;
    child->parent = TASK_NO_PARENT;
    return 0;
}

/*
 * Kill: retire another task without waiting for it to reach an exit. The task
 * cannot be the caller (it is the one running) and cannot already be gone, and
 * its slot, stack and space are left to its parent's waitpid - so a killed child
 * is reaped exactly like one that exited.
 */
int sched_kill(s64 pid, u64 signal)
{
    if (current == NULL || pid < 0 || pid >= (s64)task_count) {
        return -1;
    }
    struct lm_task *t = &tasks[pid];
    if (t->state != TASK_READY || t == current) {
        return -1;
    }

    t->exit_code = 0x8E000000ULL | (signal & 0xFF);   /* 'killed by signal n' */
    t->state = TASK_DEAD;
    vfs_release_task(t->id);
    wake_waiting_parent(t->id);

    kputs("[proc] pid=");
    kput_dec((u64)pid);
    kputs(" killed, signal ");
    kput_dec(signal);
    kputs("\r\n");
    return 0;
}

/*
 * A user program that faults is retired like one that exits, with the vector in
 * its exit code: the machine keeps running, its parent can wait for it, and the
 * record says what happened. Halting here would let one bad program - or a file
 * that was not a program at all - take the system down.
 */
struct lm_fault_frame *sched_faulted(struct lm_fault_frame *f)
{
    if (current == NULL) {
        return NULL;
    }
    f->rdi = 0xFA000000ULL | (f->vec & 0xFF);
    return sched_exit(f);
}

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
    wake_waiting_parent(t->id);

    struct lm_task *next = pick_next();
    if (next == NULL) {
        lm_sched_idle();            /* does not return */
    }
    become(next);
    return (struct lm_fault_frame *)(unsigned long)next->rsp;
}
