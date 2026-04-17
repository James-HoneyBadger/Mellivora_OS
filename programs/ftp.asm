; ftp.asm - FTP client
; Usage: ftp <host> [port]
;
; Interactive FTP client. Commands:
;   user <name>  - Set username (default: anonymous)
;   pass <pass>  - Set password
;   ls           - List directory
;   cd <dir>     - Change directory
;   get <file>   - Download file to screen
;   pwd          - Print working directory
;   quit         - Disconnect and exit
;
; Uses passive mode (PASV) for data transfers.

%include "syscalls.inc"
%include "lib/net.inc"

start:
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz ftp_usage

        ; Parse host
        mov esi, arg_buf
        mov edi, hostname
        xor ecx, ecx
.parse_host:
        mov al, [esi]
        cmp al, ' '
        je .host_done
        cmp al, 0
        je .host_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, 255
        jb .parse_host
.host_done:
        mov byte [edi + ecx], 0
        mov word [port], 21

        ; Optional port
        cmp byte [esi], ' '
        jne .resolve
        inc esi
        xor eax, eax
.pp_loop:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .pp_done
        cmp bl, '9'
        ja .pp_done
        sub bl, '0'
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .pp_loop
.pp_done:
        test eax, eax
        jz .resolve
        mov [port], ax

.resolve:
        mov esi, hostname
        call net_dns
        test eax, eax
        jz ftp_dns_fail
        mov [server_ip], eax

        ; Connect control channel
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je ftp_sock_fail
        mov [ctrl_fd], eax

        mov eax, [ctrl_fd]
        mov ebx, [server_ip]
        movzx ecx, word [port]
        call net_connect
        cmp eax, -1
        je ftp_conn_fail

        ; Read banner
        call ftp_recv_response

        ; Auto-login as anonymous
        mov esi, cmd_user_anon
        call ftp_send_cmd
        call ftp_recv_response

        mov esi, cmd_pass_anon
        call ftp_send_cmd
        call ftp_recv_response

        mov eax, SYS_PRINT
        mov ebx, msg_connected
        int 0x80

        ; Main command loop
.cmd_loop:
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80

        ; Read user input
        mov edi, input_buf
        mov ecx, 255
        call read_line
        test eax, eax
        jz .cmd_loop

        ; Parse command
        mov esi, input_buf
        ; Strip trailing newline
        call strip_crlf

        ; Match commands
        mov edi, cmd_quit
        call starts_with
        jc .do_quit

        mov edi, cmd_ls_str
        call starts_with
        jc .do_list

        mov edi, cmd_cd_str
        call starts_with
        jc .do_cd

        mov edi, cmd_get_str
        call starts_with
        jc .do_get

        mov edi, cmd_pwd_str
        call starts_with
        jc .do_pwd

        mov edi, cmd_user_str
        call starts_with
        jc .do_user

        mov edi, cmd_pass_str
        call starts_with
        jc .do_pass

        mov eax, SYS_PRINT
        mov ebx, msg_unknown
        int 0x80
        jmp .cmd_loop

; --- Commands ---

.do_quit:
        mov esi, ftp_quit
        call ftp_send_cmd
        call ftp_recv_response
        mov eax, [ctrl_fd]
        call net_close
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.do_pwd:
        mov esi, ftp_pwd
        call ftp_send_cmd
        call ftp_recv_response
        jmp .cmd_loop

.do_user:
        ; Build USER command
        mov esi, input_buf
        add esi, 5             ; skip "user "
        mov edi, ftp_cmd_buf
        mov dword [edi], 'USER'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        call copy_to_edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        mov esi, ftp_cmd_buf
        call ftp_send_cmd
        call ftp_recv_response
        jmp .cmd_loop

.do_pass:
        mov esi, input_buf
        add esi, 5
        mov edi, ftp_cmd_buf
        mov dword [edi], 'PASS'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        call copy_to_edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        mov esi, ftp_cmd_buf
        call ftp_send_cmd
        call ftp_recv_response
        jmp .cmd_loop

.do_cd:
        mov esi, input_buf
        add esi, 3             ; skip "cd "
        mov edi, ftp_cmd_buf
        mov byte [edi], 'C'
        mov byte [edi+1], 'W'
        mov byte [edi+2], 'D'
        mov byte [edi+3], ' '
        add edi, 4
        call copy_to_edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        mov esi, ftp_cmd_buf
        call ftp_send_cmd
        call ftp_recv_response
        jmp .cmd_loop

.do_list:
        ; Enter PASV mode
        call ftp_pasv
        test eax, eax
        jz .pasv_fail

        ; Send LIST
        mov esi, ftp_list
        call ftp_send_cmd

        ; Receive data from data channel
        call ftp_recv_data
        ; Read control response
        call ftp_recv_response
        jmp .cmd_loop

.do_get:
        ; Enter PASV mode
        call ftp_pasv
        test eax, eax
        jz .pasv_fail

        ; Build RETR command
        mov esi, input_buf
        add esi, 4             ; skip "get "
        mov edi, ftp_cmd_buf
        mov dword [edi], 'RETR'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        call copy_to_edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        mov esi, ftp_cmd_buf
        call ftp_send_cmd

        ; Receive data
        call ftp_recv_data
        ; Read control response
        call ftp_recv_response
        jmp .cmd_loop

.pasv_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_pasv_fail
        int 0x80
        jmp .cmd_loop

; --- FTP helpers ---

; ftp_send_cmd: Send FTP command (ESI = string with CRLF)
ftp_send_cmd:
        push rsi
        ; Get length
        xor ecx, ecx
        mov edi, esi
.fsc_len:
        cmp byte [edi + ecx], 0
        je .fsc_go
        inc ecx
        jmp .fsc_len
.fsc_go:
        mov eax, [ctrl_fd]
        mov ebx, esi
        call net_send
        pop rsi
        ret

; ftp_recv_response: Receive and print control response
ftp_recv_response:
        mov dword [retry_count], 0
.frr_loop:
        mov eax, [ctrl_fd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv

        cmp eax, 0
        jle .frr_retry

        ; Null-terminate and print
        mov byte [resp_buf + eax], 0
        push rax
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        pop rax

        ; Check if this is a complete response (3-digit code + space)
        cmp eax, 4
        jl .frr_loop
        cmp byte [resp_buf + 3], ' '
        je .frr_done
        cmp byte [resp_buf + 3], '-'
        je .frr_loop        ; Multi-line response, keep reading
        jmp .frr_done
.frr_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 200
        jge .frr_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .frr_loop
.frr_done:
        ret

; ftp_pasv: Enter passive mode, connect data socket
; Returns EAX = 1 on success, 0 on failure
ftp_pasv:
        mov esi, ftp_pasv_cmd
        call ftp_send_cmd

        ; Receive PASV response: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
        mov dword [retry_count], 0
.fp_recv:
        mov eax, [ctrl_fd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv
        cmp eax, 0
        jle .fp_recv_retry
        mov byte [resp_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        jmp .fp_parse

.fp_recv_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 200
        jge .fp_fail
        mov eax, SYS_YIELD
        int 0x80
        jmp .fp_recv

.fp_parse:
        ; Find '(' in response
        mov esi, resp_buf
.fp_find_paren:
        lodsb
        cmp al, 0
        je .fp_fail
        cmp al, '('
        jne .fp_find_paren

        ; Parse 6 comma-separated numbers
        xor ecx, ecx              ; count
        lea edi, [pasv_nums]
.fp_parse_num:
        xor eax, eax
.fp_digit:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .fp_not_digit
        cmp bl, '9'
        ja .fp_not_digit
        sub bl, '0'
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .fp_digit
.fp_not_digit:
        mov [edi + ecx*4], eax
        inc ecx
        cmp ecx, 6
        jge .fp_got_nums
        inc esi                    ; skip ',' or ')'
        jmp .fp_parse_num

.fp_got_nums:
        ; Build IP: h1.h2.h3.h4
        mov eax, [pasv_nums]
        mov ebx, [pasv_nums+4]
        shl ebx, 8
        or eax, ebx
        mov ebx, [pasv_nums+8]
        shl ebx, 16
        or eax, ebx
        mov ebx, [pasv_nums+12]
        shl ebx, 24
        or eax, ebx
        mov [data_ip], eax

        ; Build port: p1*256 + p2
        mov eax, [pasv_nums+16]
        shl eax, 8
        add eax, [pasv_nums+20]
        mov [data_port], ax

        ; Connect data socket
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .fp_fail
        mov [data_fd], eax

        mov eax, [data_fd]
        mov ebx, [data_ip]
        movzx ecx, word [data_port]
        call net_connect
        cmp eax, -1
        je .fp_data_fail

        mov eax, 1
        ret

.fp_data_fail:
        mov eax, [data_fd]
        call net_close
.fp_fail:
        xor eax, eax
        ret

; ftp_recv_data: receive all data from data socket and print, then close
ftp_recv_data:
        mov dword [retry_count], 0
.frd_loop:
        mov eax, [data_fd]
        mov ebx, data_buf
        mov ecx, 2048
        call net_recv

        cmp eax, -1
        je .frd_done
        cmp eax, 0
        je .frd_retry

        mov dword [retry_count], 0
        ; Print data
        mov byte [data_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, data_buf
        int 0x80
        jmp .frd_loop

.frd_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 300
        jge .frd_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .frd_loop

.frd_done:
        mov eax, [data_fd]
        call net_close
        ret

; strip_crlf: strip trailing CR/LF from input_buf
strip_crlf:
        mov edi, input_buf
        xor ecx, ecx
.sc_len:
        cmp byte [edi + ecx], 0
        je .sc_strip
        inc ecx
        jmp .sc_len
.sc_strip:
        test ecx, ecx
        jz .sc_done
        dec ecx
        cmp byte [edi + ecx], 0x0A
        je .sc_zero
        cmp byte [edi + ecx], 0x0D
        je .sc_zero
        jmp .sc_done
.sc_zero:
        mov byte [edi + ecx], 0
        jmp .sc_strip
.sc_done:
        ret

; starts_with: check if input_buf starts with string at EDI
; Returns CF set if match
starts_with:
        push rsi
        mov esi, input_buf
.sw_loop:
        mov al, [edi]
        cmp al, 0
        je .sw_match
        cmp al, [esi]
        jne .sw_no
        inc esi
        inc edi
        jmp .sw_loop
.sw_match:
        pop rsi
        stc
        ret
.sw_no:
        pop rsi
        clc
        ret

; copy_to_edi: copy from ESI until null
copy_to_edi:
        lodsb
        cmp al, 0
        je .cte_done
        stosb
        jmp copy_to_edi
.cte_done:
        ret

; --- read_line: read a line from keyboard into EDI, max ECX chars ---
; Returns EAX = length
read_line:
        push rbx
        push rcx
        push rdi
        xor edx, edx            ; count
.rl_loop:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 0x0D
        je .rl_done
        cmp al, 0x0A
        je .rl_done
        cmp al, 0x08
        je .rl_bs
        cmp al, 0x7F
        je .rl_bs
        cmp edx, ecx
        jge .rl_loop
        mov [edi + edx], al
        inc edx
        push rdx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rdx
        jmp .rl_loop
.rl_bs:
        test edx, edx
        jz .rl_loop
        dec edx
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        pop rdx
        jmp .rl_loop
.rl_done:
        mov byte [edi + edx], 0
        push rdx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rdx
        mov eax, edx
        pop rdi
        pop rcx
        pop rbx
        ret

; --- Error handlers ---
ftp_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
ftp_dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
ftp_sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
ftp_conn_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn
        int 0x80
        mov eax, [ctrl_fd]
        call net_close
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; Strings
msg_usage:      db "Usage: ftp <host> [port]", 0x0A, 0
msg_dns:        db "Error: DNS resolution failed", 0x0A, 0
msg_sock:       db "Error: Could not create socket", 0x0A, 0
msg_conn:       db "Error: Connection failed", 0x0A, 0
msg_connected:  db "Connected. Type 'quit' to exit.", 0x0A, 0
msg_unknown:    db "Unknown command. Try: ls, cd, get, pwd, user, pass, quit", 0x0A, 0
msg_pasv_fail:  db "Error: PASV mode failed", 0x0A, 0
prompt_str:     db "ftp> ", 0

cmd_quit:       db "quit", 0
cmd_ls_str:     db "ls", 0
cmd_cd_str:     db "cd ", 0
cmd_get_str:    db "get ", 0
cmd_pwd_str:    db "pwd", 0
cmd_user_str:   db "user ", 0
cmd_pass_str:   db "pass ", 0

ftp_quit:       db "QUIT", 0x0D, 0x0A, 0
ftp_pwd:        db "PWD", 0x0D, 0x0A, 0
ftp_list:       db "LIST", 0x0D, 0x0A, 0
ftp_pasv_cmd:   db "PASV", 0x0D, 0x0A, 0
cmd_user_anon:  db "USER anonymous", 0x0D, 0x0A, 0
cmd_pass_anon:  db "PASS guest@mellivora", 0x0D, 0x0A, 0

; Data
hostname:       times 256 db 0
port:           dw 21
server_ip:      dd 0
ctrl_fd:        dd 0
data_fd:        dd 0
data_ip:        dd 0
data_port:      dw 0
pasv_nums:      times 6 dd 0
retry_count:    dd 0
ftp_cmd_buf:    times 512 db 0
arg_buf:        times 512 db 0
input_buf:      times 512 db 0
resp_buf:       times 2048 db 0
data_buf:       times 2048 db 0
