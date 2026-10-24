/*
 * bootinfo.h - the contract between the loaders and the nucleus
 *
 * The nucleus is started with one structure and nothing else. On a BIOS boot
 * stage2 fills it in at a fixed low address; on a UEFI boot the loader allocates
 * it and passes the physical address in RDI. Both describe the same thing -
 * memory map, framebuffer geometry, firmware tables - so the rest of the
 * nucleus asks this structure instead of the boot path.
 *
 * The offsets here are the authority: the loaders write against them, and what
 * checks that they agree is tools/validate_constants.py plus the _Static_asserts
 * in lm_bootinfo.c.
 */

#ifndef LM_BOOTINFO_H
#define LM_BOOTINFO_H

#include "lm.h"

#define BOOTINFO_MAGIC      0x4E49424C      /* 'LBIN' little endian */
#define BOOTINFO_VERSION    2U

/* flags */
#define BOOTINFO_FLAG_FB        (1U << 0)   /* framebuffer geometry is valid  */
#define BOOTINFO_FLAG_UEFI      (1U << 1)   /* came from a UEFI loader        */
#define BOOTINFO_FLAG_EFI_MMAP  (1U << 2)   /* memory map is EFI descriptors  */

/* fb_format */
#define BOOTINFO_FB_BGRX    0               /* blue, green, red, unused       */
#define BOOTINFO_FB_RGBX    1               /* red, green, blue, unused       */

/* The BIOS-safe landing zone both loaders can use before the kernel is up. */
#define BOOTINFO_LEGACY_ADDR    0x500ULL    /* stage2's original fields       */
#define BOOTINFO_ADDR           0x600ULL    /* this structure, BIOS path      */

/* Offsets, for the assembly and for the validator. */
#define BI_OFF_MAGIC        0x00
#define BI_OFF_VERSION      0x04
#define BI_OFF_SIZE         0x08
#define BI_OFF_FLAGS        0x0C
#define BI_OFF_MMAP_COUNT   0x10
#define BI_OFF_MMAP_ENTRY   0x18
#define BI_OFF_MMAP_ADDR    0x20
#define BI_OFF_FB_BASE      0x28
#define BI_OFF_FB_SIZE      0x30
#define BI_OFF_FB_WIDTH     0x38
#define BI_OFF_FB_HEIGHT    0x3C
#define BI_OFF_FB_PITCH     0x40
#define BI_OFF_FB_BPP       0x44
#define BI_OFF_FB_FORMAT    0x45
#define BI_OFF_RSDP         0x48
#define BI_OFF_SMBIOS       0x50
#define BI_OFF_BOOT_DRIVE   0x58
#define BI_OFF_CMDLINE      0x60

struct lm_bootinfo {
    u32 magic;              /* 0x00 */
    u32 version;            /* 0x04 */
    u32 size;               /* 0x08 */
    u32 flags;              /* 0x0C */
    u64 mmap_count;         /* 0x10 */
    u64 mmap_entry_size;    /* 0x18 */
    u64 mmap_addr;          /* 0x20 physical address of the array */
    u64 fb_base;            /* 0x28 physical address of the framebuffer */
    u64 fb_size;            /* 0x30 bytes */
    u32 fb_width;           /* 0x38 pixels */
    u32 fb_height;          /* 0x3C pixels */
    u32 fb_pitch;           /* 0x40 bytes per scan line */
    u8  fb_bpp;             /* 0x44 bits per pixel */
    u8  fb_format;          /* 0x45 BOOTINFO_FB_* */
    u16 fb_reserved;        /* 0x46 */
    u64 rsdp;               /* 0x48 ACPI RSDP, or 0 */
    u64 smbios;             /* 0x50 SMBIOS entry point, or 0 */
    u32 boot_drive;         /* 0x58 BIOS drive number, or 0x80 under UEFI */
    u32 reserved;           /* 0x5C */
    char cmdline[64];       /* 0x60 */
};

/* The firmware's own descriptor, which is what a UEFI loader passes. Type 7 is
 * conventional memory: the only kind this allocator may hand out. */
struct efi_memory_desc {
    u32 type;
    u32 pad;
    u64 phys_start;
    u64 virt_start;
    u64 pages;
    u64 attributes;
};
#define EFI_CONVENTIONAL 7

/* A validated view of what the loader said. */
struct lm_machine {
    int have_bootinfo;
    int have_fb;
    u64 fb_base;
    u64 fb_size;
    u32 fb_width;
    u32 fb_height;
    u32 fb_pitch;
    u8  fb_bpp;
    u8  fb_format;
    u64 mmap_count;
    u64 mmap_entry_size;
    u64 mmap_addr;
    int mmap_is_efi;
    u64 rsdp;
    u64 smbios;
    u32 boot_drive;
};

/* Filled in by bootinfo_init() from the pointer the loader passed in RDI. */
extern struct lm_machine machine;

/*
 * bootinfo_init - read what the loader said.
 *   In : info = physical address of a struct lm_bootinfo, or 0 when the loader
 *        is an old one that only wrote the 0x500 fields.
 *
 * The pointer is physical: the nucleus runs on the higher-half mapping, so the
 * structure is reached through the window at LM_PHYS_BASE, which the loader
 * maps for the first gigabyte of physical memory. Anything in that gigabyte -
 * the nucleus itself, this structure, the boot stack - can be addressed this
 * way, which is what makes it useful.
 */
void bootinfo_init(u64 info);

#define LM_PHYS_BASE 0xFFFFFFFF80000000ULL
#define LM_PHYS(addr) ((void *)(u64)((addr) + LM_PHYS_BASE))

/*
 * The framebuffer is a device aperture, not memory: its physical address is
 * wherever the firmware put it, so it is not inside LM_PHYS_BASE's gigabyte and
 * cannot be reached through it. The loader maps the gigabyte the aperture
 * starts in at LM_FB_WINDOW, an address in the kernel's own half that every
 * task inherits, so the console can print from inside a system call.
 */
#define LM_FB_WINDOW 0xFFFFFFFFC0000000ULL
#define LM_FB_VA(addr) ((void *)(u64)(LM_FB_WINDOW + ((addr) & 0x3FFFFFFFULL)))

#endif /* LM_BOOTINFO_H */
