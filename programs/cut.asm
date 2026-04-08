; cut.asm - Field extraction utility [HBU]
; Usage: cut -f LIST [-d C] FILE
;   -f LIST  field list/ranges (e.g. 1,3,5-7), required
;   -d C     field delimiter character (default ',')
;
%include "syscalls.inc"

start:
        ; Read raw argument string
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Defaults
        mov dword [field_mask_lo], 0
        mov dword [field_mask_hi], 0
        mov byte [fields_set], 0
        mov byte [delim_char], ','
        mov byte [filename], 0

        mov esi, arg_buf

.parse_loop:
        call skip_spaces
        cmp byte [esi], 0
        je .parse_done

        cmp byte [esi], '-'
        je .parse_option

        ; Positional filename
        cmp byte [filename], 0
        jne .usage
        mov edi, filename
        call copy_word
        jmp .parse_loop

.parse_option:
        inc esi
        mov al, [esi]
        cmp al, 'f'
        je .opt_field
        cmp al, 'd'
        je .opt_delim
        jmp .usage

.opt_field:
        inc esi
        call parse_field_list
        cmp eax, 1
        jne .usage
        jmp .parse_loop

.opt_delim:
        inc esi
        cmp byte [esi], 0
        je .usage
        cmp byte [esi], ' '
        jne .opt_delim_take
        call skip_spaces
        cmp byte [esi], 0
        je .usage
.opt_delim_take:
        mov al, [esi]
        mov [delim_char], al
        inc esi
        jmp .parse_loop

.parse_done:
        ; Require both -f and filename
        cmp byte [fields_set], 1
        jne .usage
        cmp byte [filename], 0
        je .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, filename
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .file_err
        mov [file_size], eax

        ; Extract selected field from each line
        mov esi, file_buf
        mov ecx, [file_size]
        mov edx, 1              ; current field number

.process_loop:
        cmp ecx, 0
        je .done

        mov al, [esi]

        cmp al, 0x0A            ; newline
        je .emit_newline

        mov bl, [delim_char]
        cmp al, bl
        je .next_field

        mov eax, edx
        call is_field_selected
        jnc .advance

        ; Print character in selected field
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .advance

.emit_newline:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov edx, 1
        jmp .advance

.next_field:
        ; Preserve delimiter between selected consecutive fields.
        ; Example: -f1,2 on "a,b" prints "a,b".
        mov eax, edx
        call is_field_selected
        jnc .nf_skip_sep
        mov eax, edx
        inc eax
        call is_field_selected
        jnc .nf_skip_sep
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [delim_char]
        int 0x80
.nf_skip_sep:
        inc edx

.advance:
        inc esi
        dec ecx
        jmp .process_loop

.done:
        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.file_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_file_err
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

; Skip spaces at ESI
skip_spaces:
.ss_loop:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp .ss_loop
.ss_done:
        ret

; Copy word from ESI -> EDI and null terminate
copy_word:
.cw_loop:
        mov al, [esi]
        cmp al, 0
        je .cw_end
        cmp al, ' '
        je .cw_end
        mov [edi], al
        inc esi
        inc edi
        jmp .cw_loop
.cw_end:
        mov byte [edi], 0
        ret

; Parse decimal number at ESI.
; Accepts either immediate form (-f2) or spaced form (-f 2).
; Returns EAX = parsed number (or 0 if invalid).
; Leaves ESI at first non-digit.
parse_number_opt:
        cmp byte [esi], ' '
        jne .pn_start
        call skip_spaces
.pn_start:
        xor eax, eax
        xor edx, edx            ; saw_digit flag
.pn_loop:
        mov bl, [esi]
        cmp bl, '0'
        jb .pn_done
        cmp bl, '9'
        ja .pn_done
        imul eax, eax, 10
        movzx ebx, bl
        sub ebx, '0'
        add eax, ebx
        inc esi
        mov edx, 1
        jmp .pn_loop
.pn_done:
        cmp edx, 0
        jne .pn_ret
        xor eax, eax
.pn_ret:
        ret

; Parse LIST for -f option: "1,3,5-7"
; Returns EAX=1 on success, EAX=0 on error.
parse_field_list:
        call skip_spaces
        cmp byte [esi], 0
        je .pfl_fail

.pfl_item:
        call parse_number_opt
        cmp eax, 1
        jl .pfl_fail
        cmp eax, 64
        jg .pfl_fail
        mov [range_start], eax
        mov [range_end], eax

        cmp byte [esi], '-'
        jne .pfl_apply
        inc esi
        call parse_number_opt
        cmp eax, 1
        jl .pfl_fail
        cmp eax, 64
        jg .pfl_fail
        mov [range_end], eax

.pfl_apply:
        mov eax, [range_end]
        cmp eax, [range_start]
        jl .pfl_fail

        mov eax, [range_start]
.pfl_set_loop:
        call set_field_bit
        cmp eax, [range_end]
        je .pfl_after_apply
        inc eax
        jmp .pfl_set_loop

.pfl_after_apply:
        mov byte [fields_set], 1
        cmp byte [esi], ','
        je .pfl_next_item
        cmp byte [esi], ' '
        je .pfl_ok
        cmp byte [esi], 0
        je .pfl_ok
        jmp .pfl_fail

.pfl_next_item:
        inc esi
        jmp .pfl_item

.pfl_ok:
        mov eax, 1
        ret
.pfl_fail:
        xor eax, eax
        ret

; Set field bit for field number in EAX (1..64)
set_field_bit:
        push ebx
        push ecx
        mov ecx, eax
        dec ecx
        cmp ecx, 32
        jb .sfb_low
        sub ecx, 32
        mov eax, 1
        shl eax, cl
        mov ebx, [field_mask_hi]
        or ebx, eax
        mov [field_mask_hi], ebx
        pop ecx
        pop ebx
        ret
.sfb_low:
        mov eax, 1
        shl eax, cl
        mov ebx, [field_mask_lo]
        or ebx, eax
        mov [field_mask_lo], ebx
        pop ecx
        pop ebx
        ret

; Test if field number in EAX (1..64) is selected.
; Returns CF=1 if selected, CF=0 otherwise.
is_field_selected:
        push eax
        push ebx
        push ecx
        cmp eax, 1
        jl .ifs_no
        cmp eax, 64
        jg .ifs_no
        mov ecx, eax
        dec ecx
        cmp ecx, 32
        jb .ifs_low
        sub ecx, 32
        mov ebx, [field_mask_hi]
        bt ebx, ecx
        jc .ifs_yes
        jmp .ifs_no
.ifs_low:
        mov ebx, [field_mask_lo]
        bt ebx, ecx
        jc .ifs_yes
        jmp .ifs_no
.ifs_yes:
        pop ecx
        pop ebx
        pop eax
        stc
        ret
.ifs_no:
        pop ecx
        pop ebx
        pop eax
        clc
        ret

msg_usage:     db "Usage: cut -f LIST [-d C] FILE", 0x0A
               db "  LIST: 1,3,5-7", 0x0A, 0
msg_file_err:  db "Error: Cannot read file", 0x0A, 0

arg_buf:       times 256 db 0
filename:      times 128 db 0
file_size:     dd 0
field_mask_lo: dd 0
field_mask_hi: dd 0
range_start:   dd 0
range_end:     dd 0
fields_set:    db 0
delim_char:    db 0
file_buf:      times 16384 db 0
