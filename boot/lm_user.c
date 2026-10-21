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

/* The user program's pages, at the same addresses in every task's own address
 * space: one code frame, shared read-only, and one stack page per task. That is
 * what makes the isolation test meaningful - both tasks write the same virtual
 * address and each must read back its own value. 0x40000000 is PDPT[1], which
 * stage2 left empty. */
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

    u64 kernel_space = vmm_current_space();
    u64 code_phys = pmm_alloc_frame();
    u64 stack0_phys = pmm_alloc_frame();
    u64 stack1_phys = pmm_alloc_frame();
    u64 space0 = vmm_space_create();
    u64 space1 = vmm_space_create();
    u8 *code = vmm_phys(code_phys);
    if (code_phys == 0 || stack0_phys == 0 || stack1_phys == 0 ||
        space0 == 0 || space1 == 0 || code == NULL) {
        kputs("[ring3] FAIL could not build the address spaces\r\n");
        return -1;
    }

    /* The program is a file now: /init is read through the VFS (HBFS on the
     * ATA disk) into the frame both tasks will execute. */
    int handle = vfs_open("init");
    if (handle < 0) {
        kputs("[ring3] FAIL no /init on the disk\r\n");
        return -1;
    }
    u64 program_bytes = vfs_size(handle);
    if (program_bytes == 0 || program_bytes > FRAME_SIZE) {
        kputs("[ring3] FAIL /init is not one page of code\r\n");
        return -1;
    }
    memset(code, 0, FRAME_SIZE);
    if (vfs_read(handle, code, program_bytes) != (int)program_bytes) {
        kputs("[ring3] FAIL short read of /init\r\n");
        return -1;
    }
    memset(vmm_phys(stack0_phys), 0, FRAME_SIZE);
    memset(vmm_phys(stack1_phys), 0, FRAME_SIZE);

    /* The code frame is shared read-only; each stack frame belongs to one
     * space, at the same virtual address in both. The kernel's own space gets
     * no user mapping at all now. */
    if (vmm_map_in(space0, USER_CODE_ADDR, code_phys, VMM_U) != 0 ||
        vmm_map_in(space0, USER_STACK_ADDR, stack0_phys, VMM_W | VMM_U | VMM_NX) != 0 ||
        vmm_map_in(space1, USER_CODE_ADDR, code_phys, VMM_U) != 0 ||
        vmm_map_in(space1, USER_STACK_ADDR, stack1_phys, VMM_W | VMM_U | VMM_NX) != 0) {
        kputs("[ring3] FAIL could not map a task's pages\r\n");
        return -1;
    }

    /* Check each space from the inside: its two pages are user-accessible, and
     * the kernel's identity map and higher half are not there to be reached
     * (a task has no PML4[0] entry for physical memory at all). */
    for (int i = 0; i < 2; i++) {
        u64 space = i == 0 ? space0 : space1;
        vmm_space_switch(space);
        int ok = vmm_user_mapped(USER_CODE_ADDR) &&
                 vmm_user_mapped(USER_STACK_ADDR) &&
                 !vmm_user_mapped(0) && !vmm_user_mapped(0x00100000) &&
                 vmm_translate(0x00100000) == 0 &&
                 vmm_translate(USER_CODE_ADDR) == code_phys &&
                 /* The stack is data and must not execute; the code page is
                  * the one leaf without NX (nothing to check if the CPU has no
                  * NX to offer). */
                 (!cpu_nx_enabled() ||
                  ((vmm_flags(USER_STACK_ADDR) & VMM_NX) != 0 &&
                   (vmm_flags(USER_CODE_ADDR) & VMM_NX) == 0));
        vmm_space_switch(kernel_space);
        if (!ok) {
            kputs("[ring3] FAIL a task's address space is wrong\r\n");
            return -1;
        }
    }
    /* ... and the kernel's own space is not user-reachable. */
    if (vmm_user_mapped(USER_CODE_ADDR) || vmm_user_mapped(USER_STACK_ADDR)) {
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
    kputs("\r\n[vfs] /init bytes=");
    kput_dec(program_bytes);
    kputs("\r\n");
    kputs("[ring3] code=0x");
    kput_hex(USER_CODE_ADDR, 8);
    kputs(" bytes=");
    kput_dec(program_bytes);
    kputs(" stacks=0x");
    kput_hex(USER_STACK_TOP, 8);
    kputs(" in 2 spaces");
    kputs("\r\n");

    sched_init();
    if (sched_create(USER_CODE_ADDR, USER_STACK_TOP, space0) != 0 ||
        sched_create(USER_CODE_ADDR, USER_STACK_TOP, space1) != 0) {
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
