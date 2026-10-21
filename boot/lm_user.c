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
 *     0x40000000, with U/S set at every level of the walk. The pages stage2
 *     built for the kernel stay supervisor-only - only the PML4 entry is
 *     shared, and the identity map's PDPT still has no U/S bit;
 *   - the return to a user stack: IRETQ pops SS, RSP, RFLAGS, CS and RIP from
 *     the frame the entry stub built, which is what makes the CPU resume with
 *     the user's own stack pointer and privilege.
 *
 * The user program is assembled into .rodata and copied into a frame, so the
 * page it runs from contains nothing but its own code.
 */

#include "lm.h"

#define PG_P 0x1ULL
#define PG_W 0x2ULL
#define PG_U 0x4ULL

/* stage2 maps physical 0..16 MB at DIRECT_BASE as well as at 0, which is how
 * this nucleus reaches a physical frame to fill it in. A frame above that range
 * is not addressable yet - a real VMM would map it on demand. */
#define DIRECT_BASE  0xFFFFFFFF80000000ULL
#define DIRECT_LIMIT 0x01000000ULL

#define EXIT_MAGIC 0x1234ULL   /* what the user program must hand back at exit */

/* The user program's two pages: code (read/execute) and stack (read/write).
 * 0x40000000 is PDPT[1], which stage2 left empty. */
#define USER_CODE_ADDR  (0x40000000ULL)
#define USER_STACK_ADDR (0x40001000ULL)
#define USER_STACK_TOP  (USER_STACK_ADDR + FRAME_SIZE)

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

static void *direct(u64 phys)
{
    if (phys == 0 || phys >= DIRECT_LIMIT) {
        return NULL;
    }
    return (void *)(unsigned long)(DIRECT_BASE + phys);
}

static int map_user_pages(u64 code_phys, u64 stack_phys)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    u64 *pml4 = direct(cr3 & ~(FRAME_SIZE - 1));
    if (pml4 == NULL || (pml4[0] & PG_P) == 0) {
        return -1;
    }
    u64 *pdpt = direct(pml4[0] & ~(FRAME_SIZE - 1));
    if (pdpt == NULL) {
        return -1;
    }

    /* 0x40000000 is PDPT[1]/PD[0]/PT[0..1], and PDPT[1] is unused, so the user
     * space gets its own tables instead of splitting a 2 MB kernel page. */
    u64 pd_phys = pmm_alloc_frame();
    u64 pt_phys = pmm_alloc_frame();
    u64 *pd = direct(pd_phys);
    u64 *pt = direct(pt_phys);
    if (pd == NULL || pt == NULL) {
        if (pd_phys != 0) {
            pmm_free_frame(pd_phys);
        }
        if (pt_phys != 0) {
            pmm_free_frame(pt_phys);
        }
        return -1;
    }
    for (u64 i = 0; i < 512; i++) {
        pd[i] = 0;
        pt[i] = 0;
    }

    /* The kernel's own mappings must stay supervisor-only - ring 3 reaching the
     * nucleus through stage2's identity map or the higher half would undo the
     * whole point of the exercise. */
    if ((pdpt[0] & PG_U) != 0 || (pml4[511] & PG_U) != 0 ||
        (pdpt[510] & PG_U) != 0) {
        return -1;
    }

    /* U/S on the shared PML4 entry lets the walk below it be user-accessible;
     * the identity map's PDPT entry has no U/S, so 0..1 GB stays kernel-only. */
    pml4[0] |= PG_U;
    pdpt[1] = pd_phys | PG_P | PG_W | PG_U;
    pd[0] = pt_phys | PG_P | PG_W | PG_U;
    pt[(USER_CODE_ADDR >> 12) & 511] = code_phys | PG_P | PG_U;
    pt[(USER_STACK_ADDR >> 12) & 511] = stack_phys | PG_P | PG_W | PG_U;

    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    return 0;
}

/*---------------------------------------
 * The user program
 *
 * PING(0x1234) returns 0x1234; the program pushes it onto its own stack, asks
 * WHOAMI, then exits with the value it popped. If the return to ring 3 lost
 * RAX, the user stack were not writable, or the second syscall clobbered a
 * register, the exit code would not be 0x1234 and lm_user_exited() says so.
 *---------------------------------------*/

__asm__(
    ".pushsection .rodata\n"
    ".globl lm_user_program\n"
    ".globl lm_user_program_end\n"
    "lm_user_program:\n"
    /* INT3 first: it is the one fault a handler can resume, and taking it from
     * ring 3 is what proves the TSS.RSP0 transition (the CPU has to load the
     * kernel stack before the handler runs). */
    "    int3\n"
    "    movl $1, %eax\n"          /* LM_SYS_PING */
    "    movl $0x1234, %edi\n"
    "    syscall\n"
    "    pushq %rax\n"             /* the result must survive on the user stack */
    "    movl $2, %eax\n"          /* LM_SYS_WHOAMI */
    "    syscall\n"
    "    movq %rax, %rsi\n"
    "    popq %rdi\n"              /* 0x1234, back off the user stack */
    "    movl $3, %eax\n"          /* LM_SYS_EXIT */
    "    syscall\n"
    "1:  jmp 1b\n"                 /* the kernel never returns to a program */
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
    struct lm_cpu *cpu = cpu_this();

    lm_user_load_gdt();

    u64 code_phys = pmm_alloc_frame();
    u64 stack_phys = pmm_alloc_frame();
    if (code_phys == 0 || stack_phys == 0) {
        kputs("[ring3] FAIL no frames for the user address space\r\n");
        return -1;
    }

    u8 *code = direct(code_phys);
    u8 *stack = direct(stack_phys);
    if (code == NULL || stack == NULL) {
        kputs("[ring3] FAIL user frames outside the direct map\r\n");
        return -1;
    }

    u64 program_bytes = (u64)(lm_user_program_end - lm_user_program);
    if (program_bytes > FRAME_SIZE) {
        kputs("[ring3] FAIL user program larger than a page\r\n");
        return -1;
    }
    memcpy(code, (const void *)lm_user_program, program_bytes);
    memset(stack, 0, FRAME_SIZE);

    if (map_user_pages(code_phys, stack_phys) != 0) {
        kputs("[ring3] FAIL could not map the user address space\r\n");
        return -1;
    }

    kputs("[ring3] gdt=0x");
    kput_hex((u64)(unsigned long)gdt, 16);
    kputs(" tss rsp0=0x");
    kput_hex(tss.rsp0, 16);
    kputs("\r\n[ring3] code=0x");
    kput_hex(USER_CODE_ADDR, 8);
    kputs(" ustack=0x");
    kput_hex(USER_STACK_TOP, 8);
    kputs(" bytes=");
    kput_dec(program_bytes);
    kputs("\r\n");

    /* IF stays clear: there is no IDT yet, so an interrupt taken in ring 3
     * would fault, and the only thing this program needs is SYSCALL. */
    cpu->user_mode = 1;
    lm_user_enter(USER_CODE_ADDR, USER_STACK_TOP, 0x2);
}

void lm_user_exited(u64 code)
{
    struct lm_cpu *cpu = cpu_this();
    int ok = 1;

    if (code != EXIT_MAGIC) {
        ok = 0;                     /* RAX or the user stack lost the PING result */
    }
    if (cpu->exit_arg != cpu->id) {
        ok = 0;                     /* WHOAMI's answer did not reach the program */
    }
    if (cpu->user_calls != 3) {
        ok = 0;
    }
    if (cpu->last_user_rip < USER_CODE_ADDR ||
        cpu->last_user_rip >= USER_CODE_ADDR + FRAME_SIZE) {
        ok = 0;                     /* the syscall did not come from the user page */
    }
    if (cpu->last_user_rsp <= USER_STACK_ADDR ||
        cpu->last_user_rsp > USER_STACK_TOP) {
        ok = 0;                     /* ... or not with the user stack */
    }

    kputs("[ring3] calls=");
    kput_dec(cpu->user_calls);
    kputs(" exit=0x");
    kput_hex(code, 4);
    kputs(" whoami=");
    kput_dec(cpu->exit_arg);
    kputs("\r\n[ring3] rip=0x");
    kput_hex(cpu->last_user_rip, 8);
    kputs(" rsp=0x");
    kput_hex(cpu->last_user_rsp, 8);
    kputs(" cs=0x23 ss=0x1B\r\n");
    kputs(ok ? "[ring3] ring3=ok\r\n" : "[ring3] FAIL ring-3 round trip\r\n");

    kputs("MELLIVORA64_OK\r\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
