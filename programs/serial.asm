; serial.asm - Serial port test utility for Mellivora OS
; Tests bidirectional COM1 communication.
;
; Usage:
;   serial              - Send greeting, then echo serial input to screen
;   serial send <text>  - Send text out the serial port
;
; Connect with: qemu-system-x86_64 ... -serial tcp::4555,server,nowait
; Then:         nc localhost 4555
;
%include "syscalls.inc"

start:
        ; Get command-line arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        mov [arg_len], eax

        ; Check for "send" subcommand
        cmp eax, 0
        je .interactive

        ; Compare first 4 chars with "send"
        cmp dword [arg_buf], 'send'
        jne .interactive
        cmp byte [arg_buf + 4], ' '
        jne .interactive

        ; --- "serial send <text>" mode ---
        lea ebx, [arg_buf + 5]
        mov eax, SYS_SERIAL
        int 0x80
        ; Also send a newline
        mov ebx, crlf_str
        mov eax, SYS_SERIAL
        int 0x80
        ; Print confirmation on screen
        mov eax, SYS_PRINT
        mov ebx, msg_sent
        int 0x80
        jmp .exit

.interactive:
        ; --- Interactive mode: send greeting, echo incoming ---
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80

        ; Send greeting over serial
        mov eax, SYS_SERIAL
        mov ebx, msg_serial_hello
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_waiting
        int 0x80

.poll_loop:
        ; Check keyboard for Escape to quit
        mov eax, SYS_READ_KEY
        int 0x80
        cmp al, 27             ; Escape
        je .exit_msg
        cmp al, 0
        je .check_serial

        ; User typed on keyboard - send it over serial
        movzx ebx, al
        push rbx
        ; Echo char to screen with color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A          ; Green = outgoing
        int 0x80
        pop rbx
        push rbx
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        ; Send to serial (need null-terminated string)
        pop rax
        mov [char_buf], al
        mov byte [char_buf + 1], 0
        mov eax, SYS_SERIAL
        mov ebx, char_buf
        int 0x80
        jmp .poll_loop

.check_serial:
        ; Poll serial port (non-blocking)
        mov eax, SYS_SERIAL_IN
        int 0x80
        cmp eax, -1
        je .no_serial_data

        ; Got a byte from serial - display it
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B          ; Cyan = incoming from serial
        int 0x80
        pop rbx
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        jmp .poll_loop

.no_serial_data:
        ; Small sleep to avoid busy-spin
        mov eax, SYS_SLEEP
        mov ebx, 1             ; 10ms (1 tick)
        int 0x80
        jmp .poll_loop

.exit_msg:
        mov eax, SYS_PRINT
        mov ebx, msg_bye
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; Data
msg_banner:       db 0x0A, "=== Serial Port Test (COM1 @ 115200 8N1) ===", 0x0A, 0
msg_waiting:      db "Type to send over serial (green=out, cyan=in).", 0x0A
                  db "Press Escape to quit.", 0x0A, 0
msg_serial_hello: db "Hello from Mellivora OS!", 13, 10, 0
msg_sent:         db "Sent to serial port.", 0x0A, 0
msg_bye:          db 0x0A, "Serial test ended.", 0x0A, 0
crlf_str:         db 13, 10, 0
char_buf:         db 0, 0

section .bss
arg_buf:    resb 512
arg_len:    resd 1
