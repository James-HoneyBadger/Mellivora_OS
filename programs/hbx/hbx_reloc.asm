; hbx_reloc.asm - an HBX payload built for a base it will not be loaded at.
;
; The make rule assembles this with -DPROGRAM_ORG=0x00400000 while the loader
; puts it at PROGRAM_BASE (0x00500000). The marker table below tells
; tools/mkhbx.py which dwords hold an absolute address: the string pointer is
; one, so a loader that does not apply the +0x100000 delta jumps to the
; unmapped address the image was built for instead of printing its marker.
;
; `label - $$` is the RVA of a label: $$ is the section start, which is the
; ORG this file was assembled at, so the same source describes the table
; correctly at any base.
%include "syscalls.inc"

start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A                   ; light green
        int 0x80
        mov eax, SYS_PRINT
        ; `mov ebx, [msg_ptr]` holds two absolute addresses: the displacement
        ; of msg_ptr, and the value stored there. Both are relocations, which
        ; is why the instruction is written out with its displacement labelled.
        db 0x8B, 0x1D                   ; mov ebx, [ptr_disp]
ptr_disp: dd msg_ptr
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg_ptr: dd msg
msg:     db "HBX_RELOC_OK", 0x0A, 0

align 4
hbx_meta:      db "HBM1"                ; cleared by the build tool
               dd hbx_reloc_count
hbx_relocs:    dd ptr_disp - $$, msg_ptr - $$
hbx_reloc_count equ ($ - hbx_relocs) / 4
