; news.asm - Usenet/NNTP client
; Usage: news <server>
;
; Interactive NNTP client (port 119). Commands:
;   list         - List newsgroups
;   group <name> - Select newsgroup
;   headers      - Show article headers in current group
;   read <N>     - Read article number N
;   post         - Post a new article
;   quit         - Disconnect and exit

%include "syscalls.inc"
%include "lib/net.inc"

start:
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz news_usage

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

        ; Resolve
        mov esi, hostname
        call net_dns
        test eax, eax
        jz news_dns_fail
        mov [server_ip], eax

        ; Connect
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je news_sock_fail
        mov [sockfd], eax

        mov eax, [sockfd]
        mov ebx, [server_ip]
        mov ecx, 119
        call net_connect
        cmp eax, -1
        je news_conn_fail

        ; Read greeting
        call nntp_recv

        mov eax, SYS_PRINT
        mov ebx, msg_welcome
        int 0x80

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

        mov edi, cmd_quit
        call starts_with
        jc .do_quit

        mov edi, cmd_list
        call starts_with
        jc .do_list

        mov edi, cmd_group
        call starts_with
        jc .do_group

        mov edi, cmd_headers
        call starts_with
        jc .do_headers

        mov edi, cmd_read
        call starts_with
        jc .do_read

        mov edi, cmd_post
        call starts_with
        jc .do_post

        mov eax, SYS_PRINT
        mov ebx, msg_unknown
        int 0x80
        jmp .cmd_loop

; --- Commands ---

.do_quit:
        mov esi, nntp_quit
        call nntp_send
        call nntp_recv
        mov eax, [sockfd]
        call net_close
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.do_list:
        mov esi, nntp_list
        call nntp_send
        call nntp_recv_multi
        jmp .cmd_loop

.do_group:
        ; Build GROUP <name>
        mov edi, cmd_buf
        mov dword [edi], 'GROU'
        mov byte [edi+4], 'P'
        mov byte [edi+5], ' '
        add edi, 6
        mov esi, input_buf
        add esi, 6             ; skip "group "
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0

        mov esi, cmd_buf
        call nntp_send
        call nntp_recv
        jmp .cmd_loop

.do_headers:
        ; XOVER or HEAD range - use XOVER for subject list
        mov esi, nntp_xover
        call nntp_send
        call nntp_recv_multi
        jmp .cmd_loop

.do_read:
        ; Build ARTICLE <N>
        mov edi, cmd_buf
        push rdi
        mov esi, article_cmd
        call copy_str
        mov esi, input_buf
        add esi, 5             ; skip "read "
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        pop rsi
        call nntp_send
        call nntp_recv_multi
        jmp .cmd_loop

.do_post:
        ; POST
        mov esi, nntp_post
        call nntp_send
        call nntp_recv

        ; Check for 340 response (go ahead)
        cmp byte [resp_buf], '3'
        jne .post_refused

        ; Prompt for fields
        mov eax, SYS_PRINT
        mov ebx, prompt_ng
        int 0x80
        mov edi, post_ng
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_from
        int 0x80
        mov edi, post_from
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_subj
        int 0x80
        mov edi, post_subj
        mov ecx, 127
        call read_line

        mov eax, SYS_PRINT
        mov ebx, prompt_body
        int 0x80
        mov edi, post_body
        mov ecx, 1023
        call read_line

        ; Build article
        mov edi, cmd_buf
        push rdi
        ; Newsgroups:
        mov esi, hdr_ng
        call copy_str
        mov esi, post_ng
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; From:
        mov esi, hdr_from
        call copy_str
        mov esi, post_from
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; Subject:
        mov esi, hdr_subj
        call copy_str
        mov esi, post_subj
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; Blank line
        mov word [edi], 0x0A0D
        add edi, 2
        ; Body
        mov esi, post_body
        call copy_str
        mov word [edi], 0x0A0D
        add edi, 2
        ; Terminator: ".\r\n"
        mov byte [edi], '.'
        inc edi
        mov word [edi], 0x0A0D
        add edi, 2
        mov byte [edi], 0
        pop rsi
        call nntp_send
        call nntp_recv

        mov eax, SYS_PRINT
        mov ebx, msg_posted
        int 0x80
        jmp .cmd_loop

.post_refused:
        mov eax, SYS_PRINT
        mov ebx, msg_post_refused
        int 0x80
        jmp .cmd_loop

; ============================================================
; NNTP helpers
; ============================================================

nntp_send:
        push rsi
        xor ecx, ecx
        mov edi, esi
.ns_len:
        cmp byte [edi + ecx], 0
        je .ns_go
        inc ecx
        jmp .ns_len
.ns_go:
        mov eax, [sockfd]
        mov ebx, esi
        call net_send
        pop rsi
        ret

nntp_recv:
        mov dword [retry_count], 0
.nr_loop:
        mov eax, [sockfd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv
        cmp eax, 0
        jle .nr_retry
        mov byte [resp_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        ret
.nr_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 200
        jge .nr_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .nr_loop
.nr_done:
        ret

; nntp_recv_multi: receive until ".\r\n" terminator
nntp_recv_multi:
        mov dword [retry_count], 0
.nrm_loop:
        mov eax, [sockfd]
        mov ebx, resp_buf
        mov ecx, 2048
        call net_recv
        cmp eax, 0
        jle .nrm_retry

        mov dword [retry_count], 0
        mov byte [resp_buf + eax], 0
        push rax
        mov eax, SYS_PRINT
        mov ebx, resp_buf
        int 0x80
        pop rax

        ; Check for ".\r\n" terminator at end
        cmp eax, 3
        jl .nrm_loop
        lea ebx, [resp_buf + eax - 3]
        cmp byte [ebx], '.'
        jne .nrm_loop
        cmp byte [ebx+1], 0x0D
        jne .nrm_loop
        cmp byte [ebx+2], 0x0A
        jne .nrm_loop
        ret

.nrm_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 300
        jge .nrm_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .nrm_loop
.nrm_done:
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

; Error handlers
news_usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
news_dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
news_sock_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_sock
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
news_conn_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn
        int 0x80
        mov eax, [sockfd]
        call net_close
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ============================================================
; Strings
; ============================================================
msg_usage:        db "Usage: news <server>", 0x0A
                  db "  Commands: list, group, headers, read N, post, quit", 0x0A, 0
msg_dns:          db "Error: DNS resolution failed", 0x0A, 0
msg_sock:         db "Error: Could not create socket", 0x0A, 0
msg_conn:         db "Error: Connection failed", 0x0A, 0
msg_welcome:      db "Mellivora News Reader (NNTP)", 0x0A
                  db "Commands: list, group <name>, headers, read <N>, post, quit", 0x0A, 0
msg_unknown:      db "Unknown command.", 0x0A, 0
msg_posted:       db "Article posted.", 0x0A, 0
msg_post_refused: db "Server refused post.", 0x0A, 0
prompt_str:       db "news> ", 0
prompt_ng:        db "Newsgroup: ", 0
prompt_from:      db "From: ", 0
prompt_subj:      db "Subject: ", 0
prompt_body:      db "Body (single line): ", 0

cmd_quit:         db "quit", 0
cmd_list:         db "list", 0
cmd_group:        db "group ", 0
cmd_headers:      db "headers", 0
cmd_read:         db "read ", 0
cmd_post:         db "post", 0

nntp_quit:        db "QUIT", 0x0D, 0x0A, 0
nntp_list:        db "LIST", 0x0D, 0x0A, 0
nntp_post:        db "POST", 0x0D, 0x0A, 0
nntp_xover:       db "XOVER", 0x0D, 0x0A, 0
article_cmd:      db "ARTICLE ", 0

hdr_ng:           db "Newsgroups: ", 0
hdr_from:         db "From: ", 0
hdr_subj:         db "Subject: ", 0

; ============================================================
; Data
; ============================================================
hostname:         times 256 db 0
server_ip:        dd 0
sockfd:           dd 0
retry_count:      dd 0
post_ng:          times 128 db 0
post_from:        times 128 db 0
post_subj:        times 128 db 0
post_body:        times 1024 db 0
cmd_buf:          times 2048 db 0
arg_buf:          times 512 db 0
input_buf:        times 512 db 0
resp_buf:         times 2048 db 0
