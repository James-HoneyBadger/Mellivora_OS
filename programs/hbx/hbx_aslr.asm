; hbx_aslr.asm - say where this image was loaded.
;
; The make rule assembles it for 0x00400000; a loader that maps it at a random
; base has to relocate every absolute address below, and the number printed is
; the address of `msg` in this launch. Two launches that print the same number
; did not randomise anything.
;
; Every absolute address in the payload is a relocation, including the immediate
; of a `mov ebx, msg` - a relocation table that lists only data pointers leaves
; the code reading the address the image was built for.
%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        db 0xBB                         ; mov ebx, imm32
prefix_imm:     dd msg
        int 0x80

        db 0x8B, 0x1D                   ; mov ebx, [msg_addr]
disp_of_addr:   dd msg_addr

        mov ebp, ebx                    ; the value, kept out of EBX: the
        mov ecx, 8                      ; character goes there for the call
.hex:
        rol ebp, 4
        mov ebx, ebp
        and ebx, 0x0F
        cmp ebx, 10
        jb .digit
        add ebx, 'A' - 10
        jmp .emit
.digit:
        add ebx, '0'
.emit:
        mov eax, SYS_PUTCHAR
        int 0x80
        loop .hex

        mov eax, SYS_PRINT
        db 0xBB                         ; mov ebx, imm32
nl_imm: dd msg_nl
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg:    db "HBX_ASLR 0x", 0
msg_nl: db 0x0A, 0

align 4
msg_addr: dd msg

align 4
hbx_meta:      db "HBM1"
               dd hbx_reloc_count
hbx_relocs:    dd prefix_imm - $$, disp_of_addr - $$, msg_addr - $$, nl_imm - $$
hbx_reloc_count equ ($ - hbx_relocs) / 4
