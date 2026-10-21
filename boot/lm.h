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
typedef long long s64;

#define NULL ((void *)0)

#define FRAME_SIZE 4096ULL   /* the page size everything here assumes */

/* The compiler may emit calls to these for a struct copy even freestanding. */
void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int value, unsigned long n);

/* Console: COM1, initialised by the nucleus before anything else prints. */
void kputc(char c);
void kputs(const char *s);
/* The next byte COM1 has received, or -1 when the input register is empty.
 * Polled: the console is a UART, and a task that wants a key spins with
 * SYS_YIELD between checks so the other tasks keep running. */
int kgetc(void);
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
#define VMM_PWT (1ULL << 3)     /* write-through */
#define VMM_PCD (1ULL << 4)     /* cache disabled: device memory */
#define VMM_NX (1ULL << 63)     /* no-execute, when EFER.NXE is on */
#define VMM_ADDR_MASK 0x000FFFFFFFFFF000ULL

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
/* A duplicate of a task's space: every user page gets its own frame with the
 * bytes copied. The kernel half and the heap window stay shared. */
u64 vmm_space_copy(u64 src);
/* Free that space's frames and its tables. */
void vmm_space_destroy(u64 space);
u64 vmm_translate_in(u64 space, u64 virt);

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

/* The nucleus's direct map of physical memory (stage2's PDH, 0..1 GB). It is in
 * every task's address space too, unlike the identity map, which is why kernel
 * code that runs during a syscall reaches low physical memory through this. */
#define LM_DIRECT_BASE 0xFFFFFFFF80000000ULL
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
#define SEL_UCODE32 0x38     /* compatibility-mode code segment, DPL 3 */
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
#define SYS_SEEK   13
#define SYS_STAT   14
#define SYS_READDIR 15
#define SYS_CHDIR  16
#define SYS_GETCWD 17
#define SYS_MKDIR  18
#define SYS_UNLINK 19
#define SYS_CREATE 20
#define SYS_PUTCHAR 21
#define SYS_GETCHAR 22
#define SYS_CLEAR  23
#define SYS_SLEEP  24
#define SYS_DATE   25
#define SYS_LOAD   26      /* load a whole file into a buffer */
#define SYS_MALLOC 27
#define SYS_FREE   28
#define SYS_PIPE_CREATE 29
#define SYS_PIPE_WRITE  30
#define SYS_PIPE_READ   31
#define SYS_PIPE_CLOSE  32
#define SYS_GETARGS     33
#define SYS_GETENV      34
#define SYS_FORK        37
#define SYS_WAIT        38
#define SYS_KILL        39
/* MOUSE returns x in RAX, y in RDX, and buttons|clicks<<8 in RCX. */
#define SYS_MOUSE       40
/* NETPING takes the target address in RDI (its four bytes in network order, so
 * 10.0.2.2 is 0x0A000202) and a timeout in RSI in milliseconds, and returns the
 * round trip in milliseconds or -1. The tree's SYS_PING (48, address in EBX)
 * reaches the same handler through the int 0x80 bridge. */
#define SYS_NETPING     41

/* The user program's addresses, the same in every task's space. */
#define USER_CODE_ADDR  0x40000000ULL
#define USER_STACK_ADDR 0x40001000ULL
#define USER_STACK_TOP  (USER_STACK_ADDR + FRAME_SIZE)

/* The program break starts here and grows in page steps; it lives in the same
 * PDPT slot as the user stack. */
#define USER_BRK_BASE 0x41000000ULL

void syscall_init(void);   /* prints the ABI and the table size */

/* INT 0x80 from a compatibility-mode task: the 32-bit tree's numbering and
 * register convention, translated onto the LP64 handlers. */
struct lm_fault_frame *lm_int80_dispatch(struct lm_fault_frame *frame);
#define INT80_VECTOR 0x80

/*---------------------------------------
 * PCI and block storage
 *---------------------------------------*/

/* One enumerated function. Drivers read the class to find their device and the
 * vendor/id pair to know which one it is. */
struct pci_device {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor;
    u16 id;
    u8 class_code;
    u8 subclass;
};

/* Enumerate bus 0..255 and report the count; returns it. */
int pci_init(void);
int pci_count(void);
const struct pci_device *pci_find(u8 class_code, u8 subclass);

/* Configuration space, for a driver that has to enable its device and read its
 * BARs. Offset is the byte offset of a 32-bit register. */
u32 pci_config_read32(u8 bus, u8 device, u8 function, u8 offset);
void pci_config_write32(u8 bus, u8 device, u8 function, u8 offset, u32 value);

/* HBFS: the on-disk format populate.py writes. */
struct hbfs_entry {
    u32 type;
    u32 size;
    u32 start;      /* data block number, relative to the data area */
    u32 blocks;
};

#define HBFS_FTYPE_FREE 0
#define HBFS_FTYPE_FILE 1
#define HBFS_FTYPE_DIR  2
#define HBFS_FTYPE_EXEC 3

int hbfs_init(void);
int hbfs_lookup(const char *name, struct hbfs_entry *out);
int hbfs_read_at(const struct hbfs_entry *entry, u64 offset, void *buffer,
                 u64 count);

/* Directories. The root has no on-disk entry of its own, so `hbfs_root`
 * synthesises one that `hbfs_readdir`/`hbfs_lookup_in` recognise. */
void hbfs_root(struct hbfs_entry *out);
/* 1 = entry written to *out, 0 = free slot (nothing written), -1 = past the
 * end of the directory. `name` receives the NUL-terminated name. */
int hbfs_readdir(const struct hbfs_entry *dir, u32 index,
                 struct hbfs_entry *out, char *name, u32 name_bytes);
int hbfs_lookup_in(const struct hbfs_entry *dir, const char *name,
                   struct hbfs_entry *out);

/* Path resolution relative to the (global) working directory: absolute paths
 * start at the root, ".." walks back to it, "." is skipped. Subdirectories are
 * one level deep until MKDIR exists, which is what ".." reflects. */
int hbfs_resolve(const char *path, struct hbfs_entry *out);
int hbfs_chdir(const char *path);
const char *hbfs_getcwd(void);

/* Writing. A file is created or truncated to exactly `size` bytes; a directory
 * takes HBFS_SUBDIR_BLOCKS zeroed blocks. Both are idempotent, so re-running a
 * program that creates its own files works. */
int hbfs_create(const char *path, u32 type, const void *data, u32 size);
int hbfs_mkdir(const char *path);
/* Free a file's blocks and clear its entry; not a directory. */
int hbfs_remove(const char *path);
/* Write `size` bytes at `offset` into an existing file, growing it if the write
 * runs past its end. Not a directory, and not a create: that is hbfs_create. */
int hbfs_write(const char *path, u64 offset, const void *data, u32 size);

/* Free blocks left in the volume, for the directory demo's report. */
u32 hbfs_free_blocks(void);

/* Grow a file in place and grow one that has to move, then clean up. */
int hbfs_writetest(void);

/* A thin VFS in front of it: open by name, then read by handle. A handle
 * opened with `write` set may also be written, anywhere in the file. */
int vfs_init(void);
int vfs_open(const char *name);        /* handle, or -1 */
int vfs_open_mode(const char *name, int write);
int vfs_close(int handle);
u64 vfs_size(int handle);
int vfs_read(int handle, void *buffer, u64 count);
int vfs_write(int handle, const void *data, u64 count);
/* whence: 0 from the start, 1 from the position, 2 from the end. */
int vfs_seek(int handle, s64 offset, int whence);

/* Name and metadata for one directory entry, shared by STAT and READDIR. */
#define VFS_NAME_BYTES 256
struct vfs_stat {
    u32 type;
    u32 size;
    u32 blocks;
    u32 start;
    char name[VFS_NAME_BYTES];
};

int vfs_stat(const char *path, struct vfs_stat *out);
/* 1 = entry, 0 = free slot, -1 = past the end of the directory. */
int vfs_readdir(const char *path, u32 index, struct vfs_stat *out);
int vfs_chdir(const char *path);
const char *vfs_getcwd(void);
/* Create or overwrite `path` with `size` bytes from `data`. */
int vfs_create(const char *path, u32 type, const void *data, u32 size);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
u32 vfs_free_blocks(void);
void vfs_release_task(u64 owner);

/* The PS/2 keyboard, polled. An auxiliary-port byte is handed to the mouse
 * rather than dropped, so a packet is never split by a keystroke. */
void kbd_init(void);
int kbd_getc(void);          /* the next character, or -1 */
int kbd_selftest(void);

/* The PS/2 mouse, on the same controller and polled from the timer tick. Its
 * coordinates are text cells. */
void mouse_init(void);
int mouse_selftest(void);
void mouse_feed(u8 byte);    /* one byte from the auxiliary port */
void mouse_poll(void);       /* drain the port, leaving keystrokes alone */
int mouse_x(void);
int mouse_y(void);
int mouse_buttons(void);     /* bit 0 left, bit 1 right, bit 2 middle */
int mouse_clicks(void);      /* presses since the last call, then cleared */

/* The VGA text console: 80x25 at 0xB8000, the second half of kputc. */
void vga_init(void);
void vga_putc(char c);
void vga_clear(void);
void vga_set_color(u8 attribute);
void vga_set_cursor(u32 column, u32 row);
int vga_selftest(void);
int vga_cursor(void);

/* Pipes: a ring buffer with a reader and a writer. */
void pipe_init(void);
int pipe_create(void);                        /* id, or -1 */
int pipe_write(int id, const void *data, u32 len);
int pipe_read(int id, void *buffer, u32 max);
int pipe_close(int id);
int pipe_selftest(void);

/* The CMOS real-time clock, read the way the 32-bit tree reads it. */
struct rtc_time {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u8 year;            /* two digits, as the chip stores it */
    u32 full_year;      /* century * 100 + year, for anything that prints it */
};

int rtc_read(struct rtc_time *t);   /* 0 or -1 when the chip answers nonsense */
int rtc_selftest(void);

/* Polling ATA PIO on the primary channel. */
int ata_init(void);
int ata_present(void);
u64 ata_sector_count(void);
int ata_read(u64 lba, void *buffer);
int ata_write(u64 lba, const void *buffer);
int ata_selftest(void);

/*---------------------------------------
 * The network adapter
 *
 * The 8254x family, polled: the rings live in frames this driver allocates and
 * the register block is mapped cache-disabled at boot. send() returns once the
 * descriptor came back, recv() never blocks.
 *---------------------------------------*/
int e1000_init(void);
int e1000_selftest(void);
int e1000_present(void);
int e1000_send(const void *frame, u32 length);     /* 0 sent, -1 failed */
int e1000_recv(void *buffer, u32 *length);         /* 1 got, 0 none, -1 no card */
const u8 *e1000_mac(void);

/*---------------------------------------
 * The network layer: ARP and ICMP echo, which is enough to prove the adapter and
 * to answer "is anything out there?". The addresses are the ones a user-mode
 * network hands out.
 *---------------------------------------*/
int net_init(void);                                  /* resolve the gateway */
int net_ready(void);
int net_ping(const u8 *ip, u32 timeout_ticks);      /* round trip in ms, or -1 */
const u8 *net_gateway_mac(void);
const u8 *net_local_ip(void);

/*---------------------------------------
 * Scheduler
 *---------------------------------------*/

struct lm_task;

/* Cache the task and kernel-stack objects; called before any task exists. */
void sched_init(void);

/* A compatibility task's exit code is whatever its program leaves in EBX.
 * TASK_ANY_EXIT accepts anything (the tree's programs do not set one). */
#define TASK_ANY_EXIT (~0ULL)
#define TASK_NO_PARENT (~0ULL)

/* Create a task that enters `entry` on `user_stack` in address space `space`
 * (a vmm_space_create() result). `compat` enters it in 32-bit mode through the
 * compatibility code segment. `expect_exit` is the code the task must leave
 * (TASK_ANY_EXIT to accept any), and `self_report` marks the programs that end
 * by passing PING/WHOAMI in RSI/RDX and exiting with their own id, which the
 * report then checks against the CPU. 0 on success. */
int sched_create(u64 entry, u64 user_stack, u64 space, int compat,
                 u64 expect_exit, int self_report);

/* Switch to the first runnable task and enter ring 3. Never returns. */
void sched_start(void) __attribute__((noreturn));

struct lm_task *sched_current(void);
u64 sched_current_id(void);   /* 0 when no task is running */

/* Per-task syscall counting, called from the dispatcher. */
void sched_note_syscall(void);

/* A critical section that must not be preempted - a filesystem update runs over
 * several disk writes that share the sector bounce buffer, so another task
 * reading the volume halfway through would see a torn directory. The tick keeps
 * counting but defers the switch while the depth is non-zero. Nestable. */
void sched_lock(void);
void sched_unlock(void);
int sched_locked(void);

/* Timer tick: round-robin, returning the frame of the task to run next. */
struct lm_fault_frame *sched_tick(struct lm_fault_frame *frame);

/* SYS_YIELD: give the CPU up voluntarily. Same switch, no tick accounting. */
struct lm_fault_frame *sched_yield(struct lm_fault_frame *frame);

/* The running task's program break. */
u64 sched_brk(void);
void sched_set_brk(u64 value);

/* The task's heap, for MALLOC/FREE: a bump pointer that walks the break, and the
 * head of a free list threaded through the blocks themselves. */
/* The command line the task was exec'd with, or "" when it had none. */
const char *sched_args(void);
void sched_set_args(const char *value);

u64 sched_heap(void);
void sched_set_heap(u64 value);
u64 sched_free_list(void);
void sched_set_free_list(u64 value);

/* SYS_EXIT: retire the running task; returns the frame to run next, or NULL. */
struct lm_fault_frame *sched_exit(struct lm_fault_frame *frame);
/* Retire a task that faulted: same path as exit, with the vector in the code. */
struct lm_fault_frame *sched_faulted(struct lm_fault_frame *frame);

/* FORK: a copy of the calling task - its space, its saved frame, its heap and its
 * command line - that resumes in the same place with 0 in RAX. Returns the child's
 * id, or -1. */
s64 sched_fork(struct lm_fault_frame *parent_frame);
/* WAITPID: 0 when the child has exited and been reaped (*code is its exit code),
 * 1 while it still runs, -1 if pid is not this task's child. Reaping frees the
 * slot, the kernel stack and the address space. */
int sched_wait(s64 pid, u64 *code);
/* KILL: retire another task now; its parent's waitpid reaps it. 0 or -1. */
int sched_kill(s64 pid, u64 signal);

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
