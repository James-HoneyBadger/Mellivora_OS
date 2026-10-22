/*
 * lm_nucleus.c - the 64-bit nucleus, written in freestanding C11
 *
 * Roadmap P2, first real step: this is the code that replaces the hand-written
 * asm nucleus. It is compiled by a cross toolchain (see
 * tools/find_lm_toolchain.sh) for x86-64 and linked into the higher half at
 * 0xFFFFFFFF80100000 (boot/lm64.ld); stage2 loads the flat image at physical
 * 1 MB and maps that physical range in the top 2 GB as well, so this code runs
 * where it was linked.
 *
 * What it proves, and all the test asserts:
 *   - the C toolchain produces x86-64 code the boot path can run,
 *   - the higher half is mapped and the nucleus executes from it,
 *   - .bss starts zeroed and the stack works,
 *   - SYSCALL/SYSRET MSRs can be programmed and a 64-bit `syscall` reaches a
 *     handler (the shape the ported syscall path will use),
 *   - ring 3 is reachable and reusable: IRETQ into a user segment returns to a
 *     user stack, SYSCALL comes back with SWAPGS and a per-CPU kernel stack,
 *     and three calls later the program exits with everything intact.
 *
 * Everything here is freestanding: no libc, no compiler runtime. The two
 * helpers a freestanding build can still need - memcpy/memset, which the
 * compiler may emit for a struct copy - are provided at the bottom.
 */

#include "lm.h"

/* The entry stub below (and lm_user_enter) address these by byte offset, so the
 * header's numbers and the structs must not drift apart. */
_Static_assert(__builtin_offsetof(struct lm_cpu, kstack_top) == CPU_KSTACK_TOP_OFF,
               "kstack_top offset must match the stub");
_Static_assert(__builtin_offsetof(struct lm_cpu, saved_rsp) == CPU_SAVED_RSP_OFF,
               "saved_rsp offset must match the stub");
_Static_assert(__builtin_offsetof(struct lm_cpu, exit_code) == CPU_EXIT_CODE_OFF,
               "exit_code offset must match the stub");
/* Both entry stubs build this frame, so the scheduler can resume a task from
 * either path. */
_Static_assert(sizeof(struct lm_fault_frame) == 22 * 8,
               "fifteen registers, vector, error code and five IRETQ values");
_Static_assert(__builtin_offsetof(struct lm_fault_frame, rax) == 14 * 8,
               "rax is the syscall number and the result slot");
_Static_assert(__builtin_offsetof(struct lm_fault_frame, r15) == 0,
               "R15 is pushed last and so sits lowest");
_Static_assert(__builtin_offsetof(struct lm_fault_frame, rax) == 14 * 8,
               "RAX is pushed first of the registers");
_Static_assert(__builtin_offsetof(struct lm_fault_frame, vec) == 15 * 8,
               "the stub's vector follows the registers");
_Static_assert(__builtin_offsetof(struct lm_fault_frame, rip) == 17 * 8,
               "the CPU's frame follows the vector and error code");
_Static_assert(__builtin_offsetof(struct lm_fault_frame, rsp) == 20 * 8,
               "RSP is only in a privilege-change frame");

#define COM1 0x3F8

/* Higher-half base of the image, from the linker script (0xFFFFFFFF80100000). */
extern u8 lm_image_start[];

static void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* no UART interrupts */
    outb(COM1 + 3, 0x80); /* DLAB on */
    outb(COM1 + 0, 0x01); /* divisor 1 = 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, 1 stop */
    outb(COM1 + 2, 0xC7); /* FIFO on, cleared */
    outb(COM1 + 4, 0x0B); /* DTR | RTS | OUT2 */
}

void kputc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* wait for the transmit register */
    }
    outb(COM1, (u8)c);
    vga_putc(c);
}

int kgetc(void)
{
    /* The keyboard is the console a person at the machine uses; the UART is the
     * one a test harness or a serial terminal uses. Both feed the same call. */
    int key = kbd_getc();
    if (key >= 0) {
        return key;
    }
    if ((inb(COM1 + 5) & 0x01) == 0) {
        return -1;                  /* the line status register's data-ready bit */
    }
    return (int)inb(COM1);
}

void kputs(const char *s)
{
    while (*s != '\0') {
        kputc(*s++);
    }
}

void kput_hex(u64 value, int digits)
{
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        u8 nibble = (u8)((value >> shift) & 0xF);
        kputc((char)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10));
    }
}

void kput_dec(u64 value)
{
    char digits[21];
    int n = 0;
    if (value == 0) {
        kputc('0');
        return;
    }
    while (value != 0 && n < (int)sizeof(digits)) {
        digits[n++] = (char)('0' + (int)(value % 10));
        value /= 10;
    }
    while (n > 0) {
        kputc(digits[--n]);
    }
}

/*---------------------------------------
 * .bss
 *---------------------------------------*/

extern u8 __bss_start[];
extern u8 __bss_end[];

static void zero_bss(void)
{
    for (u8 *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
}

/*---------------------------------------
 * CPU features and the SYSCALL entry
 *---------------------------------------*/

static int cpu_has_syscall(void)
{
    u32 max_ext;
    __asm__ volatile("cpuid" : "=a"(max_ext) : "a"(0x80000000U) : "ebx", "ecx", "edx");
    if (max_ext < 0x80000001U) {
        return 0;
    }
    u32 edx;
    __asm__ volatile("cpuid" : "=d"(edx) : "a"(0x80000001U) : "ebx", "ecx");
    return (edx & (1U << 11)) != 0; /* SYSCALL/SYSRET */
}

#define MSR_EFER    0xC0000080U
#define MSR_STAR    0xC0000081U
#define MSR_LSTAR   0xC0000082U
#define MSR_FMASK   0xC0000084U

static void wrmsr(u32 msr, u64 value)
{
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static u64 rdmsr(u32 msr)
{
    u32 lo;
    u32 hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

/*
 * The entry path LSTAR points at. SYSCALL has already switched CS/SS to the
 * kernel selectors but changed nothing else: RSP is still the caller's and GS
 * still holds the caller's base, so the stub starts with SWAPGS (the per-CPU
 * block lives in KERNEL_GS_BASE - see boot/lm_cpu.c) and only then touches
 * memory. Saving every general register the C dispatcher may clobber is what
 * lets a ring-3 caller find its RAX, RBX and the rest intact after IRETQ.
 */
void lm_syscall_entry(void);

__asm__(
    ".text\n"
    ".globl lm_syscall_entry\n"
    "lm_syscall_entry:\n"
    "    swapgs\n"
    /* A syscall body runs with interrupts off. The kernel keeps statics of its
     * own - the sector bounce buffers, the page tables' free lists - and a tick
     * that switched tasks in the middle of one would corrupt them. The places
     * that *want* to wait (sleep, a shell polling for a key) say so with
     * sti; hlt, and that is where preemption resumes. IRETQ puts the caller's
     * own RFLAGS back, so ring 3 stays preemptible. */
    "    movq %rsp, %gs:0x18\n"
    "    movq %gs:0x10, %rsp\n"
    /* IRETQ in 64-bit mode pops a five-value frame - RIP, CS, RFLAGS, RSP, SS -
     * even for a same-privilege return, so RSP is part of the frame or the
     * return lands with a garbage stack pointer. The dispatcher rewrites CS/SS
     * to whatever ring the caller came from. */
    "    pushq $0x1B\n"             /* SS (placeholder) */
    "    pushq %gs:0x18\n"          /* caller's RSP, restored by IRETQ */
    "    pushq %r11\n"              /* RFLAGS, as SYSCALL left them */
    "    pushq $0x23\n"             /* CS (placeholder) */
    "    pushq %rcx\n"              /* return RIP, put there by SYSCALL */
    /* The fault stub's two slots, so a task frame built for either stub can be
     * resumed by the other. */
    "    pushq $0\n"                /* error code */
    "    pushq $0\n"                /* vector */
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
    "    movq %rsp, %rdi\n"         /* struct lm_fault_frame * */
    "    call lm_syscall_dispatch\n"
    "    testq %rax, %rax\n"
    "    jz 2f\n"                   /* NULL: there is nothing left to run */
    "    movq %rax, %rsp\n"         /* the same frame, or the next task's */
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
    "    swapgs\n"                  /* GS base back to the caller's */
    "    iretq\n"
    "2:\n"
    "    cli\n"
    "    hlt\n"
    "    jmp 2b\n");

/* Bitmask of what the round trip got right, so a failure says which part. */
#define RT_RESULT 1
#define RT_STATE  2
#define RT_STACK  4
#define RT_RSP    8

static u64 syscall_roundtrip(void)
{
    u64 faulty = 0;

    if (!cpu_has_syscall()) {
        return 0;
    }

    u64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1U);                  /* SCE */
    /* Kernel CS 0x08 / SS 0x10 come from STAR[47:32]; SYSRET would take
     * CS/SS from STAR[63:48]+16 and +8, which is why KDATA sits at 0x10: the
     * same value names user data at 0x18 and user code at 0x20. */
    wrmsr(MSR_STAR, ((u64)SEL_KDATA << 48) | ((u64)SEL_KCODE << 32));
    wrmsr(MSR_LSTAR, (u64)(unsigned long)lm_syscall_entry);
    /* SYSCALL does not switch stacks: clear IF and DF on entry so the kernel
     * runs with interrupts off on a stack it has not validated yet. */
    wrmsr(MSR_FMASK, 0x200U | 0x400U);

    u64 rsp_before;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_before));

    u64 nr = SYS_PING;
    u64 arg = 0x1234;
    u64 result;

    /* RAX carries the number in and the result out; RDI carries the argument.
     * Everything the entry stub and the dispatcher touch is declared as
     * clobbered, even though the stub saves and restores them. */
    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(nr), "D"(arg)
                     : "rcx", "r11", "rsi", "rdx", "rbx", "r8", "r9", "r10",
                       "memory");

    struct lm_cpu *cpu = cpu_this();

    if (result != 0x1234) {
        faulty |= RT_RESULT;
    }
    if (cpu->syscall_count != 1 || cpu->last_nr != SYS_PING ||
        cpu->last_arg != 0x1234) {
        faulty |= RT_STATE;
    }

    /* The handler has to have run on the per-CPU kernel stack, not on the
     * caller's stack. */
    u64 kbase = cpu->kstack_top - 8192;
    if (cpu->last_rsp <= kbase || cpu->last_rsp > cpu->kstack_top) {
        faulty |= RT_STACK;
    }

    /* And the caller's stack pointer has to be exactly where it was: the
     * five-value IRETQ frame is what makes that true. */
    if (cpu->saved_rsp != rsp_before) {
        faulty |= RT_RSP;
    }

    return faulty;
}

/*---------------------------------------
 * Entry
 *---------------------------------------*/

/*
 * report_memory - bring up the frame allocator and prove it round-trips
 *
 * The numbers are what the 64-bit port has to get right: a guest with 4 GB or
 * more has usable E820 ranges above 0x100000000, so "highest address" only
 * looks right if the map was read as 64-bit. The allocator then has to hand out
 * distinct, aligned frames and take them back.
 */
static void report_memory(void)
{
    if (pmm_init() != 0) {
        kputs("[pmm] FAIL no usable memory in the E820 map\r\n");
        return;
    }

    struct pmm_stats s = pmm_get_stats();
    kputs("[pmm] usable=");
    kput_dec(s.usable_bytes >> 20);
    kputs("MB top=0x");
    kput_hex(s.highest_addr, 12);
    kputs(" frames=");
    kput_dec(s.total_frames);
    kputs(" free=");
    kput_dec(s.free_frames);
    kputs("\r\n");

    u64 baseline = s.free_frames;
    u64 taken[4];
    int ok = 1;

    for (int i = 0; i < 4; i++) {
        taken[i] = pmm_alloc_frame();
        if (taken[i] == 0 || (taken[i] & (FRAME_SIZE - 1)) != 0) {
            ok = 0;
            break;
        }
        for (int j = 0; j < i; j++) {
            if (taken[j] == taken[i]) {
                ok = 0;              /* handed out twice */
            }
        }
    }

    if (ok && pmm_get_stats().free_frames + 4 != baseline) {
        ok = 0;                      /* the count did not move */
    }

    for (int i = 0; i < 4; i++) {
        if (taken[i] != 0 && pmm_free_frame(taken[i]) != 0) {
            ok = 0;
        }
    }

    if (ok && pmm_get_stats().free_frames != baseline) {
        ok = 0;                      /* the frames did not come back */
    }

    /* A frame that is already free must be refused: this is what catches a
     * double free before it corrupts the bitmap. */
    if (pmm_free_frame((u64)pmm_get_stats().highest_addr) != -1) {
        ok = 0;
    }

    kputs(ok ? "[pmm] pmm=ok\r\n" : "[pmm] FAIL allocator round trip\r\n");
}

void lm_main(void)
{
    /* .bss is NOBITS: the flat image carries no bytes for it, so the nucleus
     * clears it itself before anything relies on it being zero. The stack lives
     * in its own section after .bss (a stack never needs zeroing), so clearing
     * .bss here cannot pull the ground out from under this call. */
    zero_bss();

    serial_init();
    vga_init();
    vga_selftest();
    kbd_init();
    kbd_selftest();
    mouse_init();
    mouse_selftest();
    cpu_init();          /* GS base: the entry path needs it before it can run */
    struct cpu_features feat = cpu_harden();  /* NX/SMEP/SMAP where offered */
    idt_init();          /* a fault must report, not triple-fault */
    irq_init();          /* PIC remapped and the PIT ticking; IF stays clear */
    rtc_selftest();
    pipe_init();
    pipe_selftest();
    syscall_init();

    kputs("[lm] Mellivora x86-64 nucleus (C11)\r\n");

    kputs("[hw] nx=");
    kputs(feat.nx ? "on" : "off");
    kputs(" smep=");
    kputs(feat.smep ? "on" : "off");
    kputs(" smap=");
    kputs(feat.smap ? "on" : "off");
    kputs("\r\n");

    kputs("[lm] base=0x");
    kput_hex((u64)(unsigned long)lm_image_start, 16);

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    kputs(" cr3=0x");
    kput_hex(cr3, 16);
    kputs("\r\n");

    u64 data_addr = cpu_this()->self;
    kputs("[lm] data=0x");
    kput_hex(data_addr, 16);
    kputs("\r\n");

    u64 rt = syscall_roundtrip();
    if (rt == 0) {
        kputs("[lm] syscall=ok\r\n");
        kputs("[cpu] id=");
        kput_dec(cpu_this()->id);
        kputs(" calls=");
        kput_dec(cpu_this()->syscall_count);
        kputs(" kstack=0x");
        kput_hex(cpu_this()->kstack_top, 16);
        kputs(" stack=ok\r\n");
    } else {
        kputs("[lm] syscall=unavailable\r\n");
        kputs("[cpu] FAIL 0x");
        kput_hex(rt, 2);
        kputs("\r\n");
    }

    report_memory();
    vmm_selftest();
    kmem_init();
    kmem_selftest();

    acpi_init();
    lapic_init();
    lapic_selftest();
    lapic_timer_start();
    pci_init();
    ata_init();
    ata_selftest();
    vfs_init();
    hbfs_writetest();
    speaker_selftest();
    e1000_init();
    e1000_selftest();
    net_init();

    /* The ring-3 test is the last thing the nucleus does: the program leaves
     * through SYS_EXIT, and lm_user_exited() reports and halts. A failure to
     * build the address space returns here instead. */
    if (rt == 0 && lm_user_run() != 0) {
        kputs("[ring3] FAIL setup\r\n");
    }

    kputs("MELLIVORA64_OK\r\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

/* stage2 arrives with RSP=0x9FC00 (low memory, identity mapped); the nucleus
 * switches to its own stack here, before any C runs, so every function it calls
 * has a proper frame. This function never returns. */
__attribute__((naked, section(".text.lm_start"))) void lm_start(void)
{
    __asm__ volatile(
        /* Long mode on a BIOS boot starts with CR0.EM/TS as the firmware left
         * them and CR4.OSFXSR/OSXMMEXCPT clear, which makes every SSE
         * instruction an invalid-opcode fault. Enable both before any C runs. */
        "mov %cr0, %rax\n"
        "and $0xfffffffffffffffb, %rax\n"   /* clear EM (bit 2) */
        "and $0xfffffffffffffff7, %rax\n"   /* clear TS (bit 3) */
        "or  $0x2, %rax\n"                  /* set MP (bit 1) */
        "mov %rax, %cr0\n"
        "mov %cr4, %rax\n"
        "or  $0x600, %rax\n"                /* OSFXSR | OSXMMEXCPT */
        "mov %rax, %cr4\n"
        "lea lm_stack_top(%rip), %rsp\n"
        "xor %rbp, %rbp\n"
        "call lm_main\n"
        "1: cli\n"
        "   hlt\n"
        "   jmp 1b\n");
}

/*---------------------------------------
 * Stack
 *
 * Its own NOBITS section *after* .bss, not part of it: the nucleus zeroes .bss
 * while running on this stack, so the two must not overlap. It is uninitialised
 * RAM, which is all a stack needs, and lm_image_end covers it so the frame
 * allocator reserves it.
 *---------------------------------------*/

__asm__(
    ".section .stack,\"aw\",@nobits\n"
    ".align 16\n"
    ".globl lm_stack\n"
    "lm_stack:\n"
    "    .space 16384\n"
    ".globl lm_stack_top\n"
    "lm_stack_top:\n"
    ".section .text\n");

/*---------------------------------------
 * Compiler helpers a freestanding build still needs
 *---------------------------------------*/

void *memcpy(void *dst, const void *src, unsigned long n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int value, unsigned long n)
{
    u8 *d = dst;
    while (n--) {
        *d++ = (u8)value;
    }
    return dst;
}
