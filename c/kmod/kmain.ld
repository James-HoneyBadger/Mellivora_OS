/*
 * kmain.ld - where a C-built kernel module lands
 *
 * The blob is linked on its own at the address the assembler reports for the
 * cmod slot in kernel/data.inc (the Makefile measures it with a probe assembly
 * and passes it here as cmod_addr), then assembled into the kernel image with
 * incbin. Nothing here may be relocated at run time: the kernel is a flat
 * binary and there is no loader to apply relocations.
 *
 * .cmod_head comes first so the header kernel/cmod.inc reads really is at
 * offset 0. .bss is folded into the PROGBITS .data output section on purpose:
 * the blob is copied into the image as bytes, so an uninitialized section that
 * emitted nothing would leave whatever the previous image left in memory.
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(cmod_init)

SECTIONS
{
    . = cmod_addr;

    .text : {
        *(.cmod_head)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data.rel.ro .data.rel.ro.*)
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
    }
}
