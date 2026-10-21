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
static u64 *vmm_leaf_in(u64 space, u64 virt, int create, u64 flags)
{
    static const int shift[3] = {39, 30, 21};

    u64 *table = vmm_phys(space);
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

u64 vmm_flags(u64 virt)
{
    u64 *leaf = vmm_leaf_in(vmm_current_space(), virt, 0, 0);
    return leaf != NULL ? *leaf : 0;
}

/* Translation in a space that is not the current one: the table walk goes through
 * the direct map, so no CR3 switch is needed. */
u64 vmm_translate_in(u64 space, u64 virt)
{
    u64 *leaf = vmm_leaf_in(space, virt, 0, 0);
    return leaf != NULL ? *leaf : 0;
}

u64 vmm_current_space(void)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PTE_ADDR;
}

void vmm_space_switch(u64 space)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(space) : "memory");
}

/*
 * A PML4 of its own. The kernel's higher half (image and direct map) and the
 * heap window are shared by copying their entries: they point at the same lower
 * tables, so mappings made later in the heap are visible from every space. The
 * identity map is deliberately not copied - a task has no reason to see
 * physical memory - so a task's PML4[0] holds nothing but its own user area.
 */
u64 vmm_space_create(void)
{
    u64 frame = pmm_alloc_frame();
    u64 *space = vmm_phys(frame);
    u64 *kernel = vmm_phys(vmm_current_space());
    if (space == NULL || kernel == NULL) {
        if (frame != 0) {
            pmm_free_frame(frame);
        }
        return 0;
    }

    memset(space, 0, FRAME_SIZE);
    space[KHEAP_PML4_INDEX] = kernel[KHEAP_PML4_INDEX];
    space[KERNEL_PML4_INDEX] = kernel[KERNEL_PML4_INDEX];
    return frame;
}

int vmm_map_in(u64 space, u64 virt, u64 phys, u64 flags)
{
    if ((virt & (FRAME_SIZE - 1)) != 0 || (phys & (FRAME_SIZE - 1)) != 0 ||
        phys == 0 || space == 0) {
        return -1;
    }

    if (!cpu_nx_enabled()) {
        flags &= ~VMM_NX;           /* a reserved bit without EFER.NXE */
    }

    u64 *leaf = vmm_leaf_in(space, virt, 1, flags);
    if (leaf == NULL || (*leaf & VMM_P) != 0) {
        return -1;
    }

    *leaf = phys | VMM_P | (flags & (VMM_W | VMM_U | VMM_NX));
    tlb_flush(virt);
    return 0;
}

int vmm_map(u64 virt, u64 phys, u64 flags)
{
    return vmm_map_in(vmm_current_space(), virt, phys, flags);
}

int vmm_unmap(u64 virt)
{
    u64 *leaf = vmm_leaf_in(vmm_current_space(), virt, 0, 0);
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

    u64 *table = vmm_phys(vmm_current_space());
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

    u64 *table = vmm_phys(vmm_current_space());
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

/*---------------------------------------
 * Copying and tearing down a task's space
 *---------------------------------------*/

/*
 * Duplicate the user half of `src`: every 4 KB leaf reachable from PML4[0] that
 * carries the user bit gets a frame of its own with the bytes copied. The kernel
 * half and the heap window are shared, so they are copied as table entries by
 * vmm_space_create and are never touched here.
 *
 * Walking the tables rather than probing addresses matters: the user half is 512
 * GB, which is 134 million pages to probe and a few thousand entries to walk.
 */
u64 vmm_space_copy(u64 src)
{
    u64 *source = vmm_phys(src);
    if (source == NULL) {
        return 0;
    }

    u64 dest = vmm_space_create();
    u64 *target = vmm_phys(dest);
    if (target == NULL) {
        return 0;
    }

    u64 *pdpt = vmm_phys(source[0] & VMM_ADDR_MASK);
    if (pdpt == NULL) {
        return dest;                /* nothing user-mapped at all */
    }

    for (u32 p = 0; p < 512; p++) {
        if ((pdpt[p] & VMM_P) == 0 || (pdpt[p] & VMM_PS) != 0) {
            continue;
        }
        u64 *pd = vmm_phys(pdpt[p] & VMM_ADDR_MASK);
        if (pd == NULL) {
            continue;
        }
        for (u32 d = 0; d < 512; d++) {
            if ((pd[d] & VMM_P) == 0 || (pd[d] & VMM_PS) != 0) {
                continue;
            }
            u64 *pt = vmm_phys(pd[d] & VMM_ADDR_MASK);
            if (pt == NULL) {
                continue;
            }
            /* PML4 covers 512 GB per entry, the PDPT 1 GB, the PD 2 MB, so a
             * leaf's address is the three table indices at 30, 21 and 12. */
            u64 base = ((u64)p << 30) | ((u64)d << 21);
            for (u32 t = 0; t < 512; t++) {
                if ((pt[t] & VMM_P) == 0 || (pt[t] & VMM_U) == 0) {
                    continue;
                }
                u64 virt = base | ((u64)t << 12);
                u64 frame = pmm_alloc_frame();
                u8 *fresh = vmm_phys(frame);
                u8 *old = vmm_phys(pt[t] & VMM_ADDR_MASK);
                if (fresh == NULL || old == NULL) {
                    if (frame != 0) {
                        pmm_free_frame(frame);
                    }
                    return 0;
                }
                for (u32 b = 0; b < FRAME_SIZE; b++) {
                    fresh[b] = old[b];
                }
                u64 flags = VMM_U | ((pt[t] & VMM_W) != 0 ? VMM_W : 0);
                if (!cpu_nx_enabled()) {
                    flags &= ~VMM_NX;
                } else if ((pt[t] & VMM_NX) != 0) {
                    flags |= VMM_NX;
                }
                if (vmm_map_in(dest, virt, frame, flags) != 0) {
                    pmm_free_frame(frame);
                    return 0;
                }
            }
        }
    }
    return dest;
}

/* Free every user page and then the tables themselves. */
void vmm_space_destroy(u64 space)
{
    if (space == 0) {
        return;
    }
    u64 *pml4 = vmm_phys(space);
    if (pml4 == NULL) {
        return;
    }

    u64 *pdpt = vmm_phys(pml4[0] & VMM_ADDR_MASK);
    if (pdpt != NULL) {
        for (u32 p = 0; p < 512; p++) {
            u64 *pd = vmm_phys(pdpt[p] & VMM_ADDR_MASK);
            if ((pdpt[p] & VMM_P) == 0 || (pdpt[p] & VMM_PS) != 0 ||
                pd == NULL) {
                continue;
            }
            for (u32 d = 0; d < 512; d++) {
                u64 *pt = vmm_phys(pd[d] & VMM_ADDR_MASK);
                if ((pd[d] & VMM_P) == 0 || (pd[d] & VMM_PS) != 0 ||
                    pt == NULL) {
                    continue;
                }
                for (u32 t = 0; t < 512; t++) {
                    if ((pt[t] & VMM_P) != 0 && (pt[t] & VMM_U) != 0) {
                        pmm_free_frame(pt[t] & VMM_ADDR_MASK);
                    }
                }
                pmm_free_frame(pd[d] & VMM_ADDR_MASK);
            }
            pmm_free_frame(pdpt[p] & VMM_ADDR_MASK);
        }
        pmm_free_frame(pml4[0] & VMM_ADDR_MASK);
    }

    /* The higher half and the heap window are shared with every other space, so
     * only the top-level table belongs to this one. */
    pmm_free_frame(space);
}
