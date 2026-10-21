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
 *     handler (the shape the ported syscall path will use).
 *
 * Everything here is freestanding: no libc, no compiler runtime. The two
 * helpers a freestanding build can still need - memcpy/memset, which the
 * compiler may emit for a struct copy - are provided at the bottom.
 */

#include "lm.h"

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
 * The C half of a syscall. It runs on the CPU's own kernel stack, which the
 * entry stub below has already switched to, and reaches per-CPU state through
 * num, which is a placeholder for the real syscall table the port will have.
 * The two dummy calls exist so the entry path is exercised, not stubbed.
 */
#define LM_SYS_PING 1U
#define LM_SYS_WHOAMI 2U

u64 lm_syscall_dispatch(u64 nr, u64 arg)
{
    struct lm_cpu *cpu = cpu_this();
    u64 rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    cpu->syscall_count++;
    cpu->last_rsp = rsp;         /* must be inside the per-CPU kernel stack */
    cpu->last_nr = nr;
    cpu->last_arg = arg;

    switch (nr) {
    case LM_SYS_PING:
        return arg;
    case LM_SYS_WHOAMI:
        return cpu->id;
    default:
        return (u64)-1;
    }
}

/*
 * The entry path LSTAR points at. It must not assume anything about the
 * register state it arrives with, so it saves the caller's stack pointer in the
 * per-CPU block, switches to that CPU's kernel stack and only then touches
 * memory. (SYSCALL from ring 3 would also need SWAPGS and a return to a
 * user-controlled stack; this nucleus runs it from ring 0, where GS already
 * points at the per-CPU block.)
 */
void lm_syscall_entry(void);

__asm__(
    ".text\n"
    ".globl lm_syscall_entry\n"
    "lm_syscall_entry:\n"
    /* Save the caller's RSP, take this CPU's kernel stack. */
    "    movq %rsp, %gs:0x18\n"
    "    movq %gs:0x10, %rsp\n"
    /* IRETQ in 64-bit mode pops a five-value frame - RIP, CS, RFLAGS, RSP, SS -
     * even for a same-privilege return, so RSP is part of the frame or the
     * return lands with a garbage stack pointer. */
    "    pushq $0x10\n"             /* SS */
    "    pushq %gs:0x18\n"          /* caller's RSP, restored by IRETQ */
    "    pushq %r11\n"              /* RFLAGS, as SYSCALL left them */
    "    pushq $0x08\n"             /* CS */
    "    pushq %rcx\n"              /* return RIP, put there by SYSCALL */
    "    movq %rsp, %rbx\n"         /* RBX is callee-saved: the frame stays put */
    "    movq %rdi, %rsi\n"         /* arg */
    "    movq %rax, %rdi\n"         /* number */
    "    call lm_syscall_dispatch\n"
    "    movq %rbx, %rsp\n"         /* back to the frame */
    "    iretq\n");

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
    wrmsr(MSR_STAR, ((u64)0x08 << 32) | ((u64)0x08 << 48));
    wrmsr(MSR_LSTAR, (u64)(unsigned long)lm_syscall_entry);
    wrmsr(MSR_FMASK, 0);                         /* nothing masked */

    u64 rsp_before;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_before));

    u64 nr = LM_SYS_PING;
    u64 arg = 0x1234;
    u64 result;

    /* RAX carries the number in and the result out; RDI carries the argument.
     * Everything the entry stub and the dispatcher touch is declared as
     * clobbered, including RBX, which the stub uses to hold the frame. */
    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(nr), "D"(arg)
                     : "rcx", "r11", "rsi", "rdx", "rbx", "r8", "r9", "r10",
                       "memory");

    struct lm_cpu *cpu = cpu_this();

    if (result != 0x1234) {
        faulty |= RT_RESULT;
    }
    if (cpu->syscall_count != 1 || cpu->last_nr != LM_SYS_PING ||
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
    cpu_init();          /* GS base: the entry path needs it before it can run */

    kputs("[lm] Mellivora x86-64 nucleus (C11)\r\n");

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
