/*
 * lm_bootinfo.c - read what the loader said about the machine
 *
 * Two loaders can start this nucleus: stage2 on a BIOS boot, which fills the
 * structure at a fixed low address, and a UEFI application, which allocates it
 * and passes the physical address in RDI. Both describe the same thing - memory
 * map and, when it found one, a linear framebuffer - so the rest of the nucleus
 * asks this file instead of the boot path.
 *
 * A loader older than this structure only wrote the 0x500 fields, so those are
 * still read as a fallback. That is the only place those addresses appear.
 */

#include "lm.h"
#include "bootinfo.h"

/*
 * The BI_OFF_* table is what the loaders write against, so a field that moves
 * without the table moving would be found only by booting a machine. These make
 * it a compile error instead. The assembly side is checked by
 * tools/validate_constants.py, which compares the same offsets with the writes
 * in stage2.asm.
 */
_Static_assert(BOOTINFO_MAGIC == 0x4E49424C, "magic must spell 'LBIN'");
_Static_assert(sizeof(struct lm_bootinfo) == 0xA0, "BootInfo v2 is 160 bytes");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, version) == BI_OFF_VERSION,
               "version offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, flags) == BI_OFF_FLAGS,
               "flags offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, mmap_count) == BI_OFF_MMAP_COUNT,
               "mmap_count offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, mmap_addr) == BI_OFF_MMAP_ADDR,
               "mmap_addr offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, fb_base) == BI_OFF_FB_BASE,
               "fb_base offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, fb_size) == BI_OFF_FB_SIZE,
               "fb_size offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, fb_width) == BI_OFF_FB_WIDTH,
               "fb_width offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, fb_pitch) == BI_OFF_FB_PITCH,
               "fb_pitch offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, fb_bpp) == BI_OFF_FB_BPP,
               "fb_bpp offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, fb_format) == BI_OFF_FB_FORMAT,
               "fb_format offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, rsdp) == BI_OFF_RSDP,
               "rsdp offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, boot_drive) == BI_OFF_BOOT_DRIVE,
               "boot_drive offset");
_Static_assert(__builtin_offsetof(struct lm_bootinfo, cmdline) == BI_OFF_CMDLINE,
               "cmdline offset");

struct lm_machine machine;

/* The legacy layout stage2 used before this structure existed. */
#define LEGACY_DRIVE     0x500U
#define LEGACY_MMAP_CNT  0x504U
#define LEGACY_MMAP_PTR  0x508U

static void clear_machine(void)
{
    u8 *p = (u8 *)&machine;
    for (u64 i = 0; i < sizeof(machine); i++) {
        p[i] = 0;
    }
}

void bootinfo_init(u64 info)
{
    clear_machine();

    u64 addr = info ? info : BOOTINFO_ADDR;
    struct lm_bootinfo *bi = (struct lm_bootinfo *)LM_PHYS(addr);

    if (bi->magic == BOOTINFO_MAGIC && bi->version >= BOOTINFO_VERSION) {
        machine.have_bootinfo = 1;
        machine.mmap_count = bi->mmap_count;
        machine.mmap_entry_size = bi->mmap_entry_size;
        machine.mmap_addr = bi->mmap_addr;
        machine.mmap_is_efi = (bi->flags & BOOTINFO_FLAG_EFI_MMAP) != 0;
        machine.rsdp = bi->rsdp;
        machine.smbios = bi->smbios;
        machine.boot_drive = bi->boot_drive;

        if (bi->flags & BOOTINFO_FLAG_FB) {
            machine.have_fb = 1;
            machine.fb_base = bi->fb_base;
            machine.fb_size = bi->fb_size;
            machine.fb_width = bi->fb_width;
            machine.fb_height = bi->fb_height;
            machine.fb_pitch = bi->fb_pitch;
            machine.fb_bpp = bi->fb_bpp;
            machine.fb_format = bi->fb_format;
        }
        return;
    }

    /* Older loader: the three fields it did write, and no framebuffer. Say
     * where it looked and what it found, because "no framebuffer" and "the
     * structure is not where you were told" look identical from the outside. */
    kputs("[bootinfo] no v2 structure at 0x");
    kput_hex(addr, 16);
    kputs(" (magic=0x");
    kput_hex(bi->magic, 8);
    kputs("), using the legacy fields\r\n");

    machine.mmap_count = lm_read32(LEGACY_MMAP_CNT);
    machine.mmap_entry_size = 24;
    machine.mmap_addr = lm_read32(LEGACY_MMAP_PTR);
    machine.boot_drive = lm_read32(LEGACY_DRIVE);
    machine.mmap_is_efi = 0;
}
