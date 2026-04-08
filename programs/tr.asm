; tr.asm - Translate characters [HBU]
; Usage: tr SET1 SET2 FILENAME
; Replaces each character in SET1 with the corresponding character in SET2
%include "syscalls.inc"

start:
        ; Get args
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        je .usage

        mov esi, arg_buf
        call .skip_sp

        ; Copy SET1
        mov edi, set1
        call .copy_word
        cmp byte [set1], 0
        je .usage

        call .skip_sp

        ; Copy SET2
        mov edi, set2
        call .copy_word
        cmp byte [set2], 0
        je .usage

        call .skip_sp

        ; Copy FILENAME
        mov edi, filename
        call .copy_word
        cmp byte [filename], 0
        je .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov [file_size], eax
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        ; Build translation table (identity map)
        xor ecx, ecx
.build_table:
        mov [trans_table + ecx], cl
        inc ecx
        cmp ecx, 256
        jl .build_table

        ; Apply SET1 -> SET2 mapping
        mov esi, set1
        mov edi, set2
.map_loop:
        movzx eax, byte [esi]
        cmp al, 0
        je .do_translate
        movzx ebx, byte [edi]
        cmp bl, 0
        je .do_translate
        mov [trans_table + eax], bl
        inc esi
        inc edi
        jmp .map_loop

.do_translate:
        mov esi, file_buf
.tr_loop:
        movzx eax, byte [esi]
        cmp al, 0
        je .done
        movzx ebx, byte [trans_table + eax]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .tr_loop

.done:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

; Helper: skip spaces at ESI
.skip_sp:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp .skip_sp
.ss_done:
        ret

; Helper: copy word from ESI to EDI, advance ESI
.copy_word:
        cmp byte [esi], 0
        je .cw_end
        cmp byte [esi], ' '
        je .cw_end
        movsb
        jmp .copy_word
.cw_end:
        mov byte [edi], 0
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_usage:     db "Usage: tr SET1 SET2 FILENAME", 0x0A, 0
msg_not_found: db "File not found.", 0x0A, 0

;---------------------------------------
; BSS
;---------------------------------------
arg_buf:     times 256 db 0
set1:        times 128 db 0
set2:        times 128 db 0
filename:    times 128 db 0
trans_table: times 256 db 0
file_buf:    times 65536 db 0
file_size:   dd 0
