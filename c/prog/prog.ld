/*
 * prog.ld - where a C-built user program lands
 *
 * User programs are flat binaries loaded at PROGRAM_BASE (0x00500000) with the
 * user stack near the top of the same 1 MB window (kernel.asm). There is no
 * loader to relocate anything, so the link address is the load address and the
 * entry must be the first byte of the file: _start goes in .text.entry.
 *
 * .bss is folded into the PROGBITS .data output section. The window is reused
 * between programs, so a section that emitted no bytes would start life full
 * of whatever ran last; materialising the zeros is cheaper than a loader-side
 * clear and cannot be forgotten.
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x00500000;

    .text : {
        *(.text.entry)
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
