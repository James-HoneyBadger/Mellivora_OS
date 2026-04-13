; touch.asm - Create an empty file (like Unix touch)
; Usage: touch <filename>

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, argbuf
        int 0x80
        cmp eax, 0
        je .usage

        ; Check if file exists already
        mov eax, SYS_STAT
        mov ebx, argbuf
        int 0x80
        cmp eax, -1
        jne .exists

        ; Create empty text file
        mov eax, SYS_FWRITE
        mov ebx, argbuf
        mov ecx, empty         ; empty buffer
        xor edx, edx           ; size = 0
        mov esi, FTYPE_TEXT
        int 0x80
        cmp eax, -1
        je .err
        jmp .done

.exists:
        ; File already exists, nothing to do
        jmp .done

.err:
        mov eax, SYS_PRINT
        mov ebx, msg_err
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        jmp .done

msg_usage:      db "Usage: touch <filename>", 10, 0
msg_err:        db "Error: could not create file", 10, 0

argbuf:         times 256 db 0
empty:          db 0
