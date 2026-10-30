; hbx_dll.asm - a program that calls into a shared library.
;
; The imports are resolved by the loader before this image is copied anywhere
; (EXEC.md sections 3 and 6): lib_resolve_imports finds mvlib.hbl, matches each
; symbol in its export table, and writes the resolved address into the slot
; named below. This program only ever says `call [slot]`.
;
; HBI1 tells tools/mkhbx.py which slots those are. A hand-written program
; cannot know where the wrapper would put an import address table, so it
; declares its own, and the builder writes an import table that names them.
;
; The image is built for PROGRAM_BASE and has no relocations, so it is loaded
; by copying into the window - the road a fixed-base image has always taken.
; The library, by contrast, is mapped into the task's own address space; the
; call crosses from the window into the demand zone.
%include "syscalls.inc"

start:
        call [iat_hello]

        mov ebx, 20
        mov ecx, 22
        call [iat_add]
        cmp eax, 42
        jne .wrong

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.wrong:
        mov eax, SYS_PRINT
        mov ebx, msg_wrong
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; The import address table. The loader patches these two dwords in the file
; before the image is loaded, so the values here are never used.
iat_hello:      dd 0
iat_add:        dd 0

mod_name:   db "mvlib", 0
name_hello: db "mv_hello", 0
name_add:   db "mv_add", 0
msg_ok:     db "HBX_DLL_OK", 0x0A, 0
msg_wrong:  db "HBX_DLL_FAIL", 0x0A, 0

align 4
hbi_meta:   db "HBI1"                   ; cleared by the build tool
            dd mod_name - $$            ; module name RVA
            dd hbi_count
hbi_slots:  dd name_hello - $$, iat_hello - $$
            dd name_add - $$, iat_add - $$
hbi_count   equ ($ - hbi_slots) / 8
