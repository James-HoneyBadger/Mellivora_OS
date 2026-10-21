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

static u64 gdt[8];
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
    gdt[4] = gdt_desc(0, 0, 0xFA, 0x2);   /* SEL_UCODE (L=1) */
    /* The 32-bit tree's programs run in compatibility mode: D=1, L=0, DPL 3. It
     * lives outside the STAR window (0x18 data / 0x20 code) because SYSRET's
     * +16/+8 arithmetic fixes those two slots. */
    gdt[7] = gdt_desc(0, 0, 0xFA, 0x4);   /* SEL_UCODE32 */

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

/*
 * What the boot test runs: the 64-bit demo (which execs /hello), the small
 * 32-bit demo that proves two tasks see different frames at one address, and one
 * of the 32-bit tree's own programs, loaded unchanged at its PROGRAM_BASE.
 */
struct boot_task {
    const char *program;
    u64 code_va;
    u64 stack_va;
    u64 stack_top;
    int compat;
    u64 expect_exit;
};

#define BOOT_TASKS 3
#define LEGACY_CODE_ADDR  0x00300000ULL     /* the tree's PROGRAM_BASE */
#define LEGACY_STACK_ADDR 0x003FF000ULL
#define LEGACY_STACK_TOP  0x00400000ULL

static const struct boot_task boot_tasks[BOOT_TASKS] = {
    {"init",       USER_CODE_ADDR,   USER_STACK_ADDR,   USER_STACK_TOP,   0, 0},
    {"hello32",    USER_CODE_ADDR,   USER_STACK_ADDR,   USER_STACK_TOP,   1, 0x32},
    {"hello_tree", LEGACY_CODE_ADDR, LEGACY_STACK_ADDR, LEGACY_STACK_TOP, 1,
     TASK_ANY_EXIT},
};

int lm_user_run(void)
{
    lm_user_load_gdt();

    u64 kernel_space = vmm_current_space();
    u64 code_phys[BOOT_TASKS];
    u64 stack_phys[BOOT_TASKS];
    u64 space[BOOT_TASKS];
    u64 program_bytes[BOOT_TASKS];

    for (int i = 0; i < BOOT_TASKS; i++) {
        code_phys[i] = pmm_alloc_frame();
        stack_phys[i] = pmm_alloc_frame();
        space[i] = vmm_space_create();
        if (code_phys[i] == 0 || stack_phys[i] == 0 || space[i] == 0) {
            kputs("[ring3] FAIL could not build the address spaces\r\n");
            return -1;
        }

        int handle = vfs_open(boot_tasks[i].program);
        if (handle < 0) {
            kputs("[ring3] FAIL no /");
            kputs(boot_tasks[i].program);
            kputs(" on the disk\r\n");
            return -1;
        }
        program_bytes[i] = vfs_size(handle);
        u8 *code = vmm_phys(code_phys[i]);
        if (program_bytes[i] == 0 || program_bytes[i] > FRAME_SIZE ||
            code == NULL) {
            kputs("[ring3] FAIL program is not one page of code\r\n");
            return -1;
        }
        memset(code, 0, FRAME_SIZE);
        if (vfs_read(handle, code, program_bytes[i]) != (int)program_bytes[i]) {
            kputs("[ring3] FAIL short read of a program\r\n");
            return -1;
        }
        memset(vmm_phys(stack_phys[i]), 0, FRAME_SIZE);

        if (vmm_map_in(space[i], boot_tasks[i].code_va, code_phys[i],
                       VMM_U) != 0 ||
            vmm_map_in(space[i], boot_tasks[i].stack_va, stack_phys[i],
                       VMM_W | VMM_U | VMM_NX) != 0) {
            kputs("[ring3] FAIL could not map a task's pages\r\n");
            return -1;
        }
    }

    /* Check each space from the inside: its own two pages are user-accessible,
     * nothing else is, and the stack is NX while the code is not. */
    for (int i = 0; i < BOOT_TASKS; i++) {
        vmm_space_switch(space[i]);
        int ok = vmm_user_mapped(boot_tasks[i].code_va) &&
                 vmm_user_mapped(boot_tasks[i].stack_va) &&
                 !vmm_user_mapped(0) && !vmm_user_mapped(0x00100000) &&
                 vmm_translate(0x00100000) == 0 &&
                 vmm_translate(boot_tasks[i].code_va) == code_phys[i] &&
                 (!cpu_nx_enabled() ||
                  ((vmm_flags(boot_tasks[i].stack_va) & VMM_NX) != 0 &&
                   (vmm_flags(boot_tasks[i].code_va) & VMM_NX) == 0));
        vmm_space_switch(kernel_space);
        if (!ok) {
            kputs("[ring3] FAIL a task's address space is wrong\r\n");
            return -1;
        }
    }
    /* ... and the kernel's own space is not user-reachable. */
    if (vmm_user_mapped(USER_CODE_ADDR) || vmm_user_mapped(USER_STACK_ADDR) ||
        vmm_user_mapped(LEGACY_CODE_ADDR)) {
        kputs("[ring3] FAIL the kernel space has user pages\r\n");
        return -1;
    }

    kputs("[nx] stack=");
    kputs(cpu_nx_enabled() ? "nx" : "off");
    kputs(" code=");
    kputs(cpu_nx_enabled() ? "exec" : "off");
    kputs(cpu_nx_enabled() ? " nx=ok\r\n" : "\r\n");

    kputs("[ring3] gdt=0x");
    kput_hex((u64)(unsigned long)gdt, 16);
    kputs(" tss rsp0=0x");
    kput_hex(tss.rsp0, 16);
    kputs("\r\n[vfs]");
    for (int i = 0; i < BOOT_TASKS; i++) {
        kputs(" /");
        kputs(boot_tasks[i].program);
        kputs("=");
        kput_dec(program_bytes[i]);
    }
    kputs("\r\n");
    kputs("[ring3] tasks=");
    kput_dec(BOOT_TASKS);
    kputs(" spaces=");
    kput_dec(BOOT_TASKS);
    kputs(" code=0x");
    kput_hex(USER_CODE_ADDR, 8);
    kputs("/0x");
    kput_hex(LEGACY_CODE_ADDR, 8);
    kputs(" mode=64,32,32");
    kputs("\r\n");

    sched_init();
    for (int i = 0; i < BOOT_TASKS; i++) {
        if (sched_create(boot_tasks[i].code_va, boot_tasks[i].stack_top,
                         space[i], boot_tasks[i].compat,
                         boot_tasks[i].expect_exit) != 0) {
            kputs("[ring3] FAIL could not create the tasks\r\n");
            return -1;
        }
    }

    sched_start();      /* enters ring 3; does not return */
    return -1;
}

void lm_tss_set_rsp0(u64 top)
{
    tss.rsp0 = top;
}
