/*
 * lm64.ld - link the 64-bit nucleus into the higher half
 *
 * Roadmap P2: the kernel lives in the top 2 GB, which is where
 * -mcmodel=kernel expects it and where a 64-bit kernel belongs. stage2 loads
 * the flat image at physical 1 MB and maps that physical range at
 * 0xFFFFFFFF80000000 as well, so an address here resolves to the loaded image
 * with no relocation at run time.
 *
 * --oformat binary takes the sections in link order, so the flat image is
 * exactly what stage2 has to place at 1 MB: no file headers, no gaps.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(lm_start)

SECTIONS
{
    . = 0xFFFFFFFF80100000;
    lm_image_start = .;

    .text : {
        *(.text.lm_start)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
