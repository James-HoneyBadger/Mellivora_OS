; tee.asm - Copy stdin/file to stdout and a file [HBU]
; Usage: tee OUTFILE         (read from stdin, write to OUTFILE)
;        tee INFILE OUTFILE  (read from INFILE, write to OUTFILE)
;
%include "syscalls.inc"

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Parse first argument
        mov esi, args_buf
        mov edi, in_filename
        call parse_arg
        call skip_spaces

        ; If a second arg exists: in_filename=INFILE, out_filename=OUTFILE (legacy)
        ; If no second arg: in_filename is the OUTFILE, read from stdin
        cmp byte [esi], 0
        je .read_stdin

        ; Two-arg mode: in_filename=INFILE, next arg=OUTFILE
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
        jmp .write_output

.read_stdin:
        ; One-arg mode: in_filename is actually the output file
        ; Copy in_filename -> out_filename
        mov esi, in_filename
        mov edi, out_filename
.copy_name:
        lodsb
        stosb
        cmp al, 0
        jne .copy_name

        ; Read from stdin
        mov eax, SYS_STDIN_READ
        mov ebx, file_buf
        int 0x80
        cmp eax, 0
        jl .no_stdin
        mov [file_size], eax

.write_output:
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

.no_stdin:
        mov eax, SYS_PRINT
        mov ebx, msg_no_stdin
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

msg_usage:      db "Usage: tee OUTFILE  or  tee INFILE OUTFILE", 0x0A, 0
msg_no_stdin:   db "Error: No stdin input available", 0x0A, 0
msg_read_err:   db "Error: Cannot read input file", 0x0A, 0
msg_write_err:  db "Error: Cannot write output file", 0x0A, 0

args_buf:       times 256 db 0
in_filename:    times 128 db 0
out_filename:   times 128 db 0
file_size:      dd 0
file_buf:       times 16384 db 0
