/*
 * lm_vmm.c - the 64-bit virtual memory manager
 *
 * Roadmap P2. stage2 builds a usable address space (identity map, higher half,
 * 1 GB of 2 MB pages in each) and the frame allocator hands out the pages; this
 * is the layer between them: a four-level walk that can create the tables a
 * mapping needs, a 4 KB map/unmap, and the two questions the rest of the port
 * asks - "where does this virtual address point" and "may ring 3 touch it".
 *
 * Two boundaries are deliberate and documented rather than hidden:
 *
 *   - tables and frames are reached through the direct map stage2 installed
 *     (physical 0..1 GB at 0xFFFFFFFF80000000), so a frame above 1 GB cannot be
 *     written yet and vmm_map() fails instead of corrupting something. The 8 GB
 *     guest is fine because the allocator walks from low memory; a real direct
 *     map of all RAM is part of the next step.
 *   - vmm_unmap() clears the leaf and nothing else. Empty tables are left for a
 *     later reclaimer; freeing them without a reference count is how a mapping
 *     ends up pointing at a page table that was handed out twice.
 */

#include "lm.h"

/* stage2's PDH: physical 0..1 GB, mapped at DIRECT_BASE. */
#define DIRECT_BASE  0xFFFFFFFF80000000ULL
#define DIRECT_LIMIT 0x40000000ULL

#define PTE_ADDR 0x000FFFFFFFFFF000ULL   /* physical address field */

/* Somewhere no other part of the port maps: PML4[254], canonical, and far from
 * the image, the identity map and the user area. */
#define TEST_VA 0x00007F0000000000ULL

void *vmm_phys(u64 phys)
{
    if (phys == 0 || phys >= DIRECT_LIMIT) {
        return NULL;
    }
    return (void *)(unsigned long)(DIRECT_BASE + phys);
}

static void tlb_flush(u64 virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/*
 * Walk to the leaf entry for `virt`. With `create`, missing intermediate tables
 * are allocated from the PMM and zeroed; `flags` decides whether a table that
 * had to be created - and any that already existed on the way - carries U/S.
 *
 * Returns NULL when a level is missing (lookup), when a large page covers the
 * address, or when the frame for a new table cannot be reached.
 */
static u64 *vmm_leaf(u64 virt, int create, u64 flags)
{
    static const int shift[3] = {39, 30, 21};

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    u64 *table = vmm_phys(cr3 & PTE_ADDR);
    if (table == NULL) {
        return NULL;
    }

    for (int level = 0; level < 3; level++) {
        u64 *entry = &table[(virt >> shift[level]) & 511];

        if ((*entry & VMM_P) != 0) {
            if ((*entry & VMM_PS) != 0) {
                return NULL;            /* a 2 MB or 1 GB page is in the way */
            }
            if ((flags & VMM_U) != 0) {
                *entry |= VMM_U;        /* the leaf decides, but it needs this */
            }
        } else {
            if (!create) {
                return NULL;
            }
            u64 frame = pmm_alloc_frame();
            void *fresh = vmm_phys(frame);
            if (fresh == NULL) {
                if (frame != 0) {
                    pmm_free_frame(frame);
                }
                return NULL;
            }
            memset(fresh, 0, FRAME_SIZE);
            *entry = frame | VMM_P | VMM_W | (flags & VMM_U);
        }

        table = vmm_phys(*entry & PTE_ADDR);
        if (table == NULL) {
            return NULL;
        }
    }

    return &table[(virt >> 12) & 511];
}

int vmm_map(u64 virt, u64 phys, u64 flags)
{
    if ((virt & (FRAME_SIZE - 1)) != 0 || (phys & (FRAME_SIZE - 1)) != 0 ||
        phys == 0) {
        return -1;
    }

    u64 *leaf = vmm_leaf(virt, 1, flags);
    if (leaf == NULL || (*leaf & VMM_P) != 0) {
        return -1;
    }

    *leaf = phys | VMM_P | (flags & (VMM_W | VMM_U));
    tlb_flush(virt);
    return 0;
}

int vmm_unmap(u64 virt)
{
    u64 *leaf = vmm_leaf(virt, 0, 0);
    if (leaf == NULL || (*leaf & VMM_P) == 0) {
        return -1;
    }

    *leaf = 0;
    tlb_flush(virt);
    return 0;
}

u64 vmm_translate(u64 virt)
{
    static const int shift[4] = {39, 30, 21, 12};

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    u64 *table = vmm_phys(cr3 & PTE_ADDR);
    if (table == NULL) {
        return 0;
    }

    for (int level = 0; level < 3; level++) {
        u64 entry = table[(virt >> shift[level]) & 511];
        if ((entry & VMM_P) == 0) {
            return 0;
        }
        if ((entry & VMM_PS) != 0) {
            /* A large page: 1 GB at the PDPT, 2 MB at the PD. */
            u64 size = 1ULL << shift[level];
            u64 base = entry & PTE_ADDR & ~(size - 1);
            return base + (virt & (size - 1));
        }
        table = vmm_phys(entry & PTE_ADDR);
        if (table == NULL) {
            return 0;
        }
    }

    u64 leaf = table[(virt >> 12) & 511];
    if ((leaf & VMM_P) == 0) {
        return 0;
    }
    return (leaf & PTE_ADDR) + (virt & (FRAME_SIZE - 1));
}

int vmm_user_mapped(u64 virt)
{
    static const int shift[3] = {39, 30, 21};

    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    u64 *table = vmm_phys(cr3 & PTE_ADDR);
    if (table == NULL) {
        return 0;
    }

    for (int level = 0; level < 3; level++) {
        u64 entry = table[(virt >> shift[level]) & 511];
        if ((entry & (VMM_P | VMM_U)) != (VMM_P | VMM_U)) {
            return 0;
        }
        if ((entry & VMM_PS) != 0) {
            return (entry & VMM_U) != 0;    /* a large page is gated by its own bit */
        }
        table = vmm_phys(entry & PTE_ADDR);
        if (table == NULL) {
            return 0;
        }
    }

    u64 leaf = table[(virt >> 12) & 511];
    return (leaf & (VMM_P | VMM_U)) == (VMM_P | VMM_U);
}

/*
 * A load that may fault. `lm_probe_read` sets the result to 0 and then reads
 * through the pointer; the fault handler redirects RIP to `lm_probe_faulted`
 * when the #PF is the one vmm_probe() armed it for, which sets the result to 1.
 */
__asm__(
    ".text\n"
    ".globl lm_probe_read\n"
    "lm_probe_read:\n"
    "    xorl %eax, %eax\n"
    "    movq (%rdi), %rcx\n"
    "    ret\n"
    ".globl lm_probe_faulted\n"
    "lm_probe_faulted:\n"
    "    movl $1, %eax\n"
    "    ret\n");

u64 vmm_probe(u64 virt)
{
    lm_fault_fixup_cr2 = virt;
    lm_fault_fixup_rip = (u64)(unsigned long)lm_probe_faulted;
    u64 faulted = lm_probe_read(virt);
    lm_fault_fixup_cr2 = 0;
    lm_fault_fixup_rip = 0;
    return faulted;
}

int vmm_selftest(void)
{
    u64 frame_a = pmm_alloc_frame();
    u64 frame_b = pmm_alloc_frame();
    void *pa = vmm_phys(frame_a);
    void *pb = vmm_phys(frame_b);
    if (pa == NULL || pb == NULL) {
        kputs("[vmm] FAIL no frames for the address-space test\r\n");
        return -1;
    }
    memset(pa, 0, FRAME_SIZE);
    memset(pb, 0, FRAME_SIZE);

    int ok = 1;

    /* A fresh mapping takes the address, and a store through it lands in the
     * frame the walker names. */
    if (vmm_map(TEST_VA, frame_a, VMM_W) != 0) {
        ok = 0;
    }
    *(volatile u64 *)TEST_VA = 0x0123456789ABCDEFULL;
    if (*(u64 *)pa != 0x0123456789ABCDEFULL) {
        ok = 0;
    }
    if (vmm_translate(TEST_VA) != frame_a) {
        ok = 0;
    }
    if (vmm_map(TEST_VA, frame_b, VMM_W) != -1) {
        ok = 0;                      /* already mapped: must be refused */
    }

    /* Unmap has to be visible to the MMU, not just to the tables. */
    if (vmm_unmap(TEST_VA) != 0 || vmm_translate(TEST_VA) != 0 ||
        vmm_unmap(TEST_VA) != -1) {
        ok = 0;
    }
    u64 gone = vmm_probe(TEST_VA);    /* 1: the read faulted */
    if (gone != 1) {
        ok = 0;
    }
    if (vmm_map(TEST_VA, frame_b, VMM_W) != 0) {
        ok = 0;
    }
    u64 back = vmm_probe(TEST_VA);    /* 0: the read was served */
    if (back != 0 || vmm_translate(TEST_VA) != frame_b) {
        ok = 0;
    }

    /* U/S is checked at every level, so a user leaf needs the tables above it
     * marked too, while the kernel leaf beside it stays out of reach. */
    if (vmm_map(TEST_VA + FRAME_SIZE, frame_a, VMM_W | VMM_U) != 0) {
        ok = 0;
    }
    if (vmm_user_mapped(TEST_VA) != 0 ||
        vmm_user_mapped(TEST_VA + FRAME_SIZE) != 1) {
        ok = 0;
    }
    if (vmm_user_mapped(0x00100000) != 0 ||
        vmm_user_mapped(0xFFFFFFFF80100000ULL) != 0) {
        ok = 0;                      /* identity map and higher half are kernel */
    }
    if (vmm_translate(0x00100000) != 0x00100000) {
        ok = 0;                      /* ... but still resolve */
    }
    if (vmm_map(0x00200000, frame_a, VMM_W) != -1) {
        ok = 0;                      /* a 2 MB page is in the way: refuse */
    }

    kputs("[vmm] va=0x");
    kput_hex(TEST_VA, 16);
    kputs(" pa=0x");
    kput_hex(frame_a, 12);
    kputs(" unmapped=");
    kputs(gone == 1 ? "fault" : "FAIL");
    kputs(" mapped=");
    kputs(back == 0 ? "read" : "FAIL");
    kputs("\r\n");

    if (vmm_unmap(TEST_VA) != 0 || vmm_unmap(TEST_VA + FRAME_SIZE) != 0) {
        ok = 0;
    }
    pmm_free_frame(frame_a);
    pmm_free_frame(frame_b);

    kputs(ok ? "[vmm] vmm=ok\r\n" : "[vmm] FAIL address-space round trip\r\n");
    return ok ? 0 : -1;
}
