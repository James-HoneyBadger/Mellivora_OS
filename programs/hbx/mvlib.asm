; mvlib.asm - a shared library: two exports, called through one program's IAT.
;
; The make rule assembles this for 0x00700000, a base it will never be loaded
; at, so every launch exercises the relocation path: `hbx_choose_base` picks an
; address in the demand zone and the delta is applied to every entry in HBM1.
;
; Two build-time tables are tables because the payload can describe itself and
; the wrapper cannot:
;
;   HBE1 - what this library exports. `label - $$` is the RVA of a label at any
;          ORG, so the same source is correct whatever base it is built for;
;          tools/mkhbx.py turns the table into the image's export table.
;   HBM1 - the absolute addresses that have to move with the image (EXEC.md
;          section 2). A pointer to a string is one, and so is the immediate of
;          `mov ebx, msg` - a relocation table that lists only data pointers
;          leaves the code reading the address the image was built at.
;
; Calling convention: plain cdecl-less registers, because the caller is
; assembly too. mv_add takes EBX and ECX and leaves the sum in EAX; mv_hello
; takes nothing and preserves everything.
%include "syscalls.inc"

; syscalls.inc jumps here, so the label has to exist. Nothing calls a library's
; entry point yet: EXEC.md section 6's PROCESS_ATTACH notification is a later
; increment, and until then a library is code and data with an export table.
start:
        ret

; mv_hello prints once per call and counts how many times *this task* has
; called it, in the library's own thread-local storage - a block of its own,
; separate from the program's. The count is 1 on the first call.
mv_hello:
        pushad
        db 0x8B, 0x1D                   ; mov ebx, [base_slot]
mv_tls_slot:    dd mv_tls_base
        inc dword [ebx]
        push ebx
        mov eax, SYS_PRINT
        db 0xBB                         ; mov ebx, imm32
tls_count_imm:  dd msg_tls
        int 0x80
        pop ebx
        mov eax, [ebx]
        add al, '0'
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B                   ; light cyan
        int 0x80
        mov eax, SYS_PRINT
        db 0xBB                         ; mov ebx, imm32
hello_imm:      dd msg_hello
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        popad
        ret

mv_add:
        push ebx
        push ecx
        mov eax, ebx
        add eax, ecx
        pop ecx
        pop ebx
        ret

name_hello: db "mv_hello", 0
name_add:   db "mv_add", 0
msg_hello:  db "LIB_HELLO", 0x0A, 0
msg_tls:    db "LIB_TLS ", 0

; This library's thread-local storage: one dword, incremented per call.
mv_tls_template: dd 0
mv_tls_template_end:
mv_tls_index:   dd 0                    ; the loader writes this module's index
mv_tls_base:    dd 0                    ; the loader writes this thread's block

align 4
hbe_meta:   db "HBE1"                   ; cleared by the build tool
            dd hbe_count
hbe_pairs:  dd name_hello - $$, mv_hello - $$
            dd name_add - $$, mv_add - $$
hbe_count   equ ($ - hbe_pairs) / 8

align 4
tls_meta:   db "TLS1"                   ; cleared by the build tool
            dd mv_tls_template_end - mv_tls_template
            dd mv_tls_template - $$
            dd mv_tls_index - $$
            dd mv_tls_base - $$

align 4
hbm_meta:   db "HBM1"
            dd hbm_count
hbm_relocs: dd hello_imm - $$, mv_tls_slot - $$, tls_count_imm - $$
hbm_count   equ ($ - hbm_relocs) / 4
