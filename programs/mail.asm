; mail.asm - Email client (SMTP send + POP3 receive)
; Usage: mail <server> [command]
;
; Commands (interactive):
;   compose  - Compose and send an email (SMTP port 25)
;   inbox    - List messages (POP3 port 110)
;   read N   - Read message N (POP3 port 110)
;   delete N - Delete message N (POP3 port 110)
;   quit     - Exit

%include "syscalls.inc"
%include "lib/net.inc"

start:
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz mail_usage

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

        ; Resolve host
        mov esi, hostname
        call net_dns
        test eax, eax
        jz mail_dns_fail
        mov [server_ip], eax

        mov eax, SYS_PRINT
        mov ebx, msg_welcome
        int 0x80

        ; Main command loop
.cmd_loop:
        mov eax, SYS_PRINT
        mov ebx, prompt_str
        int 0x80

        mov edi, input_buf
        mov ecx, 255
        call read_line
        test eax, eax
        jz .cmd_loop

        mov esi, input_buf
        call strip_crlf

        mov edi, cmd_compose
        call starts_with
        jc .do_compose

        mov edi, cmd_inbox
        call starts_with
        jc .do_inbox

        mov edi, cmd_read
        call starts_with
        jc .do_read

        mov edi, cmd_del
        call starts_with
        jc .do_delete

        mov edi, cmd_quit
        call starts_with
        jc .do_quit

        mov eax, SYS_PRINT
        mov ebx, msg_unknown
        int 0x80
        jmp .cmd_loop

.do_quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ============================================================
; COMPOSE - Send email via SMTP (port 25)
; ============================================================
.do_compose:
        ; Prompt for fields
        mov eax, SYS_PRINT
        mov ebx, prompt_from
        int 0x80
        mov edi, mail_from
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_to
        int 0x80
        mov edi, mail_to
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_subj
        int 0x80
        mov edi, mail_subj
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_body
        int 0x80
        mov edi, mail_body
        mov ecx, 1023
        call read_line

        ; Connect SMTP
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .smtp_err
        mov [smtp_fd], eax

        mov eax, [smtp_fd]
        mov ebx, [server_ip]
        mov ecx, 25
        call net_connect
        cmp eax, -1
        je .smtp_conn_err

        ; Read greeting
        call smtp_recv

        ; HELO
        mov esi, smtp_helo
        call smtp_send
        call smtp_recv

        ; MAIL FROM
        mov edi, cmd_buf
        push rdi
        mov esi, smtp_mail_from
        call copy_str
        mov esi, mail_from
        call copy_str
        mov byte [edi], '>'
        inc edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        pop rsi
        call smtp_send
        call smtp_recv

        ; RCPT TO
        mov edi, cmd_buf
        push rdi
        mov esi, smtp_rcpt_to
        call copy_str
        mov esi, mail_to
        call copy_str
        mov byte [edi], '>'
        inc edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        pop rsi
        call smtp_send
        call smtp_recv

        ; DATA
        mov esi, smtp_data
        call smtp_send
        call smtp_recv

        ; Send headers + body
        mov edi, cmd_buf
        push rdi
        ; From:
        mov esi, hdr_from
        call copy_str
        mov esi, mail_from
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; To:
        mov esi, hdr_to
        call copy_str
        mov esi, mail_to
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; Subject:
        mov esi, hdr_subj
        call copy_str
        mov esi, mail_subj
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; Blank line + body
        mov word [edi], 0x0A0D
        add edi, 2
        mov esi, mail_body
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; End with ".\r\n"
        mov byte [edi], '.'
        inc edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        pop rsi
        call smtp_send
        call smtp_recv

        ; QUIT
        mov esi, smtp_quit
        call smtp_send
        call smtp_recv

        mov eax, [smtp_fd]
        call net_close

        mov eax, SYS_PRINT
        mov ebx, msg_sent
        int 0x80
        jmp .cmd_loop

.smtp_err:
        mov eax, SYS_PRINT
        mov ebx, msg_smtp_err
        int 0x80
        jmp .cmd_loop
.smtp_conn_err:
        mov eax, [smtp_fd]
        call net_close
        mov eax, SYS_PRINT
        mov ebx, msg_smtp_err
        int 0x80
        jmp .cmd_loop

; ============================================================
; INBOX - List messages via POP3 (port 110)
; ============================================================
.do_inbox:
        call pop3_connect
        test eax, eax
        jz .pop3_err

        ; LIST
        mov esi, pop3_list
        call pop3_send
        call pop3_recv_multi

        call pop3_quit
        jmp .cmd_loop

; ============================================================
; READ N - Read message via POP3
; ============================================================
.do_read:
        call pop3_connect
        test eax, eax
        jz .pop3_err

        ; Build RETR N
        mov edi, cmd_buf
        mov dword [edi], 'RETR'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        ; Parse number from input
        mov esi, input_buf
        add esi, 5              ; skip "read "
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0

        mov esi, cmd_buf
        call pop3_send
        call pop3_recv_multi

        call pop3_quit
        jmp .cmd_loop

; ============================================================
; DELETE N - Delete message via POP3
; ============================================================
.do_delete:
        call pop3_connect
        test eax, eax
        jz .pop3_err

        ; Build DELE N
        mov edi, cmd_buf
        mov dword [edi], 'DELE'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        mov esi, input_buf
        add esi, 7              ; skip "delete "
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0

        mov esi, cmd_buf
        call pop3_send
        call pop3_recv

        call pop3_quit
        jmp .cmd_loop

.pop3_err:
        mov eax, SYS_PRINT
        mov ebx, msg_pop3_err
        int 0x80
        jmp .cmd_loop

; ============================================================
; SMTP helpers
; ============================================================
smtp_send:
        ; ESI = null-terminated string
        push rsi
        xor ecx, ecx
        mov edi, esi
.ss_len:
        cmp byte [edi + ecx], 0
        je .ss_go
        inc ecx
        jmp .ss_len
.ss_go:
        mov eax, [smtp_fd]
        mov ebx, esi
        call net_send
        pop rsi
        ret

smtp_recv:
        mov dword [retry_count], 0
.sr_loop:
        mov eax, [smtp_fd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv
        cmp eax, 0
        jle .sr_retry
        mov byte [resp_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        ret
.sr_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 200
        jge .sr_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .sr_loop
.sr_done:
        ret

; ============================================================
; POP3 helpers
; ============================================================

; pop3_connect: Connect to POP3 server, login
; Returns EAX=1 success, 0 fail
pop3_connect:
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .pc_fail
        mov [pop3_fd], eax

        mov eax, [pop3_fd]
        mov ebx, [server_ip]
        mov ecx, 110
        call net_connect
        cmp eax, -1
        je .pc_close_fail

        ; Read greeting
        call pop3_recv

        ; Prompt for user/pass
        mov eax, SYS_PRINT
        mov ebx, prompt_user
        int 0x80
        mov edi, pop3_user
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_pass
        int 0x80
        mov edi, pop3_pass
        mov ecx, 127
        call read_line

        ; USER
        mov edi, cmd_buf
        mov dword [edi], 'USER'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        mov esi, pop3_user
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        mov esi, cmd_buf
        call pop3_send
        call pop3_recv

        ; PASS
        mov edi, cmd_buf
        mov dword [edi], 'PASS'
        add edi, 4
        mov byte [edi], ' '
        inc edi
        mov esi, pop3_pass
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        mov esi, cmd_buf
        call pop3_send
        call pop3_recv

        mov eax, 1
        ret
.pc_close_fail:
        mov eax, [pop3_fd]
        call net_close
.pc_fail:
        xor eax, eax
        ret

pop3_send:
        push rsi
        xor ecx, ecx
        mov edi, esi
.ps_len:
        cmp byte [edi + ecx], 0
        je .ps_go
        inc ecx
        jmp .ps_len
.ps_go:
        mov eax, [pop3_fd]
        mov ebx, esi
        call net_send
        pop rsi
        ret

pop3_recv:
        mov dword [retry_count], 0
.pr_loop:
        mov eax, [pop3_fd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv
        cmp eax, 0
        jle .pr_retry
        mov byte [resp_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        ret
.pr_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 200
        jge .pr_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .pr_loop
.pr_done:
        ret

; pop3_recv_multi: receive multi-line response until ".\r\n"
pop3_recv_multi:
        mov dword [retry_count], 0
.prm_loop:
        mov eax, [pop3_fd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv
        cmp eax, 0
        jle .prm_retry

        mov dword [retry_count], 0
        mov byte [resp_buf + eax], 0
        push rax
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        pop rax

        ; Check for terminator: "\r\n.\r\n" at end
        cmp eax, 3
        jl .prm_loop
        lea ebx, [resp_buf + eax - 3]
        cmp byte [ebx], '.'
        jne .prm_loop
        cmp byte [ebx+1], 0x0D
        jne .prm_loop
        cmp byte [ebx+2], 0x0A
        jne .prm_loop
        ret

.prm_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 300
        jge .prm_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .prm_loop
.prm_done:
        ret

pop3_quit:
        mov esi, pop3_quit_str
        call pop3_send
        call pop3_recv
        mov eax, [pop3_fd]
        call net_close
        ret

; ============================================================
; Utility routines
; ============================================================

strip_crlf:
        mov esi, input_buf
strip_crlf_esi:
        push rsi
        mov edi, esi
        xor ecx, ecx
.scr_len:
        cmp byte [edi + ecx], 0
        je .scr_strip
        inc ecx
        jmp .scr_len
.scr_strip:
        test ecx, ecx
        jz .scr_done
        dec ecx
        cmp byte [edi + ecx], 0x0A
        je .scr_zero
        cmp byte [edi + ecx], 0x0D
        je .scr_zero
        jmp .scr_done
.scr_zero:
        mov byte [edi + ecx], 0
        jmp .scr_strip
.scr_done:
        pop rsi
        ret

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

; copy_str: copy ESI to EDI, advance EDI, null not copied
copy_str:
.cs_loop:
        lodsb
        cmp al, 0
        je .cs_done
        stosb
        jmp .cs_loop
.cs_done:
        ret

; --- read_line: read a line from keyboard into EDI, max ECX chars ---
; Returns EAX = length
read_line:
        push rbx
        push rcx
        push rdi
        xor edx, edx
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

; Error screens
mail_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
mail_dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ============================================================
; Strings
; ============================================================
msg_usage:      db "Usage: mail <server>", 0x0A
                db "  Commands: compose, inbox, read N, delete N, quit", 0x0A, 0
msg_dns:        db "Error: DNS resolution failed", 0x0A, 0
msg_welcome:    db "Mellivora Mail Client", 0x0A
                db "Commands: compose, inbox, read N, delete N, quit", 0x0A, 0
msg_unknown:    db "Unknown command.", 0x0A, 0
msg_sent:       db "Message sent.", 0x0A, 0
msg_smtp_err:   db "Error: SMTP connection failed", 0x0A, 0
msg_pop3_err:   db "Error: POP3 connection failed", 0x0A, 0
prompt_str:     db "mail> ", 0
prompt_from:    db "From: ", 0
prompt_to:      db "To: ", 0
prompt_subj:    db "Subject: ", 0
prompt_body:    db "Body (single line): ", 0
prompt_user:    db "Username: ", 0
prompt_pass:    db "Password: ", 0

cmd_compose:    db "compose", 0
cmd_inbox:      db "inbox", 0
cmd_read:       db "read ", 0
cmd_del:        db "delete ", 0
cmd_quit:       db "quit", 0

smtp_helo:      db "HELO mellivora", 0x0D, 0x0A, 0
smtp_mail_from: db "MAIL FROM:<", 0
smtp_rcpt_to:   db "RCPT TO:<", 0
smtp_data:      db "DATA", 0x0D, 0x0A, 0
smtp_quit:      db "QUIT", 0x0D, 0x0A, 0
hdr_from:       db "From: ", 0
hdr_to:         db "To: ", 0
hdr_subj:       db "Subject: ", 0

pop3_list:      db "LIST", 0x0D, 0x0A, 0
pop3_quit_str:  db "QUIT", 0x0D, 0x0A, 0

; ============================================================
; Data
; ============================================================
hostname:       times 256 db 0
server_ip:      dd 0
smtp_fd:        dd 0
pop3_fd:        dd 0
retry_count:    dd 0
mail_from:      times 128 db 0
mail_to:        times 128 db 0
mail_subj:      times 128 db 0
mail_body:      times 1024 db 0
pop3_user:      times 128 db 0
pop3_pass:      times 128 db 0
cmd_buf:        times 2048 db 0
arg_buf:        times 512 db 0
input_buf:      times 512 db 0
resp_buf:       times 2048 db 0
