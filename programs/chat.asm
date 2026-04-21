; chat.asm - Simple LAN UDP chat
; Usage: chat [nickname]
; Binds to UDP port 5555, broadcasts to 255.255.255.255:5555
; Press Enter to send messages, Ctrl+C to exit

%include "syscalls.inc"
%include "lib/net.inc"

CHAT_PORT       equ 5555
BROADCAST_IP    equ 0xFFFFFFFF ; 255.255.255.255
RECV_BUF_SIZE   equ 512
SEND_BUF_SIZE   equ 512
NICKNAME_SIZE   equ 32

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Default nickname
        mov esi, default_nick
        mov edi, nickname
        mov ecx, NICKNAME_SIZE
        rep movsb

        ; Check for custom nickname
        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .nick_done

        mov edi, nickname
        xor ecx, ecx
.copy_nick:
        mov al, [esi]
        cmp al, ' '
        je .nick_done
        cmp al, 0
        je .nick_done
        cmp ecx, NICKNAME_SIZE - 2
        jge .nick_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_nick
.nick_done:

        ; Create UDP socket
        mov eax, SYS_SOCKET
        mov ebx, 2              ; UDP
        int 0x80
        cmp eax, -1
        je .sock_fail
        mov [fd], eax

        ; Bind to port
        mov eax, SYS_BIND
        mov ebx, [fd]
        mov ecx, CHAT_PORT
        int 0x80
        cmp eax, -1
        je .bind_fail

        ; Print welcome banner
        mov eax, SYS_PRINT
        mov ebx, msg_welcome
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, nickname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_port
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_help
        int 0x80

.chat_loop:
        ; Try to receive a message (non-blocking via short timeout)
        mov eax, SYS_RECV
        mov ebx, [fd]
        mov ecx, recv_buf
        mov edx, RECV_BUF_SIZE - 1
        int 0x80
        cmp eax, -1
        je .check_input
        test eax, eax
        jz .check_input

        ; Got a message — null-terminate and print
        mov byte [recv_buf + eax], 0
        ; Print newline if cursor not at column 0 (best-effort)
        mov eax, SYS_PUTCHAR
        mov ebx, 13
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, recv_buf
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.check_input:
        ; Check for user input (single line)
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, SEND_BUF_SIZE - 64
        int 0x80
        cmp eax, -1
        je .chat_loop
        test eax, eax
        jz .chat_loop

        ; Build message: "<nickname>: message"
        mov edi, send_buf
        mov esi, nickname
.copy_nk:
        lodsb
        test al, al
        jz .after_nick
        stosb
        jmp .copy_nk
.after_nick:
        mov word [edi], ': '
        add edi, 2
        mov esi, input_buf
.copy_msg:
        lodsb
        cmp al, 10
        je .msg_done
        cmp al, 13
        je .msg_done
        test al, al
        jz .msg_done
        stosb
        jmp .copy_msg
.msg_done:
        mov byte [edi], 0
        sub edi, send_buf
        mov [send_len], edi

        ; Broadcast the message
        mov eax, SYS_CONNECT
        mov ebx, [fd]
        mov ecx, BROADCAST_IP
        mov edx, CHAT_PORT
        int 0x80

        mov eax, SYS_SEND
        mov ebx, [fd]
        mov ecx, send_buf
        mov edx, [send_len]
        int 0x80

        jmp .chat_loop

.sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock_fail
        int 0x80
        jmp .exit

.bind_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_bind_fail
        int 0x80

.exit:
        mov eax, SYS_SOCKCLOSE
        mov ebx, [fd]
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

default_nick:   db "anonymous", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

msg_welcome:    db "=== LAN Chat - nick: ", 0
msg_port:       db " on UDP:", 0
msg_help:       db "5555. Type and press Enter to send, Ctrl+C to quit.", 10, 0
msg_sock_fail:  db "chat: cannot create UDP socket", 10, 0
msg_bind_fail:  db "chat: cannot bind to port 5555", 10, 0

fd:             dd -1
send_len:       dd 0
nickname:       times NICKNAME_SIZE db 0
arg_buf:        times 256 db 0
input_buf:      times SEND_BUF_SIZE db 0
send_buf:       times SEND_BUF_SIZE db 0
recv_buf:       times RECV_BUF_SIZE db 0
