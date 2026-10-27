; stale_write.asm - leave a recognisable pattern in the program window
;
; Half of the stale-data regression pair; stale_probe.asm is the other half.
; A new task's window starts as a copy of the shell's window, so whatever the
; previous program left above its own image is readable by the next one unless
; the launcher clears it. This program fills every dword from FILL_BASE up to
; its own stack pointer - its image, its heap and its whole user stack - with
; MARKER and exits.
;
; Run as:  stale_write
; Output:  STALE_WROTE_OK

%include "syscalls.inc"

MARKER   equ 0x4D415453         ; 'STAM'

start:
        ; 1. Everything between this program's image and the stack region: the
        ;    area a heap or an mmap scratch buffer would come from.
        mov edi, FILL_BASE
        mov ecx, (STACK_REGION - FILL_BASE) / 4
        mov eax, MARKER
        cld
        rep stosd

        ; 2. The mapped stack window: the 15 pages below ESP (the 16th ends at
        ;    ESP and holds the launcher's trampoline address; below the window
        ;    is the unmapped growth area, so stop there).
        mov edi, esp
        sub edi, 0xF000
        mov ecx, 0xF000 / 4
        rep stosd

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80

        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

FILL_BASE    equ 0x540000       ; 256 KB into the window: above any fixture
STACK_REGION equ 0x5C0000       ; USER_STACK_BASE: the stack guard page
msg_ok:  db 'STALE_WROTE_OK', 13, 10, 0
