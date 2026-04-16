; split.asm - Split a file into smaller parts [HBU]
; Usage: split [-l lines] [-b bytes] <filename> [prefix]
; Default: split by 1000 lines, prefix "x"
;
%include "syscalls.inc"

SPLIT_MAX_FILE  equ 65536
DEFAULT_LINES   equ 1000

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle show_usage

        ; Parse arguments
        mov esi, args_buf
        mov dword [split_lines], DEFAULT_LINES
        mov dword [split_bytes], 0
        mov byte [prefix_buf], 'x'
        mov byte [prefix_buf + 1], 0
        mov dword [prefix_len], 1

.parse_loop:
        cmp byte [esi], 0
        je .check_input
        cmp byte [esi], ' '
        jne .not_space
        inc esi
        jmp .parse_loop
.not_space:
        cmp byte [esi], '-'
        jne .got_filename

        ; Option flag
        inc esi
        cmp byte [esi], 'l'
        je .opt_lines
        cmp byte [esi], 'b'
        je .opt_bytes
        jmp show_usage

.opt_lines:
        inc esi
        call skip_spaces
        call parse_num
        mov [split_lines], eax
        mov dword [split_bytes], 0
        jmp .parse_loop

.opt_bytes:
        inc esi
        call skip_spaces
        call parse_num
        mov [split_bytes], eax
        jmp .parse_loop

.got_filename:
        cmp byte [filename_buf], 0
        jne .got_prefix
        ; First non-option arg = filename
        mov edi, filename_buf
.copy_fn:
        lodsb
        cmp al, ' '
        je .fn_done
        cmp al, 0
        je .fn_end
        stosb
        jmp .copy_fn
.fn_done:
        mov byte [edi], 0
        jmp .parse_loop
.fn_end:
        mov byte [edi], 0
        jmp .check_input

.got_prefix:
        ; Second non-option arg = prefix
        mov edi, prefix_buf
        xor ecx, ecx
.copy_pfx:
        lodsb
        cmp al, ' '
        je .pfx_done
        cmp al, 0
        je .pfx_end
        stosb
        inc ecx
        jmp .copy_pfx
.pfx_done:
        mov byte [edi], 0
        mov [prefix_len], ecx
        jmp .parse_loop
.pfx_end:
        mov byte [edi], 0
        mov [prefix_len], ecx
        jmp .check_input

.check_input:
        cmp byte [filename_buf], 0
        je show_usage

        ; Read input file
        mov eax, SYS_FREAD
        mov ebx, filename_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jl file_error
        mov [file_size], eax

        ; Choose split mode
        cmp dword [split_bytes], 0
        jne .split_bytes

        ; =============================================
        ; Split by lines
        ; =============================================
        mov dword [cur_offset], 0
        mov dword [part_num], 0

.line_part:
        mov eax, [cur_offset]
        cmp eax, [file_size]
        jge .done

        ; Find end of this chunk: count split_lines newlines
        mov esi, file_buf
        add esi, [cur_offset]
        mov edx, [file_size]
        sub edx, [cur_offset]  ; remaining bytes
        xor ecx, ecx           ; line counter
        xor ebx, ebx           ; bytes in this chunk

.scan_lines:
        cmp ebx, edx
        jge .chunk_found
        cmp byte [esi + ebx], 0x0A
        jne .sl_next
        inc ecx
        cmp ecx, [split_lines]
        jge .sl_include_nl
.sl_next:
        inc ebx
        jmp .scan_lines
.sl_include_nl:
        inc ebx                 ; include the newline
.chunk_found:
        ; ebx = chunk size
        mov [chunk_size], ebx

        ; Copy chunk to chunk_buf
        mov esi, file_buf
        add esi, [cur_offset]
        mov edi, chunk_buf
        mov ecx, ebx
        rep movsb

        ; Build output filename
        call build_outname

        ; Write file
        mov eax, SYS_FWRITE
        mov ebx, outname_buf
        mov ecx, chunk_buf
        mov edx, [chunk_size]
        mov esi, 0
        int 0x80

        ; Print output filename
        mov eax, SYS_PRINT
        mov ebx, outname_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        ; Advance
        mov eax, [chunk_size]
        add [cur_offset], eax
        inc dword [part_num]
        jmp .line_part

        ; =============================================
        ; Split by bytes
        ; =============================================
.split_bytes:
        mov dword [cur_offset], 0
        mov dword [part_num], 0

.byte_part:
        mov eax, [cur_offset]
        cmp eax, [file_size]
        jge .done

        ; Determine chunk size
        mov ecx, [split_bytes]
        mov edx, [file_size]
        sub edx, [cur_offset]
        cmp ecx, edx
        jle .bs_ok
        mov ecx, edx
.bs_ok:
        mov [chunk_size], ecx

        ; Copy chunk to chunk_buf
        mov esi, file_buf
        add esi, [cur_offset]
        mov edi, chunk_buf
        rep movsb

        ; Build output filename
        call build_outname

        ; Write file
        mov eax, SYS_FWRITE
        mov ebx, outname_buf
        mov ecx, chunk_buf
        mov edx, [chunk_size]
        mov esi, 0
        int 0x80

        ; Print output filename
        mov eax, SYS_PRINT
        mov ebx, outname_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, newline
        int 0x80

        ; Advance
        mov eax, [chunk_size]
        add [cur_offset], eax
        inc dword [part_num]
        jmp .byte_part

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; =============================================
; Build output filename: prefix + two-letter suffix (aa..zz)
; Uses [part_num] for suffix generation
; =============================================
build_outname:
        PUSHALL
        ; Copy prefix
        mov esi, prefix_buf
        mov edi, outname_buf
        mov ecx, [prefix_len]
        rep movsb
        ; Compute suffix: part_num / 26 = first, part_num % 26 = second
        mov eax, [part_num]
        xor edx, edx
        mov ecx, 26
        div ecx
        add al, 'a'
        stosb
        add dl, 'a'
        mov [edi], dl
        mov byte [edi + 1], 0
        POPALL
        ret

; =============================================
; Helpers
; =============================================
skip_spaces:
        cmp byte [esi], ' '
        jne .ret
        inc esi
        jmp skip_spaces
.ret:
        ret

parse_num:
        xor eax, eax
.loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .done
        cmp cl, '9'
        ja .done
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .loop
.done:
        ret

show_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

file_error:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; =============================================
; Data
; =============================================
msg_usage:      db "Usage: split [-l lines] [-b bytes] <file> [prefix]", 0x0A
                db "Default: split by 1000 lines, prefix 'x'", 0x0A, 0
msg_err:        db "Error: cannot read file", 0x0A, 0
newline:        db 0x0A, 0

args_buf:       times 256 db 0
filename_buf:   times 256 db 0
prefix_buf:     times 32 db 0
prefix_len:     dd 0
outname_buf:    times 64 db 0
split_lines:    dd 0
split_bytes:    dd 0
file_size:      dd 0
cur_offset:     dd 0
part_num:       dd 0
chunk_size:     dd 0
file_buf:       times SPLIT_MAX_FILE db 0
chunk_buf:      times SPLIT_MAX_FILE db 0
