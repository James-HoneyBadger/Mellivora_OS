; irc.asm - IRC Client for Mellivora OS
; Usage: irc <server> [port] [nick]
;
; Commands:
;   /join #channel    - Join a channel
;   /part [#channel]  - Leave current/specified channel
;   /nick <name>      - Change nickname
;   /msg <user> <msg> - Private message
;   /quit [message]   - Disconnect and exit
;   /list             - List channels
;   /who              - List users in channel
;   /me <action>      - Send action message
;   (other text)      - Send message to current channel

%include "syscalls.inc"
%include "lib/net.inc"

IRC_PORT_DEFAULT equ 6667
RECV_BUF_SIZE   equ 4096
SEND_BUF_SIZE   equ 512
LINE_BUF_SIZE   equ 512
NICK_LEN        equ 16
CHAN_LEN        equ 32

COL_SERVER      equ 0x0E        ; yellow
COL_ERROR       equ 0x0C        ; light red
COL_INFO        equ 0x0B        ; light cyan
COL_NICK        equ 0x0A        ; light green
COL_MSG         equ 0x07        ; grey
COL_ACTION      equ 0x0D        ; light magenta
COL_PROMPT      equ 0x0F        ; white

start:
        ; Parse arguments
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz irc_usage

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
        mov word [port], IRC_PORT_DEFAULT

        ; Skip spaces
        cmp byte [esi], ' '
        jne .set_nick_default
        inc esi

        ; Optional port
        cmp byte [esi], 0
        je .set_nick_default
        xor eax, eax
        cmp byte [esi], '0'
        jb .parse_nick_arg
        cmp byte [esi], '9'
        ja .parse_nick_arg
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
        mov [port], ax
        cmp byte [esi], ' '
        jne .set_nick_default
        inc esi

.parse_nick_arg:
        ; Optional nick
        mov edi, my_nick
        xor ecx, ecx
.pn_loop:
        mov al, [esi]
        cmp al, ' '
        je .pn_done
        cmp al, 0
        je .pn_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, NICK_LEN - 1
        jb .pn_loop
.pn_done:
        mov byte [edi + ecx], 0
        jmp .connect

.set_nick_default:
        ; Default nick
        mov esi, default_nick
        mov edi, my_nick
.copy_def:
        lodsb
        stosb
        test al, al
        jnz .copy_def

.connect:
        ; Print connection info
        mov ebx, COL_INFO
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_connecting
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, hostname
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, msg_dots
        mov eax, SYS_PRINT
        int 0x80

        ; DNS resolve
        mov esi, hostname
        call net_dns
        test eax, eax
        jz irc_dns_fail
        mov [server_ip], eax

        ; Create TCP socket
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je irc_sock_fail
        mov [sock_fd], eax

        ; Connect
        mov eax, [sock_fd]
        mov ebx, [server_ip]
        movzx ecx, word [port]
        call net_connect
        cmp eax, -1
        je irc_conn_fail

        ; Print connected
        mov ebx, COL_INFO
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_connected
        mov eax, SYS_PRINT
        int 0x80

        ; Send NICK
        mov edi, send_buf
        mov esi, cmd_nick_irc
        call strcpy_append
        mov esi, my_nick
        call strcpy_append
        mov word [edi], 0x0A0D  ; \r\n
        add edi, 2
        call irc_send_buf

        ; Send USER
        mov edi, send_buf
        mov esi, cmd_user_irc
        call strcpy_append
        mov esi, my_nick
        call strcpy_append
        mov esi, user_suffix
        call strcpy_append
        call irc_send_buf

        ; Main loop
main_loop:
        ; Check for incoming data
        mov eax, [sock_fd]
        mov ebx, recv_buf
        mov ecx, RECV_BUF_SIZE - 1
        call net_recv
        cmp eax, -1
        je irc_disconnected
        test eax, eax
        jz .check_input

        ; Null-terminate received data
        mov byte [recv_buf + eax], 0

        ; Process received IRC lines
        mov esi, recv_buf
.process_lines:
        cmp byte [esi], 0
        je .check_input

        ; Find end of line (\r\n or \n)
        mov edi, line_parse_buf
        xor ecx, ecx
.copy_line:
        mov al, [esi]
        cmp al, 0
        je .line_done
        cmp al, 0x0A
        je .line_nl
        cmp al, 0x0D
        je .line_cr
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, LINE_BUF_SIZE - 1
        jb .copy_line
        jmp .line_done
.line_cr:
        inc esi
        cmp byte [esi], 0x0A
        jne .line_done
.line_nl:
        inc esi
.line_done:
        mov byte [edi + ecx], 0
        test ecx, ecx
        jz .process_lines

        ; Parse and display the IRC line
        call parse_irc_line
        jmp .process_lines

.check_input:
        ; Check keyboard
        mov eax, SYS_GETCHAR
        int 0x80
        test al, al
        jz main_loop

        cmp al, 0x0D           ; Enter
        je .send_input

        cmp al, 0x08           ; Backspace
        je .input_bs

        ; Printable?
        cmp al, 32
        jb main_loop
        cmp al, 126
        ja main_loop

        ; Add to input buffer
        cmp dword [input_len], LINE_BUF_SIZE - 2
        jge main_loop
        mov edi, [input_len]
        mov [input_buf + edi], al
        inc dword [input_len]

        ; Echo character
        push rax
        mov ebx, COL_PROMPT
        mov eax, SYS_SETCOLOR
        int 0x80
        pop rax
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp main_loop

.input_bs:
        cmp dword [input_len], 0
        je main_loop
        dec dword [input_len]
        mov ebx, 0x08
        mov eax, SYS_PUTCHAR
        int 0x80
        mov ebx, ' '
        mov eax, SYS_PUTCHAR
        int 0x80
        mov ebx, 0x08
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp main_loop

.send_input:
        ; Newline on screen
        mov ebx, 0x0A
        mov eax, SYS_PUTCHAR
        int 0x80

        ; Null-terminate
        mov edi, [input_len]
        mov byte [input_buf + edi], 0

        cmp dword [input_len], 0
        je main_loop

        ; Check if it's a command (starts with /)
        cmp byte [input_buf], '/'
        je .handle_command

        ; Regular message → send PRIVMSG to current channel
        cmp byte [current_chan], 0
        je .no_channel

        ; Build PRIVMSG
        mov edi, send_buf
        mov esi, str_privmsg
        call strcpy_append
        mov esi, current_chan
        call strcpy_append
        mov byte [edi], ' '
        inc edi
        mov byte [edi], ':'
        inc edi
        mov esi, input_buf
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf

        ; Echo locally: <mynick> message
        mov ebx, COL_NICK
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_lt
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, my_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_gt_space
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, COL_MSG
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, input_buf
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80

.input_done:
        mov dword [input_len], 0
        jmp main_loop

.no_channel:
        mov ebx, COL_ERROR
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_no_chan
        mov eax, SYS_PRINT
        int 0x80
        jmp .input_done

;--- Command handling ---
.handle_command:
        mov esi, input_buf
        inc esi                 ; skip '/'

        ; /quit
        mov edi, str_cmd_quit
        call cmd_match
        jc .cmd_quit

        ; /join
        mov edi, str_cmd_join
        call cmd_match
        jc .cmd_join

        ; /part
        mov edi, str_cmd_part
        call cmd_match
        jc .cmd_part

        ; /nick
        mov edi, str_cmd_nick
        call cmd_match
        jc .cmd_nick

        ; /msg
        mov edi, str_cmd_msg
        call cmd_match
        jc .cmd_privmsg

        ; /me
        mov edi, str_cmd_me
        call cmd_match
        jc .cmd_me

        ; /list
        mov edi, str_cmd_list
        call cmd_match
        jc .cmd_list

        ; /who
        mov edi, str_cmd_who
        call cmd_match
        jc .cmd_who

        ; Unknown command
        mov ebx, COL_ERROR
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_unknown_cmd
        mov eax, SYS_PRINT
        int 0x80
        jmp .input_done

.cmd_quit:
        ; Send QUIT message
        mov edi, send_buf
        mov esi, str_quit_irc
        call strcpy_append
        ; Check if there's a quit message
        cmp byte [esi], 0
        je .quit_no_msg
        ; esi still points to text after "quit "
        mov esi, input_buf
        add esi, 5              ; skip "/quit"
        cmp byte [esi], ' '
        jne .quit_no_msg
        inc esi
        mov byte [edi], ':'
        inc edi
        call strcpy_append
        jmp .quit_send
.quit_no_msg:
        mov esi, default_quit
        mov byte [edi], ':'
        inc edi
        call strcpy_append
.quit_send:
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf

        ; Wait briefly for server response
        mov ebx, 50
        mov eax, SYS_SLEEP
        int 0x80

        jmp irc_exit

.cmd_join:
        ; /join #channel
        call skip_cmd_space
        cmp byte [esi], 0
        je .input_done

        ; Save channel name
        mov edi, current_chan
        xor ecx, ecx
.cj_copy:
        mov al, [esi + ecx]
        cmp al, ' '
        je .cj_done
        cmp al, 0
        je .cj_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, CHAN_LEN - 1
        jb .cj_copy
.cj_done:
        mov byte [edi + ecx], 0

        ; Send JOIN
        mov edi, send_buf
        mov esi, str_join_irc
        call strcpy_append
        mov esi, current_chan
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        jmp .input_done

.cmd_part:
        ; /part [#channel]
        call skip_cmd_space
        cmp byte [esi], 0
        jne .part_named
        ; Part current channel
        cmp byte [current_chan], 0
        je .input_done
        mov edi, send_buf
        mov esi, str_part_irc
        call strcpy_append
        mov esi, current_chan
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        mov byte [current_chan], 0
        jmp .input_done
.part_named:
        mov edi, send_buf
        push rsi
        mov esi, str_part_irc
        call strcpy_append
        pop rsi
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        jmp .input_done

.cmd_nick:
        call skip_cmd_space
        cmp byte [esi], 0
        je .input_done
        ; Save new nick
        mov edi, my_nick
        xor ecx, ecx
.cn_copy:
        mov al, [esi + ecx]
        cmp al, ' '
        je .cn_done
        cmp al, 0
        je .cn_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, NICK_LEN - 1
        jb .cn_copy
.cn_done:
        mov byte [edi + ecx], 0
        ; Send NICK
        mov edi, send_buf
        mov esi, cmd_nick_irc
        call strcpy_append
        mov esi, my_nick
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        jmp .input_done

.cmd_privmsg:
        ; /msg <user> <message>
        call skip_cmd_space
        cmp byte [esi], 0
        je .input_done
        ; Get target
        mov edi, send_buf
        push rsi
        mov esi, str_privmsg
        call strcpy_append
        pop rsi
        ; Copy target
.cm_target:
        mov al, [esi]
        cmp al, ' '
        je .cm_target_done
        cmp al, 0
        je .cm_done_send
        mov [edi], al
        inc edi
        inc esi
        jmp .cm_target
.cm_target_done:
        mov byte [edi], ' '
        inc edi
        mov byte [edi], ':'
        inc edi
        inc esi                 ; skip space
        call strcpy_append
.cm_done_send:
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        jmp .input_done

.cmd_me:
        ; /me <action>
        call skip_cmd_space
        cmp byte [current_chan], 0
        je .no_channel
        ; Build ACTION CTCP
        mov edi, send_buf
        push rsi
        mov esi, str_privmsg
        call strcpy_append
        mov esi, current_chan
        call strcpy_append
        mov esi, str_action_pre
        call strcpy_append
        pop rsi
        call strcpy_append
        mov byte [edi], 0x01   ; CTCP delimiter
        inc edi
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf

        ; Echo action
        mov ebx, COL_ACTION
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_star_space
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, my_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_space
        mov eax, SYS_PRINT
        int 0x80
        ; Re-parse the action text
        mov esi, input_buf
        add esi, 3              ; skip "/me"
        cmp byte [esi], ' '
        jne .me_echo_done
        inc esi
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
.me_echo_done:
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        jmp .input_done

.cmd_list:
        mov edi, send_buf
        mov esi, str_list_irc
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        jmp .input_done

.cmd_who:
        cmp byte [current_chan], 0
        je .no_channel
        mov edi, send_buf
        mov esi, str_who_irc
        call strcpy_append
        mov esi, current_chan
        call strcpy_append
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        jmp .input_done

irc_disconnected:
        mov ebx, COL_ERROR
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_disconnected
        mov eax, SYS_PRINT
        int 0x80

irc_exit:
        mov eax, [sock_fd]
        call net_close
        mov ebx, COL_MSG
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

irc_usage:
        mov ebx, msg_usage
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

irc_dns_fail:
        mov ebx, COL_ERROR
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_dns_fail
        mov eax, SYS_PRINT
        int 0x80
        jmp irc_exit

irc_sock_fail:
        mov ebx, COL_ERROR
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_sock_fail
        mov eax, SYS_PRINT
        int 0x80
        jmp irc_exit

irc_conn_fail:
        mov ebx, COL_ERROR
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, msg_conn_fail
        mov eax, SYS_PRINT
        int 0x80
        jmp irc_exit


;=======================================================================
; parse_irc_line - Parse and display one IRC protocol line
;  line_parse_buf contains the line
;=======================================================================
parse_irc_line:
        PUSHALL
        mov esi, line_parse_buf

        ; PING handler
        cmp dword [esi], 'PING'
        jne .not_ping
        ; Reply with PONG
        mov edi, send_buf
        mov dword [edi], 'PONG'
        add edi, 4
        ; Copy rest of PING line
        add esi, 4
.pong_copy:
        mov al, [esi]
        cmp al, 0
        je .pong_done
        mov [edi], al
        inc edi
        inc esi
        jmp .pong_copy
.pong_done:
        mov word [edi], 0x0A0D
        add edi, 2
        call irc_send_buf
        POPALL
        ret

.not_ping:
        ; Lines starting with ':' have a prefix
        cmp byte [esi], ':'
        jne .no_prefix

        inc esi                 ; skip ':'
        ; Extract nick from prefix (before !)
        mov edi, sender_nick
        xor ecx, ecx
.extract_nick:
        mov al, [esi]
        cmp al, '!'
        je .nick_done
        cmp al, ' '
        je .nick_done
        cmp al, 0
        je .nick_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, NICK_LEN - 1
        jb .extract_nick
.nick_done:
        mov byte [edi + ecx], 0

        ; Skip to space after prefix
.skip_prefix:
        cmp byte [esi], ' '
        je .prefix_done
        cmp byte [esi], 0
        je .parse_done
        inc esi
        jmp .skip_prefix
.prefix_done:
        inc esi                 ; skip space

        ; Check command type
        ; PRIVMSG
        cmp dword [esi], 'PRIV'
        jne .not_privmsg
        add esi, 8              ; skip "PRIVMSG "
        ; Get target
        mov edi, msg_target
        xor ecx, ecx
.get_target:
        mov al, [esi]
        cmp al, ' '
        je .target_done
        cmp al, 0
        je .target_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, CHAN_LEN - 1
        jb .get_target
.target_done:
        mov byte [edi + ecx], 0
        ; Skip " :"
        cmp byte [esi], ' '
        jne .show_msg
        inc esi
        cmp byte [esi], ':'
        jne .show_msg
        inc esi

        ; Check for CTCP ACTION
        cmp byte [esi], 0x01
        jne .show_msg
        inc esi
        cmp dword [esi], 'ACTI'
        jne .show_msg
        add esi, 7              ; skip "ACTION "
        ; Display action
        mov ebx, COL_ACTION
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_star_space
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, sender_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_space
        mov eax, SYS_PRINT
        int 0x80
        ; Print action text (strip trailing 0x01)
        call print_strip_ctcp
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.show_msg:
        ; Display: <nick> message
        mov ebx, COL_NICK
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_lt
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, sender_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_gt_space
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, COL_MSG
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.not_privmsg:
        ; JOIN
        cmp dword [esi], 'JOIN'
        jne .not_join
        add esi, 5             ; skip "JOIN "
        ; Skip leading ':'
        cmp byte [esi], ':'
        jne .join_show
        inc esi
.join_show:
        mov ebx, COL_INFO
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_arrow
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, sender_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, msg_has_joined
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.not_join:
        ; PART
        cmp dword [esi], 'PART'
        jne .not_part
        add esi, 5
        mov ebx, COL_INFO
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_arrow_left
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, sender_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, msg_has_left
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.not_part:
        ; QUIT
        cmp dword [esi], 'QUIT'
        jne .not_quit
        mov ebx, COL_INFO
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_arrow_left
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, sender_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, msg_has_quit
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.not_quit:
        ; NICK change
        cmp dword [esi], 'NICK'
        jne .not_nick_change
        add esi, 5
        cmp byte [esi], ':'
        jne .nick_ch_show
        inc esi
.nick_ch_show:
        mov ebx, COL_INFO
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, sender_nick
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, msg_nick_change
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.not_nick_change:
        ; NOTICE - display as server message
        cmp dword [esi], 'NOTI'
        jne .not_notice
        add esi, 7             ; skip "NOTICE "
        ; Skip target and ':'
.notice_skip:
        cmp byte [esi], ':'
        je .notice_msg
        cmp byte [esi], 0
        je .parse_done
        inc esi
        jmp .notice_skip
.notice_msg:
        inc esi
        mov ebx, COL_SERVER
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, str_notice_pre
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.not_notice:
        ; Numeric replies (3-digit codes)
        ; Check if first 3 chars are digits
        movzx eax, byte [esi]
        cmp al, '0'
        jb .show_raw
        cmp al, '9'
        ja .show_raw
        ; It's a numeric reply — skip number and target, show text
        add esi, 4             ; skip "NNN "
        ; Skip target nick
.skip_numeric_target:
        cmp byte [esi], ' '
        je .numeric_text
        cmp byte [esi], 0
        je .parse_done
        inc esi
        jmp .skip_numeric_target
.numeric_text:
        inc esi
        cmp byte [esi], ':'
        jne .numeric_show
        inc esi
.numeric_show:
        mov ebx, COL_SERVER
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.show_raw:
        ; Show raw server line
        mov ebx, COL_SERVER
        mov eax, SYS_SETCOLOR
        int 0x80
        mov ebx, line_parse_buf
        mov eax, SYS_PRINT
        int 0x80
        mov ebx, str_newline
        mov eax, SYS_PRINT
        int 0x80
        POPALL
        ret

.no_prefix:
        ; Lines without prefix (e.g., ERROR)
        jmp .show_raw

.parse_done:
        POPALL
        ret


;=======================================================================
; Helper routines
;=======================================================================

; strcpy_append - Copy string at ESI to EDI, advance EDI past end
strcpy_append:
        push rax
.sc_loop:
        lodsb
        test al, al
        jz .sc_done
        stosb
        jmp .sc_loop
.sc_done:
        pop rax
        ret

; irc_send_buf - Send contents of send_buf (EDI = end pointer)
irc_send_buf:
        PUSHALL
        mov ecx, edi
        sub ecx, send_buf      ; length
        mov eax, [sock_fd]
        mov ebx, send_buf
        call net_send
        POPALL
        ret

; cmd_match - Check if input starts with command name
;  ESI = input (after '/'), EDI = command string
; Returns: carry set if match, ESI advanced past command
cmd_match:
        push rax
        push rbx
        mov ebx, esi            ; save start
.cm_loop:
        mov al, [edi]
        test al, al
        jz .cm_check_end
        cmp al, [esi]
        jne .cm_no
        ; Case-insensitive
        or al, 0x20
        mov ah, [esi]
        or ah, 0x20
        cmp al, ah
        jne .cm_no
        inc esi
        inc edi
        jmp .cm_loop
.cm_check_end:
        ; Command matched if next char is space or NUL
        mov al, [esi]
        cmp al, ' '
        je .cm_yes
        cmp al, 0
        je .cm_yes
.cm_no:
        mov esi, ebx            ; restore
        pop rbx
        pop rax
        clc
        ret
.cm_yes:
        pop rbx
        pop rax
        stc
        ret

; skip_cmd_space - Skip spaces in ESI
skip_cmd_space:
.scs_loop:
        cmp byte [esi], ' '
        jne .scs_done
        inc esi
        jmp .scs_loop
.scs_done:
        ret

; print_strip_ctcp - Print ESI string, stopping at 0x01 or NUL
print_strip_ctcp:
        PUSHALL
.psc_loop:
        mov al, [esi]
        cmp al, 0x01
        je .psc_done
        cmp al, 0
        je .psc_done
        push rsi
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rsi
        inc esi
        jmp .psc_loop
.psc_done:
        POPALL
        ret


;=======================================================================
; Data
;=======================================================================
msg_usage:      db "Usage: irc <server> [port] [nick]", 0x0A, 0
msg_connecting: db "Connecting to ", 0
msg_connected:  db "Connected!", 0x0A, 0
msg_dots:       db "...", 0x0A, 0
msg_dns_fail:   db "DNS resolution failed", 0x0A, 0
msg_sock_fail:  db "Failed to create socket", 0x0A, 0
msg_conn_fail:  db "Connection failed", 0x0A, 0
msg_disconnected: db "Disconnected from server", 0x0A, 0
msg_no_chan:    db "Not in a channel. Use /join #channel", 0x0A, 0
msg_unknown_cmd: db "Unknown command. Type /quit to exit.", 0x0A, 0
msg_has_joined: db " has joined ", 0
msg_has_left:   db " has left ", 0
msg_has_quit:   db " has quit", 0x0A, 0
msg_nick_change: db " is now known as ", 0

default_nick:   db "MelliUser", 0
default_quit:   db "Mellivora OS IRC", 0

cmd_nick_irc:   db "NICK ", 0
cmd_user_irc:   db "USER ", 0
user_suffix:    db " 0 * :Mellivora OS User", 0x0D, 0x0A, 0

str_privmsg:    db "PRIVMSG ", 0
str_join_irc:   db "JOIN ", 0
str_part_irc:   db "PART ", 0
str_quit_irc:   db "QUIT ", 0
str_list_irc:   db "LIST", 0
str_who_irc:    db "WHO ", 0
str_action_pre: db " :", 0x01, "ACTION ", 0

str_cmd_quit:   db "quit", 0
str_cmd_join:   db "join", 0
str_cmd_part:   db "part", 0
str_cmd_nick:   db "nick", 0
str_cmd_msg:    db "msg", 0
str_cmd_me:     db "me", 0
str_cmd_list:   db "list", 0
str_cmd_who:    db "who", 0

str_lt:         db "<", 0
str_gt_space:   db "> ", 0
str_newline:    db 0x0A, 0
str_space:      db " ", 0
str_star_space: db "* ", 0
str_arrow:      db "--> ", 0
str_arrow_left: db "<-- ", 0
str_notice_pre: db "[Notice] ", 0

hostname:   times 256 db 0
port:       dw IRC_PORT_DEFAULT
server_ip:  dd 0
sock_fd:    dd 0
my_nick:    times NICK_LEN db 0
current_chan: times CHAN_LEN db 0
sender_nick: times NICK_LEN db 0
msg_target: times CHAN_LEN db 0
input_buf:  times LINE_BUF_SIZE db 0
input_len:  dd 0
arg_buf:    times 256 db 0
recv_buf:   times RECV_BUF_SIZE db 0
send_buf:   times SEND_BUF_SIZE db 0
line_parse_buf: times LINE_BUF_SIZE db 0
