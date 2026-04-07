; sort.asm - Line sort utility for Mellivora OS
; Usage: sort <filename>
; Reads a text file, sorts lines alphabetically, displays result
;
%include "syscalls.inc"

MAX_LINES       equ 256
MAX_LINE_LEN    equ 128

start:
        ; Get filename
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Read file
        mov eax, SYS_FREAD
        mov ebx, args_buf
        mov ecx, file_buffer
        int 0x80
        cmp eax, 0
        jl .file_err
        mov [file_size], eax

        ; Parse into lines
        mov esi, file_buffer
        xor ecx, ecx           ; line count
        mov dword [line_count], 0

.parse_lines:
        mov eax, esi
        sub eax, file_buffer
        cmp eax, [file_size]
        jge .parse_done

        ; Record pointer to this line
        mov eax, [line_count]
        cmp eax, MAX_LINES
        jge .parse_done
        mov [line_ptrs + eax * 4], esi
        inc dword [line_count]

        ; Find end of line
.find_eol:
        movzx eax, byte [esi]
        cmp al, 0x0A
        je .found_eol
        cmp al, 0
        je .parse_done
        inc esi
        jmp .find_eol

.found_eol:
        mov byte [esi], 0       ; null-terminate line
        inc esi
        jmp .parse_lines

.parse_done:
        ; Bubble sort the lines
        mov dword [sorted], 0

.sort_pass:
        mov dword [sorted], 1   ; assume sorted
        mov ecx, [line_count]
        dec ecx
        cmp ecx, 0
        jle .print_result
        xor ebx, ebx           ; index

.sort_loop:
        cmp ebx, ecx
        jge .check_sorted

        ; Compare line[ebx] with line[ebx+1]
        mov esi, [line_ptrs + ebx * 4]
        mov edi, [line_ptrs + ebx * 4 + 4]
        call strcmp_nocase
        cmp eax, 0
        jle .no_swap

        ; Swap
        mov eax, [line_ptrs + ebx * 4]
        mov edx, [line_ptrs + ebx * 4 + 4]
        mov [line_ptrs + ebx * 4], edx
        mov [line_ptrs + ebx * 4 + 4], eax
        mov dword [sorted], 0

.no_swap:
        inc ebx
        jmp .sort_loop

.check_sorted:
        cmp dword [sorted], 1
        jne .sort_pass

.print_result:
        ; Print sorted lines
        xor ebx, ebx
.print_loop:
        cmp ebx, [line_count]
        jge .done

        mov eax, SYS_PRINT
        push ebx
        mov ebx, [line_ptrs + ebx * 4]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop ebx

        inc ebx
        jmp .print_loop

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
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; Case-insensitive string compare
; ESI, EDI = strings
; Returns EAX: <0, 0, >0
;=======================================================================
strcmp_nocase:
        push ebx
        push ecx
        push esi
        push edi
.sc_loop:
        movzx eax, byte [esi]
        movzx ebx, byte [edi]
        ; To lowercase
        cmp al, 'A'
        jl .sc_no_lower1
        cmp al, 'Z'
        jg .sc_no_lower1
        add al, 32
.sc_no_lower1:
        cmp bl, 'A'
        jl .sc_no_lower2
        cmp bl, 'Z'
        jg .sc_no_lower2
        add bl, 32
.sc_no_lower2:
        sub eax, ebx
        jne .sc_done
        cmp byte [esi], 0
        je .sc_done
        inc esi
        inc edi
        jmp .sc_loop
.sc_done:
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret

; Data
msg_usage:      db "Usage: sort <filename>", 0x0A, 0
msg_file_err:   db "Error: Cannot open file", 0x0A, 0
args_buf:       times 256 db 0
file_size:      dd 0
line_count:     dd 0
sorted:         dd 0
line_ptrs:      times MAX_LINES dd 0
file_buffer:    times 32768 db 0
