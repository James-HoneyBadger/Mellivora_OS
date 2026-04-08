; tee.asm - Copy file content to stdout and a file [HBU]
; Usage: tee INPUTFILE OUTPUTFILE
; Reads INPUTFILE, prints it, and writes the same bytes to OUTPUTFILE.
;
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse: input output
        mov esi, args_buf
        mov edi, in_filename
        call parse_arg
        call skip_spaces
        cmp byte [esi], 0
        je .usage
        mov edi, out_filename
        call parse_arg

        ; Read input file
        mov eax, SYS_FREAD
        mov ebx, in_filename
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .read_err
        mov [file_size], eax

        ; Echo to stdout
        mov esi, file_buf
        mov ecx, [file_size]
.print_loop:
        cmp ecx, 0
        je .write_out
        movzx ebx, byte [esi]
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        dec ecx
        jmp .print_loop

.write_out:
        ; Write output file as text
        mov eax, SYS_FWRITE
        mov ebx, out_filename
        mov ecx, file_buf
        mov edx, [file_size]
        mov esi, FTYPE_TEXT
        int 0x80
        cmp eax, 0
        jl .write_err

        mov eax, SYS_EXIT
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.read_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_read_err
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.write_err:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_write_err
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

; Copy one space-delimited argument from ESI to EDI.
parse_arg:
.pa_loop:
        lodsb
        cmp al, ' '
        je .pa_done
        cmp al, 0
        je .pa_end
        stosb
        jmp .pa_loop
.pa_end:
        dec esi
.pa_done:
        mov byte [edi], 0
        ret

skip_spaces:
.ss_loop:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp .ss_loop
.ss_done:
        ret

msg_usage:      db "Usage: tee INPUTFILE OUTPUTFILE", 0x0A, 0
msg_read_err:   db "Error: Cannot read input file", 0x0A, 0
msg_write_err:  db "Error: Cannot write output file", 0x0A, 0

args_buf:       times 256 db 0
in_filename:    times 128 db 0
out_filename:   times 128 db 0
file_size:      dd 0
file_buf:       times 16384 db 0
