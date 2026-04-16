; csv.asm - Simple CSV viewer for Mellivora OS
; Usage: csv FILENAME
; Displays CSV files in formatted columns
%include "syscalls.inc"

MAX_COLS equ 10
COL_WIDTH equ 15

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        je .usage

        mov esi, arg_buf
.skip_sp:
        cmp byte [esi], ' '
        jne .got_name
        inc esi
        jmp .skip_sp
.got_name:
        cmp byte [esi], 0
        je .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov edi, file_buf
        add edi, eax
        mov byte [edi], 0

        mov esi, file_buf
        mov dword [row_num], 0

.process_line:
        cmp byte [esi], 0
        je .done

        inc dword [row_num]
        mov dword [col_num], 0

        ; Header row in special color
        cmp dword [row_num], 1
        jne .data_row
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        jmp .parse_cols

.data_row:
        ; Alternate row colors
        mov eax, [row_num]
        and eax, 1
        cmp eax, 0
        je .even_row
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .parse_cols
.even_row:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

.parse_cols:
        ; Print each field padded to COL_WIDTH
        xor ecx, ecx           ; char count in field

.field_loop:
        mov al, [esi]
        cmp al, 0
        je .end_field
        cmp al, 0x0A
        je .end_field
        cmp al, ','
        je .next_field

        ; Print character if within width
        cmp ecx, COL_WIDTH - 1
        jge .field_skip
        movzx ebx, al
        push rcx
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        inc ecx
.field_skip:
        inc esi
        jmp .field_loop

.next_field:
        ; Pad remaining width with spaces
        call .pad_field
        ; Print separator
        push rcx
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        ; Restore row color
        cmp dword [row_num], 1
        jne .nr_color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        jmp .nr_done
.nr_color:
        mov eax, [row_num]
        and eax, 1
        cmp eax, 0
        je .nr_even
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .nr_done
.nr_even:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
.nr_done:
        pop rcx
        xor ecx, ecx
        inc dword [col_num]
        inc esi
        jmp .field_loop

.end_field:
        call .pad_field
        ; Newline
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rax

        ; If header row just printed, print separator line
        cmp dword [row_num], 1
        jne .ef_skip
        push rsi
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov ecx, [col_num]
        inc ecx
        imul ecx, COL_WIDTH + 1
.sep_loop:
        cmp ecx, 0
        jle .sep_done
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        dec ecx
        jmp .sep_loop
.sep_done:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rsi
.ef_skip:
        cmp byte [esi], 0x0A
        jne .done
        inc esi
        jmp .process_line

.done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
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

; Pad current field to COL_WIDTH
; ECX = characters printed so far
.pad_field:
        push rcx
.pf_loop:
        cmp ecx, COL_WIDTH
        jge .pf_done
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rcx
        inc ecx
        jmp .pf_loop
.pf_done:
        pop rcx
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_usage:     db "Usage: csv FILENAME", 0x0A, 0
msg_not_found: db "File not found.", 0x0A, 0

;---------------------------------------
; BSS
;---------------------------------------
arg_buf:   times 256 db 0
file_buf:  times 65536 db 0
row_num:   dd 0
col_num:   dd 0
