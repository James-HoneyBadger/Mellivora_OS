/*
 * lm_user.c - the ring-3 path for the 64-bit nucleus
 *
 * Roadmap P2. Everything above this point runs at CPL 0; this file adds the
 * three pieces a user program needs:
 *
 *   - a GDT with user code/data descriptors and a 64-bit TSS, so IRETQ can
 *     return to ring 3 and a privilege transition has somewhere to land
 *     (TSS.RSP0 - the CPU loads it on any interrupt taken from ring 3);
 *   - a user address space: two frames from the frame allocator mapped at
 *     0x40000000 through the VMM, which sets U/S at every level of the walk.
 *     The pages stage2 built for the kernel stay supervisor-only - only the
 *     shared PML4 entry gains U/S, and the leaf under it decides access;
 *   - the return to a user stack: IRETQ pops SS, RSP, RFLAGS, CS and RIP from
 *     the frame the entry stub built, which is what makes the CPU resume with
 *     the user's own stack pointer and privilege.
 *
 * The user program is assembled into .rodata and copied into a frame, so the
 * page it runs from contains nothing but its own code.
 */

#include "lm.h"

/* The user program's pages: one shared code page (read/execute, so both tasks
 * run the same code) and one stack per task. 0x40000000 is PDPT[1], which
 * stage2 left empty. */
#define USER_CODE_ADDR  (0x40000000ULL)
#define USER_STACK0     (0x40001000ULL)
#define USER_STACK1     (0x40002000ULL)
#define USER_STACK_TOP(s) ((s) + FRAME_SIZE)

/*---------------------------------------
 * GDT and TSS
 *---------------------------------------*/

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

/* 64-bit TSS (Intel SDM Vol. 3, 7.2.3): the RSP0 stack is what a privilege
 * transition through an interrupt or exception uses. An iomap_base past the end
 * of the segment means "no I/O bitmap", so a user `in`/`out` faults. */
struct tss64 {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

static u64 gdt[7];
static struct tss64 tss;
static struct gdt_ptr gdtr;

static u64 gdt_desc(u64 base, u32 limit, u8 access, u8 flags)
{
    /* Byte 6 carries limit[19:16], AVL, L, D/B and G; `flags` is that top
     * nibble (G=8, D=4, L=2, AVL=1), so it starts at bit 52, not bit 48. */
    return (u64)(limit & 0xFFFF)
         | ((base & 0xFFFFFFULL) << 16)
         | ((u64)access << 40)
         | ((u64)((limit >> 16) & 0xF) << 48)
         | ((u64)(flags & 0xF) << 52)
         | (((base >> 24) & 0xFFULL) << 56);
}

static void lm_user_load_gdt(void)
{
    gdt[0] = 0;
    /* access 0x9A/0x92: present, DPL 0, code/data, readable/writable.
     * access 0xFA/0xF2: the same with DPL 3.
     * flags 0x2: long mode (L=1, D=0); a data segment ignores both. */
    gdt[1] = gdt_desc(0, 0, 0x9A, 0x2);   /* SEL_KCODE */
    gdt[2] = gdt_desc(0, 0, 0x92, 0x0);   /* SEL_KDATA */
    gdt[3] = gdt_desc(0, 0, 0xF2, 0x0);   /* SEL_UDATA */
    gdt[4] = gdt_desc(0, 0, 0xFA, 0x2);   /* SEL_UCODE */

    /* A 64-bit TSS descriptor is 16 bytes: type 9 (64-bit TSS available),
     * present, DPL 0, and the base is split around the middle. */
    u64 base = (u64)(unsigned long)&tss;
    gdt[5] = gdt_desc(base, sizeof(tss) - 1, 0x89, 0x0);
    gdt[6] = base >> 32;

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)(unsigned long)gdt;
    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    /* Reload CS from the new table; the selector number is unchanged, but the
     * descriptor behind it is not, and a far return is the only way to load CS
     * from ring 0 without an interrupt. */
    __asm__ volatile("pushq $0x08\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n"
                     "1:\n\t"
                     : : : "rax", "memory");
    __asm__ volatile("movw %w0, %%ds\n\t"
                     "movw %w0, %%es\n\t"
                     "movw %w0, %%ss\n\t"
                     "movw %w0, %%fs\n\t"
                     : : "r"((u16)SEL_KDATA) : "memory");

    tss.rsp0 = cpu_this()->kstack_top;
    tss.iomap_base = (u16)sizeof(tss);
    __asm__ volatile("ltr %w0" : : "r"((u16)SEL_TSS));
}

/*---------------------------------------
 * User address space
 *---------------------------------------*/

/*---------------------------------------
 * The user program
 *
 * Both tasks run this: PING(0x1234) returns 0x1234, which the program pushes
 * onto its own stack; it asks for its task id, waits for two more timer ticks
 * (surrendering the CPU to the other task whenever one arrives), asks WHOAMI,
 * pops the PING result back and exits with its own id as the code. The nucleus
 * checks the id, the PING value and the WHOAMI answer per task, so a lost
 * register, a task whose stack was not writable or a preemption that resumed
 * the wrong frame shows up as a failed task rather than a passing run.
 *---------------------------------------*/

__asm__(
    ".pushsection .rodata\n"
    ".globl lm_user_program\n"
    ".globl lm_user_program_end\n"
    "lm_user_program:\n"
    /* INT3 first: the one fault a handler can resume, and taking it from ring 3
     * is what proves the TSS.RSP0 transition (the CPU has to load the kernel
     * stack before the handler runs). */
    "    int3\n"
    "    movl $1, %eax\n"          /* LM_SYS_PING */
    "    movl $0x1234, %edi\n"
    "    syscall\n"
    "    pushq %rax\n"             /* the result must survive on the user stack */
    "    movl $5, %eax\n"          /* LM_SYS_GETTID */
    "    syscall\n"
    "    movq %rax, %r12\n"        /* the task's own id, kept across syscalls */
    /* Wait for two more ticks. The clock is shared, so this is the point where
     * the other task gets the CPU: a tick taken in ring 3 preempts. RBX is the
     * counter because SYSCALL itself clobbers RCX and R11. */
    "    movl $4, %eax\n"          /* LM_SYS_TICKS */
    "    syscall\n"
    "    leaq 2(%rax), %r13\n"     /* target */
    "    movl $100000, %ebx\n"
    "1:  movl $4, %eax\n"          /* LM_SYS_TICKS */
    "    syscall\n"
    "    cmpq %r13, %rax\n"
    "    jae 2f\n"
    "    movl $2000, %r8d\n"       /* spin in user mode between polls */
    "4:  decl %r8d\n"
    "    jnz 4b\n"
    "    decl %ebx\n"
    "    jnz 1b\n"
    "    movl $3, %eax\n"          /* LM_SYS_EXIT: no tick, so fail loudly */
    "    movl $0xBAD, %edi\n"
    "    syscall\n"
    "2:  movl $2, %eax\n"          /* LM_SYS_WHOAMI */
    "    syscall\n"
    "    movq %rax, %rsi\n"        /* the nucleus checks this is its CPU id */
    "    popq %rdx\n"              /* 0x1234, back off the user stack */
    "    movl %r12d, %edi\n"       /* exit code: the task id */
    "    movl $3, %eax\n"          /* LM_SYS_EXIT */
    "    syscall\n"
    "3:  jmp 3b\n"                 /* the kernel never returns to a program */
    "lm_user_program_end:\n"
    ".popsection\n");

extern const u8 lm_user_program[];
extern const u8 lm_user_program_end[];

/* IRETQ to a ring-3 CS pops a five-value frame from the *current* (kernel)
 * stack, so it is built here and the CPU switches stacks as part of the return.
 * RDI = entry, RSI = user RSP, RDX = RFLAGS.
 *
 * No SWAPGS here: the two halves are already the way the user program needs
 * them - the live GS base is the user value and KERNEL_GS_BASE is the per-CPU
 * block (see cpu_init), which is exactly the state a SYSCALL's SWAPGS expects
 * to find. */
__asm__(
    ".text\n"
    /* lm_enter_task: RDI is a task's saved frame, in exactly the layout the
     * entry stubs leave behind, so starting a task and resuming a preempted one
     * are the same code. No SWAPGS: the kernel's GS base is already the user
     * value (see lm_user_enter). */
    ".globl lm_enter_task\n"
    "lm_enter_task:\n"
    "    movq %rdi, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %r11\n"
    "    popq %r10\n"
    "    popq %r9\n"
    "    popq %r8\n"
    "    popq %rbp\n"
    "    popq %rdi\n"
    "    popq %rsi\n"
    "    popq %rdx\n"
    "    popq %rcx\n"
    "    popq %rbx\n"
    "    popq %rax\n"
    "    addq $16, %rsp\n"          /* vector and error code */
    "    iretq\n"
    ".globl lm_user_enter\n"
    "lm_user_enter:\n"
    "    pushq $0x1B\n"             /* SEL_UDATA | RPL_USER */
    "    pushq %rsi\n"              /* user RSP */
    "    pushq %rdx\n"              /* user RFLAGS */
    "    pushq $0x23\n"             /* SEL_UCODE | RPL_USER */
    "    pushq %rdi\n"              /* user RIP */
    "    iretq\n");

int lm_user_run(void)
{
    lm_user_load_gdt();

    u64 code_phys = pmm_alloc_frame();
    u64 stack0_phys = pmm_alloc_frame();
    u64 stack1_phys = pmm_alloc_frame();
    if (code_phys == 0 || stack0_phys == 0 || stack1_phys == 0) {
        kputs("[ring3] FAIL no frames for the user address space\r\n");
        return -1;
    }

    u8 *code = vmm_phys(code_phys);
    u8 *stack0 = vmm_phys(stack0_phys);
    u8 *stack1 = vmm_phys(stack1_phys);
    if (code == NULL || stack0 == NULL || stack1 == NULL) {
        kputs("[ring3] FAIL user frames outside the direct map\r\n");
        return -1;
    }

    u64 program_bytes = (u64)(lm_user_program_end - lm_user_program);
    if (program_bytes > FRAME_SIZE) {
        kputs("[ring3] FAIL user program larger than a page\r\n");
        return -1;
    }
    memcpy(code, (const void *)lm_user_program, program_bytes);
    memset(stack0, 0, FRAME_SIZE);
    memset(stack1, 0, FRAME_SIZE);

    /* The VMM sets U/S on the way down; the code page is shared and read-only,
     * the stacks are writable, and the result is checked rather than assumed,
     * including that the kernel's own mappings did not become user-accessible. */
    if (vmm_map(USER_CODE_ADDR, code_phys, VMM_U) != 0 ||
        vmm_map(USER_STACK0, stack0_phys, VMM_W | VMM_U) != 0 ||
        vmm_map(USER_STACK1, stack1_phys, VMM_W | VMM_U) != 0) {
        kputs("[ring3] FAIL could not map the user address space\r\n");
        return -1;
    }
    if (!vmm_user_mapped(USER_CODE_ADDR) || !vmm_user_mapped(USER_STACK0) ||
        !vmm_user_mapped(USER_STACK1) || vmm_user_mapped(0) ||
        vmm_user_mapped(0x00100000) || vmm_user_mapped(0xFFFFFFFF80100000ULL)) {
        kputs("[ring3] FAIL user pages are not isolated\r\n");
        return -1;
    }

    kputs("[ring3] gdt=0x");
    kput_hex((u64)(unsigned long)gdt, 16);
    kputs(" tss rsp0=0x");
    kput_hex(tss.rsp0, 16);
    kputs("\r\n[ring3] code=0x");
    kput_hex(USER_CODE_ADDR, 8);
    kputs(" bytes=");
    kput_dec(program_bytes);
    kputs(" stacks=0x");
    kput_hex(USER_STACK_TOP(USER_STACK0), 8);
    kputs(",0x");
    kput_hex(USER_STACK_TOP(USER_STACK1), 8);
    kputs("\r\n");

    sched_init();
    if (sched_create(USER_CODE_ADDR, USER_STACK_TOP(USER_STACK0)) != 0 ||
        sched_create(USER_CODE_ADDR, USER_STACK_TOP(USER_STACK1)) != 0) {
        kputs("[ring3] FAIL could not create the tasks\r\n");
        return -1;
    }

    sched_start();      /* enters ring 3; does not return */
    return -1;
}

void lm_tss_set_rsp0(u64 top)
{
    tss.rsp0 = top;
}
