/*
 * lm_idt.c - the 64-bit IDT and the exception entry path
 *
 * Roadmap P2. Until this file existed any fault was a triple fault: the IDTR
 * still described the firmware's real-mode table, and stage2 never built one.
 * A port needs the opposite - a fault has to leave enough behind to say what
 * happened and where - so this is the piece the ring-3 path was waiting on:
 * TSS.RSP0 is what the CPU loads when ring 3 faults, and that is the only way
 * to prove a privilege transition through the TSS actually works.
 *
 * Layout of the entry stack (low to high), which struct lm_fault_frame and the
 * asserts in lm_nucleus.c pin down:
 *
 *   vector   pushed by the stub
 *   error    pushed by the CPU (or a zero the stub pushes for vectors that
 *            do not have one, so every frame looks the same)
 *   RIP, CS, RFLAGS            pushed by the CPU
 *   RSP, SS                    pushed by the CPU only for a privilege change
 *   the fifteen general registers
 *
 * `RSP`/`SS` move the register block by 16 bytes when they are absent (a fault
 * from ring 0), so the C side only reads them when the frame's CS says ring 3.
 */

#include "lm.h"

/* Vectors 0..21: the architecturally defined exceptions. Anything higher is
 * left non-present, and the CPU turns an attempt to use it into #GP - which
 * lands here anyway. */
#define LM_ISR_COUNT 22

/*
 * Two stub kinds, because the CPU pushes an error code for some vectors and
 * not others (Intel SDM Vol. 3, 6.13). The ones without get a zero so the C
 * frame is the same shape either way.
 */
__asm__(
    ".text\n"
    ".macro LM_ISR_NOERR v\n"
    "lm_isr_\\v:\n"
    "    pushq $0\n"
    "    pushq $\\v\n"
    "    jmp lm_isr_common\n"
    ".endm\n"
    ".macro LM_ISR_ERR v\n"
    "lm_isr_\\v:\n"
    "    pushq $\\v\n"
    "    jmp lm_isr_common\n"
    ".endm\n"
    "LM_ISR_NOERR 0\n"      /* #DE */
    "LM_ISR_NOERR 1\n"      /* #DB */
    "LM_ISR_NOERR 2\n"      /* NMI */
    "LM_ISR_NOERR 3\n"      /* #BP */
    "LM_ISR_NOERR 4\n"      /* #OF */
    "LM_ISR_NOERR 5\n"      /* #BR */
    "LM_ISR_NOERR 6\n"      /* #UD */
    "LM_ISR_NOERR 7\n"      /* #NM */
    "LM_ISR_ERR   8\n"      /* #DF */
    "LM_ISR_NOERR 9\n"      /* reserved */
    "LM_ISR_ERR   10\n"     /* #TS */
    "LM_ISR_ERR   11\n"     /* #NP */
    "LM_ISR_ERR   12\n"     /* #SS */
    "LM_ISR_ERR   13\n"     /* #GP */
    "LM_ISR_ERR   14\n"     /* #PF */
    "LM_ISR_NOERR 15\n"     /* reserved */
    "LM_ISR_NOERR 16\n"     /* #MF */
    "LM_ISR_ERR   17\n"     /* #AC */
    "LM_ISR_NOERR 18\n"     /* #MC */
    "LM_ISR_NOERR 19\n"     /* #XM */
    "LM_ISR_NOERR 20\n"     /* #VE */
    "LM_ISR_ERR   21\n"     /* #CP */
    ".globl lm_isr_common\n"
    "lm_isr_common:\n"
    /* The same GS arrangement as the syscall stub: KERNEL_GS_BASE holds the
     * per-CPU block, and a fault can come from either ring. */
    "    swapgs\n"
    "    pushq %rax\n"
    "    pushq %rbx\n"
    "    pushq %rcx\n"
    "    pushq %rdx\n"
    "    pushq %rsi\n"
    "    pushq %rdi\n"
    "    pushq %rbp\n"
    "    pushq %r8\n"
    "    pushq %r9\n"
    "    pushq %r10\n"
    "    pushq %r11\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, %rdi\n"     /* struct lm_fault_frame * */
    "    call lm_fault_dispatch\n"
    "    testq %rax, %rax\n"
    "    jz 9f\n"               /* the handler does not want to resume */
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
    "    addq $16, %rsp\n"      /* vector and error code */
    "    swapgs\n"
    "    iretq\n"
    "9:\n"
    "    cli\n"
    "    hlt\n"
    "    jmp 9b\n"
    ".text\n");

/* The stub addresses, in vector order, for the IDT builder. */
extern void *const lm_isr_table[LM_ISR_COUNT];

__asm__(
    ".pushsection .rodata\n"
    ".globl lm_isr_table\n"
    "lm_isr_table:\n"
    ".quad lm_isr_0,  lm_isr_1,  lm_isr_2,  lm_isr_3,  lm_isr_4,  lm_isr_5\n"
    ".quad lm_isr_6,  lm_isr_7,  lm_isr_8,  lm_isr_9,  lm_isr_10, lm_isr_11\n"
    ".quad lm_isr_12, lm_isr_13, lm_isr_14, lm_isr_15, lm_isr_16, lm_isr_17\n"
    ".quad lm_isr_18, lm_isr_19, lm_isr_20, lm_isr_21\n"
    ".popsection\n");

static u64 idt[256 * 2];    /* 16 bytes per entry, 256 vectors */
static u64 fault_count;
static u64 fault_cr2;

static void idt_set(int vector, u64 handler, u8 dpl)
{
    /* offset[15:0], selector, IST=0, type/attributes, offset[31:16]; the high
     * half of the offset lives in the second qword. 0xE is a 64-bit interrupt
     * gate, so IF is cleared on entry. */
    u64 type_attr = 0x8EULL | ((u64)dpl << 5);
    idt[vector * 2 + 0] = (handler & 0xFFFFULL)
                        | ((u64)SEL_KCODE << 16)
                        | (type_attr << 40)
                        | (((handler >> 16) & 0xFFFFULL) << 48);
    idt[vector * 2 + 1] = handler >> 32;
}

void idt_init(void)
{
    for (u64 i = 0; i < 256 * 2; i++) {
        idt[i] = 0;             /* absent: an unused vector becomes #GP */
    }
    for (int v = 0; v < LM_ISR_COUNT; v++) {
        /* INT3 is executed by the user program, so its gate has to allow CPL 3;
         * the CPU ignores DPL for the exceptions it raises itself. */
        idt_set(v, (u64)(unsigned long)lm_isr_table[v], v == 3 ? 3 : 0);
    }

    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) idtr;

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)(unsigned long)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

static u64 read_cr2(void)
{
    u64 value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static const char *const fault_names[LM_ISR_COUNT] = {
    "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "reserved", "#TS", "#NP", "#SS", "#GP", "#PF", "reserved",
    "#MF", "#AC", "#MC", "#XM", "#VE", "#CP",
};

/*
 * Called on the stack the CPU chose. For a ring-3 fault that is TSS.RSP0 - the
 * per-CPU kernel stack - and proving so is the point of the check below: a
 * same-privilege fault keeps whatever kernel stack was in use, which is why
 * `kstack` is only ever "ok" for a privilege transition.
 *
 * Returns 1 to resume the interrupted context (#BP is the one vector this
 * nucleus can resume: INT3's saved RIP already points past it) and 0 to stop.
 */
int lm_fault_dispatch(struct lm_fault_frame *f)
{
    struct lm_cpu *cpu = cpu_this();
    u64 rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    fault_count++;
    fault_cr2 = f->vec == 14 ? read_cr2() : 0;

    int from_user = (f->cs & 3) == 3;
    int on_kstack = rsp <= cpu->kstack_top && rsp > cpu->kstack_base;
    int expected = f->vec == 3 && from_user && on_kstack;

    kputs(expected ? "[fault] " : "[fault] FAIL ");
    kputs("n=");
    kput_dec(fault_count);
    kputs(" vec=");
    kput_dec(f->vec);
    kputs(" (");
    kputs(f->vec < LM_ISR_COUNT ? fault_names[f->vec] : "?");
    kputs(") err=0x");
    kput_hex(f->err, 4);
    kputs(" rip=0x");
    kput_hex(f->rip, 16);
    kputs(" cs=0x");
    kput_hex(f->cs, 4);
    if (from_user) {
        /* Present only when the CPU switched privilege: the user's own stack,
         * saved in the frame rather than read from a register. */
        kputs(" rsp=0x");
        kput_hex(f->rsp, 16);
    }
    if (f->vec == 14) {
        kputs(" cr2=0x");
        kput_hex(fault_cr2, 16);
    }
    kputs(" stack=");
    kputs(on_kstack ? "ok" : "FAIL");
    kputs(expected ? " fault=ok\r\n" : "\r\n");

    return expected;
}
