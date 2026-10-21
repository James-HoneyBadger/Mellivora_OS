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

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

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

static void putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* wait for the transmit register */
    }
    outb(COM1, (u8)c);
}

static void puts(const char *s)
{
    while (*s != '\0') {
        putc(*s++);
    }
}

static void put_hex(u64 value, int digits)
{
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        u8 nibble = (u8)((value >> shift) & 0xF);
        putc((char)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10));
    }
}

static void put_hex64(u64 value)
{
    put_hex(value, 16);
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

static volatile u64 syscall_hits;

/*
 * The handler has to come back to ring 0 at the instruction after `syscall`,
 * so it rebuilds the frame IRETQ expects out of RCX (return RIP) and R11
 * (flags). A ported kernel would instead switch to a per-CPU kernel stack; the
 * MSR programming and the entry path below are the same.
 */
void lm_syscall_entry(void);

__asm__(
    ".text\n"
    ".globl lm_syscall_entry\n"
    "lm_syscall_entry:\n"
    "    lock incq syscall_hits(%rip)\n"
    /* Same-privilege IRETQ pops RIP, CS and RFLAGS only - it does not pop
     * RSP/SS - so the frame is exactly those three. */
    "    pushq %r11\n"         /* RFLAGS, as SYSCALL left it */
    "    pushq $0x08\n"        /* CS: the 64-bit kernel code selector */
    "    pushq %rcx\n"         /* return RIP, put there by SYSCALL */
    "    iretq\n");

static int syscall_roundtrip(void)
{
    if (!cpu_has_syscall()) {
        return 0;
    }

    u64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1U);                  /* SCE */
    wrmsr(MSR_STAR, ((u64)0x08 << 32) | ((u64)0x08 << 48));
    wrmsr(MSR_LSTAR, (u64)(unsigned long)lm_syscall_entry);
    wrmsr(MSR_FMASK, 0);                         /* nothing masked */

    __asm__ volatile("syscall" : : : "rcx", "r11", "memory");
    return syscall_hits == 1;
}

/*---------------------------------------
 * Entry
 *---------------------------------------*/

void lm_main(void)
{
    /* .bss is NOBITS: the flat image carries no bytes for it, so the nucleus
     * has to clear it itself before anything relies on it being zero. This runs
     * on the stack stage2 left (low memory, identity mapped), because the
     * nucleus stack is *inside* .bss and cannot be used until it is clear. */
    zero_bss();

    __asm__ volatile("lea lm_stack_top(%%rip), %%rax\n"
                     "mov %%rax, %%rsp\n"
                     "xor %%rbp, %%rbp\n" : : : "rax");

    serial_init();

    puts("[lm] Mellivora x86-64 nucleus (C11)\r\n");

    puts("[lm] base=0x");
    put_hex64((u64)(unsigned long)lm_image_start);

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    puts(" cr3=0x");
    put_hex64(cr3);
    puts("\r\n");

    u64 stack_marker = (u64)(unsigned long)&syscall_hits;
    puts("[lm] data=0x");
    put_hex64(stack_marker);
    puts("\r\n");

    if (syscall_roundtrip()) {
        puts("[lm] syscall=ok\r\n");
    } else {
        puts("[lm] syscall=unavailable\r\n");
    }

    puts("MELLIVORA64_OK\r\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

/* stage2 arrives with RSP=0x9FC00 (low memory, identity mapped); lm_main moves
 * to the nucleus stack once .bss is clear. This function never returns. */
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
        "xor %rbp, %rbp\n"
        "call lm_main\n"
        "1: cli\n"
        "   hlt\n"
        "   jmp 1b\n");
}

/*---------------------------------------
 * Stack (in .bss, so it is mapped by the higher-half mapping and zeroed)
 *---------------------------------------*/

__asm__(
    ".bss\n"
    ".align 16\n"
    ".globl lm_stack\n"
    "lm_stack:\n"
    "    .space 16384\n"
    ".globl lm_stack_top\n"
    "lm_stack_top:\n");

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
