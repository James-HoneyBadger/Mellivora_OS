; file.asm - Identify file type by magic bytes
; Usage: file <filename>

%include "syscalls.inc"

start:
        ; Get filename from arguments
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle .usage

        ; Print filename prefix
        mov eax, SYS_PRINT
        mov ebx, args_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_colon
        int 0x80

        ; Get file size via SYS_STAT
        mov eax, SYS_STAT
        mov ebx, args_buf
        int 0x80
        cmp eax, -1
        je .not_found
        mov [file_size], eax

        ; Empty file?
        cmp eax, 0
        je .empty

        ; Read file into buffer
        mov eax, SYS_FREAD
        mov ebx, args_buf
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        je .not_found

        ; --- Check magic bytes ---

        ; ELF: 7F 45 4C 46
        cmp dword [file_buf], 0x464C457F
        je .elf

        ; BMP: 42 4D
        cmp word [file_buf], 0x4D42
        je .bmp

        ; PNG: 89 50 4E 47
        cmp dword [file_buf], 0x474E5089
        je .png

        ; GIF: 47 49 46 38
        cmp dword [file_buf], 0x38464947
        je .gif

        ; RIFF/WAV: 52 49 46 46
        cmp dword [file_buf], 0x46464952
        je .riff

        ; PK (ZIP): 50 4B 03 04
        cmp dword [file_buf], 0x04034B50
        je .zip

        ; Gzip: 1F 8B
        cmp word [file_buf], 0x8B1F
        je .gzip

        ; PDF: %PDF
        cmp dword [file_buf], 0x46445025
        je .pdf

        ; MZ (DOS/PE): 4D 5A
        cmp word [file_buf], 0x5A4D
        je .mz

        ; Flat binary at 0x200000 (Mellivora program): starts with E9 (jmp)
        cmp byte [file_buf], 0xE9
        je .mellivora

        ; Check for text file (printable ASCII)
        call .check_text
        jc .text

        ; Unknown binary
        mov eax, SYS_PRINT
        mov ebx, msg_data
        int 0x80
        jmp .exit

.elf:
        mov eax, SYS_PRINT
        mov ebx, msg_elf
        int 0x80
        jmp .print_size

.bmp:
        mov eax, SYS_PRINT
        mov ebx, msg_bmp
        int 0x80
        jmp .print_size

.png:
        mov eax, SYS_PRINT
        mov ebx, msg_png
        int 0x80
        jmp .print_size

.gif:
        mov eax, SYS_PRINT
        mov ebx, msg_gif
        int 0x80
        jmp .print_size

.riff:
        ; Check for WAVE subtype at offset 8
        cmp dword [file_buf + 8], 0x45564157  ; "WAVE"
        jne .riff_other
        mov eax, SYS_PRINT
        mov ebx, msg_wav
        int 0x80
        jmp .print_size
.riff_other:
        mov eax, SYS_PRINT
        mov ebx, msg_riff
        int 0x80
        jmp .print_size

.zip:
        mov eax, SYS_PRINT
        mov ebx, msg_zip
        int 0x80
        jmp .print_size

.gzip:
        mov eax, SYS_PRINT
        mov ebx, msg_gzip
        int 0x80
        jmp .print_size

.pdf:
        mov eax, SYS_PRINT
        mov ebx, msg_pdf
        int 0x80
        jmp .print_size

.mz:
        mov eax, SYS_PRINT
        mov ebx, msg_mz
        int 0x80
        jmp .print_size

.mellivora:
        mov eax, SYS_PRINT
        mov ebx, msg_mellivora
        int 0x80
        jmp .print_size

.text:
        mov eax, SYS_PRINT
        mov ebx, msg_text
        int 0x80
        jmp .print_size

.empty:
        mov eax, SYS_PRINT
        mov ebx, msg_empty
        int 0x80
        jmp .exit

.not_found:
        mov eax, SYS_PRINT
        mov ebx, msg_not_found
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.print_size:
        ; Print " (N bytes)\n"
        mov eax, SYS_PRINT
        mov ebx, msg_open_paren
        int 0x80
        mov eax, [file_size]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80

.exit:
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

;---------------------------------------
; Check if file_buf contains mostly text
; Sets CF if text, clears if binary
;---------------------------------------
.check_text:
        mov esi, file_buf
        mov ecx, [file_size]
        cmp ecx, 512
        jbe .ct_go
        mov ecx, 512           ; Check first 512 bytes
.ct_go:
        xor edx, edx           ; non-printable count
.ct_loop:
        lodsb
        cmp al, 0x0A            ; LF
        je .ct_ok
        cmp al, 0x0D            ; CR
        je .ct_ok
        cmp al, 0x09            ; TAB
        je .ct_ok
        cmp al, 0x1B            ; ESC (ANSI sequences)
        je .ct_ok
        cmp al, 32
        jb .ct_bin
        cmp al, 126
        ja .ct_bin
.ct_ok:
        dec ecx
        jnz .ct_loop
        stc
        ret
.ct_bin:
        clc
        ret

; Strings
msg_usage:      db "Usage: file <filename>", 0x0A, 0
msg_colon:      db ": ", 0
msg_not_found:  db "cannot open (No such file)", 0x0A, 0
msg_empty:      db "empty", 0x0A, 0
msg_elf:        db "ELF 64-bit executable", 0
msg_bmp:        db "BMP image", 0
msg_png:        db "PNG image", 0
msg_gif:        db "GIF image", 0
msg_wav:        db "RIFF WAVE audio", 0
msg_riff:       db "RIFF data", 0
msg_zip:        db "ZIP archive", 0
msg_gzip:       db "gzip compressed data", 0
msg_pdf:        db "PDF document", 0
msg_mz:         db "DOS/PE executable", 0
msg_mellivora:  db "Mellivora flat binary", 0
msg_text:       db "ASCII text", 0
msg_data:       db "data", 0x0A, 0
msg_open_paren: db " (", 0
msg_bytes:      db " bytes)", 0x0A, 0

; Data
args_buf:       times 256 db 0
file_size:      dd 0
file_buf:       times 4096 db 0         ; Read first 4KB for magic detection
