/*
 * lm.h - shared declarations for the 64-bit nucleus (roadmap P2)
 *
 * Freestanding C11: no headers, no libc. Everything the nucleus and the
 * subsystems it grows use is declared here.
 */
#ifndef LM_H
#define LM_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define NULL ((void *)0)

#define FRAME_SIZE 4096ULL   /* the page size everything here assumes */

/* The compiler may emit calls to these for a struct copy even freestanding. */
void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int value, unsigned long n);

/* Console: COM1, initialised by the nucleus before anything else prints. */
void kputc(char c);
void kputs(const char *s);
void kput_hex(u64 value, int digits);
void kput_dec(u64 value);

/*---------------------------------------
 * Per-CPU state
 *
 * One block per processor, reachable through the KERNEL_GS_BASE MSR and made
 * visible by SWAPGS at the top of the entry path: the entry stub needs
 * somewhere to keep its own stack pointer without assuming anything about the
 * register state the caller had. Fields are u64 and in this order because the
 * entry stub and the ring-3 exit path address them by byte offset.
 *
 * While ring 0 code runs, GS base is the value the user program sees (0 here)
 * and KERNEL_GS_BASE holds this block; SWAPGS in the entry stub makes the block
 * current, and the exit paths swap it back. That one arrangement serves a
 * SYSCALL taken from ring 0 and from ring 3 alike.
 *---------------------------------------*/

struct lm_cpu {
    u64 self;             /* 0x00  address of this block */
    u64 id;               /* 0x08  processor index */
    u64 kstack_top;       /* 0x10  kernel stack this CPU enters on */
    u64 saved_rsp;        /* 0x18  caller's stack pointer, saved by the entry stub */
    u64 syscall_count;    /* 0x20  calls taken */
    u64 last_rsp;         /* 0x28  stack the handler ran on (must be the kernel one) */
    u64 last_nr;          /* 0x30  last syscall number */
    u64 last_arg;         /* 0x38  last syscall argument */
    u64 user_mode;        /* 0x40  non-zero while the CPU runs a user program */
    u64 user_calls;       /* 0x48  calls taken from ring 3 */
    u64 exit_code;        /* 0x50  argument of the last SYS_EXIT */
    u64 exit_arg;         /* 0x58  second word SYS_EXIT was given */
    u64 last_user_rip;    /* 0x60  user instruction that entered the kernel */
    u64 last_user_rsp;    /* 0x68  user stack pointer it entered with */
    u64 kstack_base;      /* 0x70  bottom of kstack_top's stack, for the fault check */
    u64 ticks;            /* 0x78  timer interrupts taken */
    u64 user_ticks;       /* 0x80  ... of those, taken while ring 3 ran */
    u64 tick_stack;       /* 0x88  ... whose handler ran on the TSS stack */
};

#define CPU_KSTACK_TOP_OFF 0x10
#define CPU_SAVED_RSP_OFF  0x18
#define CPU_EXIT_CODE_OFF  0x50

/* The processor this nucleus is running on. */
struct lm_cpu *cpu_this(void);
int cpu_init(void);

/* What the CPU offers beyond the baseline. */
struct cpu_features {
    int nx;
    int smep;
    int smap;
};

/* Enable NX (EFER.NXE), SMEP and SMAP where CPUID says they exist, and report
 * what was turned on. Must run before the first NX mapping is installed. */
struct cpu_features cpu_harden(void);

/* 1 once EFER.NXE is set, so a caller may put VMM_NX in a mapping. */
int cpu_nx_enabled(void);

/* 1 once CR4.SMAP is set: STAC/CLAC are then legal and user memory is
 * unreachable without them. */
int cpu_smap_enabled(void);

/*---------------------------------------
 * Interrupt frame
 *
 * One layout for both entry paths - SYSCALL and the exception/IRQ stub - so a
 * task's saved frame can be resumed by either, and a task that exits can hand
 * over to a frame the timer saved. In memory order: the registers the stub
 * pushed last are nearest the stack pointer, then the vector and error code it
 * pushed, then the five values IRETQ pops.
 *
 * `rsp`/`ss` exist only when the frame came from ring 3; a same-privilege frame
 * has neither and everything the CPU pushed shifts down by 16 bytes. Code that
 * reads them checks `(cs & 3) == 3` first.
 *---------------------------------------*/

struct lm_fault_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vec, err;
    u64 rip, cs, rflags, rsp, ss;
};

/*---------------------------------------
 * Virtual memory
 *
 * A four-level walk over the tables stage2 built, with 4 KB leaves. Shared
 * with the user path, so `VMM_U` has to be propagated into the intermediate
 * entries a user leaf sits under; a kernel leaf under the same tables keeps
 * access because the check is at every level, leaf included.
 *---------------------------------------*/

#define VMM_P  0x1ULL
#define VMM_W  0x2ULL
#define VMM_U  0x4ULL
#define VMM_PS 0x80ULL
#define VMM_NX (1ULL << 63)     /* no-execute, when EFER.NXE is on */

/* The kernel heap window: a 16 MB range of its own, mapped on demand. */
#define KHEAP_BASE       0xFFFFC00000000000ULL
#define KHEAP_PAGES      4096ULL
#define KHEAP_PML4_INDEX ((KHEAP_BASE >> 39) & 511)

/* The kernel's own PML4 (the higher half at 0xFFFFFFFF80000000). */
#define KERNEL_PML4_INDEX 511ULL

/* Leaf entry for `virt` in the current space, or 0 when it is not mapped. */
u64 vmm_flags(u64 virt);

/* Map one 4 KB page in the current space. `flags` are VMM_W/VMM_U; present is
 * implied. Returns -1 if the address is already mapped, the frame cannot be
 * reached, or a large page covers the address. Tables come from the PMM. */
int vmm_map(u64 virt, u64 phys, u64 flags);

/* The same, in a PML4 the caller names (a task's space, or one being built). */
int vmm_map_in(u64 space, u64 virt, u64 phys, u64 flags);

/* CR3's physical base: the space the CPU is walking. */
u64 vmm_current_space(void);

/* Load CR3 (flushes the TLB - there is no PCID yet). */
void vmm_space_switch(u64 space);

/* A new PML4 with the kernel's higher half and heap entries shared into it.
 * Returns its physical address, or 0. */
u64 vmm_space_create(void);

/* Drop a mapping. Returns -1 when there was none. */
int vmm_unmap(u64 virt);

/* Physical address `virt` resolves to, or 0 when it is not mapped. Large
 * pages are resolved, so the identity map answers too. */
u64 vmm_translate(u64 virt);

/* 1 only when every level on the way to `virt` carries U/S - the check a
 * copy_from_user would make. */
int vmm_user_mapped(u64 virt);

/* 1 when the MMU refuses a read from `virt`, using the fault handler's fix-up
 * path: the probe load is abandoned and this returns instead of dying. */
u64 vmm_probe(u64 virt);

/* The nucleus's direct map of physical memory (stage2's PDH, 0..1 GB). */
void *vmm_phys(u64 phys);

/* Map, translate, unmap a page and prove the MMU agrees. Prints vmm=ok. */
int vmm_selftest(void);

/*---------------------------------------
 * Kernel slab allocator
 *
 * Fixed-size caches; a cache takes a page only when its free list is empty and
 * never gives one back. Objects are zeroed on allocation, so a recycled slot
 * cannot leak the previous owner's contents.
 *---------------------------------------*/

struct kmem_stats {
    u64 caches;
    u64 slabs;
    u64 objects;   /* slots across every slab */
    u64 used;      /* slots currently handed out */
};

void kmem_init(void);
int kmem_cache(u64 size);            /* id, or -1 when unsupported/full */
void *kmem_alloc(int id);            /* zeroed object, or NULL */
void kmem_free(int id, void *obj);
struct kmem_stats kmem_get_stats(void);
int kmem_selftest(void);

/*---------------------------------------
 * Timers and interrupts
 *---------------------------------------*/

#define LM_IRQ_FIRST 32
#define LM_IRQ_COUNT 16
#define LM_TIMER_HZ  100

/* Remap the PIC, start the PIT and unmask IRQ0. Interrupts stay off in the
 * kernel: only the RFLAGS of a ring-3 context has IF set. */
void irq_init(void);

/* Vector 32..47, called from the common entry path; returns the frame to
 * resume, which a tick may have changed to another task's. */
struct lm_fault_frame *lm_irq_dispatch(struct lm_fault_frame *frame);

/* The TSS stack a ring-3 fault or tick will land on. Updated on every switch. */
void lm_tss_set_rsp0(u64 top);

/*---------------------------------------
 * IDT and exceptions
 *---------------------------------------*/

/* Install the long-mode IDT (vectors 0..21; the rest stay absent). */
void idt_init(void);

/* Runs on the stack the CPU selected. Returns the frame to IRETQ into - the same
 * one to resume, another task's after a switch - or NULL to stop. */
struct lm_fault_frame *lm_fault_dispatch(struct lm_fault_frame *frame);

/*
 * Kernel-mode fix-up: while `lm_fault_fixup_cr2` is non-zero, a ring-0 #PF on
 * exactly that address resumes at `lm_fault_fixup_rip` instead of reporting.
 * vmm_probe() arms it around a single load; nothing else may.
 */
extern u64 lm_fault_fixup_cr2;
extern u64 lm_fault_fixup_rip;

u64 lm_probe_read(u64 virt);
void lm_probe_faulted(void);

/* GDT selectors. The order matters twice over: SYSCALL loads CS/SS from
 * STAR[47:32] and STAR[47:32]+8, and SYSRET loads CS/SS from
 * STAR[63:48]+16 and STAR[63:48]+8, so user data and user code must sit at
 * KDATA+8 and KDATA+16 for the SYSRET form to agree with the IRETQ one. */
#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UDATA 0x18
#define SEL_UCODE 0x20
#define SEL_TSS   0x28
#define RPL_USER  3

/*---------------------------------------
 * Ring-3 path
 *---------------------------------------*/

/*
 * Map the user program and its stack, load the user GDT and TSS, and drop to
 * ring 3. Returns -1 if the address space could not be built; on success it
 * never returns - the program leaves through SYS_EXIT, which reports and halts
 * in lm_user_exited().
 */
int lm_user_run(void);

/* Enter ring 3 at `entry` with the given stack and RFLAGS. Never returns. */
void lm_user_enter(u64 entry, u64 user_rsp, u64 rflags) __attribute__((noreturn));

/* Pop a task's saved frame and IRETQ into it. Never returns. */
void lm_enter_task(u64 rsp) __attribute__((noreturn));

/*---------------------------------------
 * Syscalls
 *
 * The LP64 shape the 32-bit tree's int 0x80 path will be bridged to: number in
 * RAX, arguments in RDI, RSI, RDX, R10, R8, R9, result in RAX. Handlers get the
 * whole frame, so they can read any argument register and, for exit and yield,
 * return the frame to run next.
 *---------------------------------------*/

#define SYS_PING   1
#define SYS_WHOAMI 2
#define SYS_EXIT   3
#define SYS_TICKS  4
#define SYS_GETTID 5
#define SYS_WRITE  6
#define SYS_YIELD  7
#define SYS_BRK    8
#define SYS_OPEN   9
#define SYS_READ   10
#define SYS_CLOSE  11
#define SYS_EXEC   12

/* The user program's addresses, the same in every task's space. */
#define USER_CODE_ADDR  0x40000000ULL
#define USER_STACK_ADDR 0x40001000ULL
#define USER_STACK_TOP  (USER_STACK_ADDR + FRAME_SIZE)

/* The program break starts here and grows in page steps; it lives in the same
 * PDPT slot as the user stack. */
#define USER_BRK_BASE 0x41000000ULL

void syscall_init(void);   /* prints the ABI and the table size */

/*---------------------------------------
 * PCI and block storage
 *---------------------------------------*/

struct pci_device;

/* Enumerate bus 0..255 and report the count; returns it. */
int pci_init(void);
int pci_count(void);
const struct pci_device *pci_find(u8 class_code, u8 subclass);

/* HBFS: the on-disk format populate.py writes. */
struct hbfs_entry {
    u32 type;
    u32 size;
    u32 start;      /* data block number, relative to the data area */
    u32 blocks;
};

int hbfs_init(void);
int hbfs_lookup(const char *name, struct hbfs_entry *out);
int hbfs_read_at(const struct hbfs_entry *entry, u64 offset, void *buffer,
                 u64 count);

/* A thin read-only VFS in front of it: open by name, then read by handle. */
int vfs_init(void);
int vfs_open(const char *name);        /* handle, or -1 */
int vfs_close(int handle);
u64 vfs_size(int handle);
int vfs_read(int handle, void *buffer, u64 count);

/* Polling ATA PIO on the primary channel. */
int ata_init(void);
int ata_present(void);
u64 ata_sector_count(void);
int ata_read(u64 lba, void *buffer);
int ata_write(u64 lba, const void *buffer);
int ata_selftest(void);

/*---------------------------------------
 * Scheduler
 *---------------------------------------*/

struct lm_task;

/* Cache the task and kernel-stack objects; called before any task exists. */
void sched_init(void);

/* Create a task that enters `entry` on `user_stack` in address space `space`
 * (a vmm_space_create() result). 0 on success. */
int sched_create(u64 entry, u64 user_stack, u64 space);

/* Switch to the first runnable task and enter ring 3. Never returns. */
void sched_start(void) __attribute__((noreturn));

struct lm_task *sched_current(void);
u64 sched_current_id(void);   /* 0 when no task is running */

/* Per-task syscall counting, called from the dispatcher. */
void sched_note_syscall(void);

/* Timer tick: round-robin, returning the frame of the task to run next. */
struct lm_fault_frame *sched_tick(struct lm_fault_frame *frame);

/* SYS_YIELD: give the CPU up voluntarily. Same switch, no tick accounting. */
struct lm_fault_frame *sched_yield(struct lm_fault_frame *frame);

/* The running task's program break. */
u64 sched_brk(void);
void sched_set_brk(u64 value);

/* SYS_EXIT: retire the running task; returns the frame to run next, or NULL. */
struct lm_fault_frame *sched_exit(struct lm_fault_frame *frame);

/*---------------------------------------
 * Physical memory manager
 *---------------------------------------*/

struct pmm_stats {
    u64 usable_bytes;  /* usable RAM below the managed ceiling */
    u64 highest_addr;  /* end of the highest usable range */
    u64 free_frames;
    u64 total_frames;  /* frames the bitmap covers */
};

/*
 * Build the frame bitmap from the E820 map stage2 left at 0x504/0x508.
 * Returns 0 on success, -1 when there is no usable memory or the map is not
 * where stage2 says it is.
 */
int pmm_init(void);
struct pmm_stats pmm_get_stats(void);

/* Physical address of a free 4 KB frame, or 0 when memory is exhausted. */
u64 pmm_alloc_frame(void);

/* Give a frame back. Returns 0 when it was ours, -1 when it was not (a double
 * free or an address outside the managed range). */
int pmm_free_frame(u64 addr);

#endif /* LM_H */
