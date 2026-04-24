; patch.asm - Apply a diff patch to a file in HBFS
; Usage: patch FILE PATCHFILE
;
; PATCHFILE format is the output of the 'diff' utility:
;   Lines starting with '< ' are lines to remove from FILE
;   Lines starting with '> ' are lines to insert at current position
;   All other lines are ignored
;
; Algorithm:
;   1. Read FILE into file_buf; index lines into file_lines[]
;   2. Read PATCHFILE into patch_buf
;   3. Walk patch lines; '<' = mark file line deleted, '>' = emit new line
;      context/other = emit corresponding un-deleted file line
;   4. Write out_buf back to FILE

%include "syscalls.inc"

FILE_BUF_SIZE  equ 0x8000
PATCH_BUF_SIZE equ 0x8000
OUT_BUF_SIZE   equ 0x8000
NAME_BUF_SIZE  equ 256
LINE_BUF_SIZE  equ 1024
MAX_LINES      equ 4096

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        mov edi, file_name
        call copy_word_to
        call skip_spaces
        cmp byte [esi], 0
        je .usage
        mov edi, patch_name
        call copy_word_to

        ; Load target file
        mov eax, SYS_FREAD
        mov ebx, file_name
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jl .file_err
        mov [file_size], eax
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        ; Load patch file
        mov eax, SYS_FREAD
        mov ebx, patch_name
        mov ecx, patch_buf
        int 0x80
        cmp eax, 0
        jl .patch_err
        mov [patch_size], eax
        mov edi, patch_buf
        add edi, eax
        mov byte [edi], 0

        ; Build line index
        call build_line_table

        ; Apply patch
        mov dword [patch_ptr], patch_buf
        mov edi, out_buf
        mov dword [cur_line], 0
        mov dword [err_count], 0

.patch_loop:
        mov esi, [patch_ptr]
        mov eax, esi
        sub eax, patch_buf
        cmp eax, [patch_size]
        jge .patch_done
        cmp byte [esi], 0
        je .patch_done

        ; Read one patch line
        mov ebx, patch_line_buf
        call read_line          ; ESI advances; line in patch_line_buf
        mov [patch_ptr], esi

        mov al, [patch_line_buf]
        cmp al, '<'
        je .do_remove
        cmp al, '>'
        je .do_insert
        ; Context — emit next kept file line
        mov eax, [cur_line]
        cmp eax, [file_line_count]
        jge .patch_loop
        call emit_line          ; EDI = write head; EAX = line index
        inc dword [cur_line]
        jmp .patch_loop

.do_remove:
        lea esi, [patch_line_buf + 2]
        call find_and_del       ; returns EBX = index or -1
        cmp ebx, -1
        jne .patch_loop
        inc dword [err_count]
        push edi
        mov eax, SYS_PRINT
        mov ebx, msg_hunk_fail
        int 0x80
        pop edi
        jmp .patch_loop

.do_insert:
        lea esi, [patch_line_buf + 2]
.ins_cp:
        lodsb
        cmp al, 0
        je .ins_nl
        stosb
        jmp .ins_cp
.ins_nl:
        mov byte [edi], 0x0A
        inc edi
        jmp .patch_loop

.patch_done:
        ; Flush remaining file lines
        mov eax, [cur_line]
.flush_rest:
        cmp eax, [file_line_count]
        jge .flush_done
        call emit_line
        inc eax
        jmp .flush_rest
.flush_done:

        ; Compute output length and write back
        mov eax, edi
        sub eax, out_buf
        mov [out_len], eax

        push edi
        mov eax, SYS_FWRITE
        mov ebx, file_name
        mov ecx, out_buf
        mov edx, [out_len]
        xor esi, esi
        int 0x80
        pop edi
        cmp eax, 0
        jl .write_err

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80

        mov eax, [err_count]
        test eax, eax
        jz .done
        push edi
        mov ebx, num_buf
        call int_to_dec
        mov eax, SYS_PRINT
        mov ebx, msg_warn_pre
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, num_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_warn_suf
        int 0x80
        pop edi

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
.file_err:
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
.patch_err:
        mov eax, SYS_PRINT
        mov ebx, msg_patch_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
.write_err:
        mov eax, SYS_PRINT
        mov ebx, msg_write_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;=======================================================================
; build_line_table — index file_buf lines into file_lines[] (pointers)
; and zero file_line_del[]
;=======================================================================
build_line_table:
        mov esi, file_buf
        xor ecx, ecx
.blt_loop:
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .blt_done
        cmp byte [esi], 0
        je .blt_done
        cmp ecx, MAX_LINES
        jge .blt_done
        mov [file_lines + ecx*4], esi
        mov byte [file_line_del + ecx], 0
        inc ecx
.blt_adv:
        lodsb
        cmp al, 0x0A
        je .blt_loop
        cmp al, 0
        je .blt_done_back
        jmp .blt_adv
.blt_done_back:
        dec esi
.blt_done:
        mov [file_line_count], ecx
        ret

;=======================================================================
; find_and_del — scan file_lines[] from [cur_line] for text at ESI
; Mark first match deleted; return EBX = index or -1
;=======================================================================
find_and_del:
        push esi
        mov ebx, [cur_line]
.fnd_loop:
        cmp ebx, [file_line_count]
        jge .fnd_nf
        cmp byte [file_line_del + ebx], 1
        je .fnd_next
        push esi
        push ebx
        mov edi, [file_lines + ebx*4]
.fnd_cmp:
        mov al, [esi]
        cmp al, 0
        je .fnd_match
        mov cl, [edi]
        cmp cl, 0x0A
        je .fnd_mis
        cmp cl, 0
        je .fnd_mis
        cmp al, cl
        jne .fnd_mis
        inc esi
        inc edi
        jmp .fnd_cmp
.fnd_match:
        pop ebx
        pop esi
        mov byte [file_line_del + ebx], 1
        pop esi
        ret
.fnd_mis:
        pop ebx
        pop esi
.fnd_next:
        inc ebx
        jmp .fnd_loop
.fnd_nf:
        pop esi
        mov ebx, -1
        ret

;=======================================================================
; emit_line — copy file line [EAX] to [EDI] if not deleted; advance EDI
;=======================================================================
emit_line:
        cmp byte [file_line_del + eax], 1
        je .el_skip
        push esi
        mov esi, [file_lines + eax*4]
.el_cp:
        lodsb
        cmp al, 0x0A
        je .el_nl
        cmp al, 0
        je .el_end
        stosb
        jmp .el_cp
.el_nl:
        stosb
.el_end:
        pop esi
.el_skip:
        ret

;=======================================================================
; read_line — read from [ESI] into [EBX] until newline/EOF; advance ESI
;=======================================================================
read_line:
        mov edi, ebx
.rl:
        lodsb
        cmp al, 0x0A
        je .rl_done
        cmp al, 0x0D
        je .rl
        cmp al, 0
        je .rl_eof
        stosb
        jmp .rl
.rl_eof:
        dec esi
.rl_done:
        mov byte [edi], 0
        ret

;=======================================================================
; skip_spaces / copy_word_to / int_to_dec
;=======================================================================
skip_spaces:
        cmp byte [esi], ' '
        jne .sp_done
        inc esi
        jmp skip_spaces
.sp_done:
        ret

copy_word_to:
.cw:
        mov al, [esi]
        cmp al, 0
        je .cw_done
        cmp al, ' '
        je .cw_done
        cmp al, 0x0A
        je .cw_done
        stosb
        inc esi
        jmp .cw
.cw_done:
        mov byte [edi], 0
        ret

int_to_dec:
        push edi
        mov edi, ebx
        test eax, eax
        jns .itd_pos
        mov byte [edi], '-'
        inc edi
        neg eax
.itd_pos:
        push edi
        xor ecx, ecx
.itd_d:
        xor edx, edx
        push ecx
        mov ecx, 10
        div ecx
        pop ecx
        add dl, '0'
        push edx
        inc ecx
        test eax, eax
        jnz .itd_d
.itd_p:
        pop eax
        mov [edi], al
        inc edi
        dec ecx
        jnz .itd_p
        pop ecx
        mov byte [edi], 0
        pop edi
        ret

;=======================================================================
; Messages
;=======================================================================
msg_usage:    db "Usage: patch FILE PATCHFILE", 0x0A, 0
msg_ok:       db "patch: applied successfully", 0x0A, 0
msg_hunk_fail: db "patch: warning: hunk not found, skipping", 0x0A, 0
msg_warn_pre: db "patch: ", 0
msg_warn_suf: db " hunk(s) could not be applied", 0x0A, 0
msg_file_err: db "patch: cannot read target file", 0x0A, 0
msg_patch_err: db "patch: cannot read patch file", 0x0A, 0
msg_write_err: db "patch: cannot write output", 0x0A, 0

;=======================================================================
; BSS
;=======================================================================
file_name:       resb NAME_BUF_SIZE
patch_name:      resb NAME_BUF_SIZE
args_buf:        resb NAME_BUF_SIZE * 2
patch_line_buf:  resb LINE_BUF_SIZE
num_buf:         resb 16
file_size:       resd 1
patch_size:      resd 1
out_len:         resd 1
file_line_count: resd 1
cur_line:        resd 1
patch_ptr:       resd 1
err_count:       resd 1
file_lines:      resd MAX_LINES
file_line_del:   resb MAX_LINES
file_buf:        resb FILE_BUF_SIZE
patch_buf:       resb PATCH_BUF_SIZE
out_buf:         resb OUT_BUF_SIZE
