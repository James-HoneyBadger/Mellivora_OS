;=======================================================================
; remote.inc - the remote console: a command line driven from the wire
;
; NETWORK.md section 6 asks for "a terminal session over the share
; protocol's connection, authenticated". The hard part is not the transport,
; it is *which context runs the command*: the share server lives in the
; network receive path, which is no place to execute a shell command, and the
; shell is the only thing on this machine that can execute one.
;
; So the server does not run anything. It queues the line and answers later:
;
;   1. the server receives SH_MSG_CONSOLE{command} on an authenticated
;      session and records the command, the session and the sequence number
;      (rc_state = WAITING). No reply goes out yet.
;   2. the shell, idle at an empty prompt, takes the command (console_take),
;      starts capturing console output the same way `>` captures it, and runs
;      the line through its own dispatch (rc_state = RUNNING).
;   3. the shell comes back to its prompt, which is the moment the command is
;      over: capture stops and the output is put in the reply buffer
;      (console_capture_done, rc_state = READY).
;   4. the shell's idle loop flushes the reply on the session that asked
;      (console_flush), and the slot goes back to IDLE.
;
; One command at a time, and only while the local console is idle at an empty
; line: a remote client cannot take the machine away from somebody typing.
; A second request that arrives while one is running is answered at once with
; "busy" rather than queued, so a client is never left waiting on a queue it
; cannot see.
;
; What this is not:
;
;   - Not an independent session. The command runs in the console's own
;     context, so it sees the console's current directory, and a `cd` from a
;     remote client moves the local shell too. It is the machine's console,
;     driven from elsewhere - not a second terminal.
;   - No pty, no echo of what the client types, no window size. One command
;     line in, one block of output back.
;   - The authority is the share secret's, not a user's: the shell runs as
;     SYSTEM, so a client that holds the secret can run what SYSTEM can.
;     NETWORK.md section 6 records that as the gap it is.
;=======================================================================

RC_CMD_MAX      equ 256
RC_OUT_MAX      equ 1200               ; text that fits beside the status byte
RC_REPLY_MAX    equ RC_OUT_MAX + 1

RC_IDLE         equ 0
RC_WAITING      equ 1                  ; a line is queued for the shell
RC_RUNNING      equ 2                  ; the shell is executing it
RC_READY        equ 3                  ; the reply is waiting to be sent

RC_ST_OK        equ 0
RC_ST_TRUNC     equ 1
RC_ST_BUSY      equ 2
RC_ST_TOOLONG   equ 3

align 4
rc_state:   dd RC_IDLE
rc_sess:    dd 0
rc_seq:     dd 0
rc_len:     dd 0
rc_capturing: dd 0
rc_cap_len: dd 0
rc_cap_buf: times RC_OUT_MAX db 0
rc_cmd:     times RC_CMD_MAX db 0
rc_out:     times RC_REPLY_MAX db 0
rc_line:    times RC_CMD_MAX db 0
rc_linelen: dd 0
rc_status:  dd 0

rc_word_exit: db "exit", 0

;=======================================================================
; Server side
;=======================================================================

;-----------------------------------------------------------------------
; console_request - take one command off the wire
;   In : ESI = command text, ECX = its length (the message payload)
;
; A command that arrives while another is queued or running is refused with a
; status instead of being dropped: the client has to be able to tell "busy"
; from "the wire died".
;-----------------------------------------------------------------------
console_request:
        pushad
        mov [.crq_len], ecx
        cmp dword [rc_state], RC_IDLE
        je .crq_take
        mov dword [rc_status], RC_ST_BUSY
        call console_refuse
        popad
        ret
.crq_take:
        cmp ecx, RC_CMD_MAX - 1
        ja .crq_long
        mov ecx, [.crq_len]
        mov edi, rc_cmd
        cld
        rep movsb
        mov ecx, [.crq_len]
        mov byte [rc_cmd + ecx], 0
        mov eax, [share_cur_sess]
        mov [rc_sess], eax
        mov eax, [share_cur_seq]
        mov [rc_seq], eax
        mov dword [rc_state], RC_WAITING
        popad
        ret
.crq_long:
        mov dword [rc_status], RC_ST_TOOLONG
        call console_refuse
        popad
        ret
.crq_len:    dd 0

;-----------------------------------------------------------------------
; console_refuse - answer the current request with a status and no text
;   In : [rc_status] = the status byte
;-----------------------------------------------------------------------
console_refuse:
        pushad
        mov eax, [rc_status]
        mov [rc_out], al
        mov dword [share_send_type], SH_MSG_CONSOLE
        mov edi, [share_cur_sess]
        add edi, SH_S_KEY
        mov [share_send_key], edi
        mov esi, rc_out
        mov ecx, 1
        call share_session_send_raw
        popad
        ret

;-----------------------------------------------------------------------
; console_take - give the shell a queued command, if there is one
;   Out: CF set when a command was taken; then EDI = end of the line in
;        line_buffer and ECX = its length, which is what .execute wants
;-----------------------------------------------------------------------
console_take:
        cmp dword [rc_state], RC_WAITING
        jne .ct_no
        mov esi, rc_cmd
        mov edi, line_buffer
        call str_copy                  ; leaves EDI past the terminator
        mov esi, rc_cmd
        call rc_strlen
        mov ecx, eax
        mov edi, line_buffer
        add edi, eax                   ; the insert point, as the reader leaves it
        ; Capture from here. Not the shell's `>` buffer: the shell resets that
        ; after every command, which would empty the reply before it was built.
        ; vga_putchar consults this flag and keeps the bytes instead of
        ; painting them, so what a program the command launches prints is
        ; caught too.
        mov dword [rc_cap_len], 0
        mov dword [rc_capturing], 1
        mov dword [rc_state], RC_RUNNING
        stc
        ret
.ct_no:
        clc
        ret

;-----------------------------------------------------------------------
; console_capture_done - the command is over; turn the output into a reply
;
; Called where the shell prints its prompt, which is the one place both a
; builtin and a program reach when the command is done. A no-op unless a
; remote command was running.
;-----------------------------------------------------------------------
console_capture_done:
        pushad
        cmp dword [rc_state], RC_RUNNING
        jne .ccd_done
        mov dword [rc_capturing], 0
        mov byte [rc_out], RC_ST_OK
        mov ecx, [rc_cap_len]
        mov esi, rc_cap_buf
        ; The shell emits a newline before it runs a line; that is the prompt
        ; making room, not the command's output.
        cmp ecx, 0
        je .ccd_fits
        cmp byte [esi], 0x0A
        jne .ccd_fits
        inc esi
        dec ecx
.ccd_fits:
        cmp ecx, RC_OUT_MAX
        jbe .ccd_room
        mov ecx, RC_OUT_MAX
        mov byte [rc_out], RC_ST_TRUNC
.ccd_room:
        mov [rc_len], ecx
        mov edi, rc_out + 1
        cld
        rep movsb
        mov eax, [rc_len]
        inc eax                        ; the status byte travels with it
        mov [rc_len], eax
        mov dword [rc_state], RC_READY
.ccd_done:
        popad
        ret

;-----------------------------------------------------------------------
; console_flush - send a ready reply on the session that asked
;-----------------------------------------------------------------------
console_flush:
        pushad
        cmp dword [rc_state], RC_READY
        jne .cf_done
        mov edi, [rc_sess]
        cmp dword [edi + SH_S_STATE], SH_S_READY
        jne .cf_clear                  ; the client went away: drop the answer
        mov [share_cur_sess], edi
        mov eax, [rc_seq]
        mov [share_cur_seq], eax
        mov dword [share_send_type], SH_MSG_CONSOLE
        add edi, SH_S_KEY
        mov [share_send_key], edi
        mov esi, rc_out
        mov ecx, [rc_len]
        call share_session_send_raw
.cf_clear:
        mov dword [rc_state], RC_IDLE
        mov dword [rc_len], 0
.cf_done:
        popad
        ret

;-----------------------------------------------------------------------
; rc_strlen - In: ESI -> EAX = bytes before the terminator
;-----------------------------------------------------------------------
rc_strlen:
        push esi
        xor eax, eax
.rsl_loop:
        cmp byte [esi + eax], 0
        je .rsl_done
        inc eax
        jmp .rsl_loop
.rsl_done:
        pop esi
        ret

;=======================================================================
; Client side
;=======================================================================

;-----------------------------------------------------------------------
; rc_run - send one command and print its answer
;   In : ESI = command, ECX = length; an open session
;   Out: CF set when the transport failed
;-----------------------------------------------------------------------
rc_run:
        pushad
        mov al, SH_MSG_CONSOLE
        call share_client_call
        cmp eax, -1
        je .rr_fail
        cmp eax, 1
        jb .rr_fail
        mov [.rr_len], eax
        mov esi, share_cl_reply
        movzx eax, byte [esi]
        cmp eax, RC_ST_BUSY
        je .rr_busy
        cmp eax, RC_ST_TOOLONG
        je .rr_long
        mov [.rr_status], eax
        inc esi
        mov ecx, [.rr_len]
        dec ecx                        ; past the status byte
        ; The reply is not NUL-terminated: terminate it inside the buffer.
        cmp ecx, SHARE_BUF - 2
        jb .rr_term
        mov ecx, SHARE_BUF - 2
.rr_term:
        mov byte [esi + ecx], 0
        call vga_print
        cmp dword [.rr_status], RC_ST_TRUNC
        jne .rr_ok
        mov esi, msg_rc_truncated
        call vga_print
.rr_ok:
        popad
        clc
        ret
.rr_busy:
        mov esi, msg_rc_busy
        call vga_print
        popad
        clc
        ret
.rr_long:
        mov esi, msg_rc_toolong
        call vga_print
        popad
        clc
        ret
.rr_fail:
        mov esi, msg_rc_lost
        call vga_print
        popad
        stc
        ret
.rr_len:    dd 0
.rr_status: dd 0

;-----------------------------------------------------------------------
; rc_read_line - read one line from the console into rc_line
;   Out: EAX = its length; rc_line is NUL-terminated
;-----------------------------------------------------------------------
rc_read_line:
        pushad
        xor ecx, ecx
.rrl_loop:
        call kb_getchar
        cmp al, 0x0D
        je .rrl_done
        cmp al, 0x0A
        je .rrl_done
        cmp al, 0x08
        je .rrl_back
        cmp ecx, RC_CMD_MAX - 1
        jae .rrl_loop                  ; full: ignore the rest
        mov [rc_line + ecx], al
        inc ecx
        call vga_putchar               ; the shell echoes its own lines; this
        jmp .rrl_loop                  ; one has to echo for itself
.rrl_back:
        test ecx, ecx
        jz .rrl_loop
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .rrl_loop
.rrl_done:
        mov [rc_line + ecx], 0
        mov [rc_linelen], ecx
        mov al, 0x0A
        call vga_putchar
        mov eax, ecx
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; rc_session_loop - type commands at the far machine until an empty line
;-----------------------------------------------------------------------
rc_session_loop:
        pushad
        mov esi, msg_rc_session
        call vga_print
.rcs_loop:
        mov esi, msg_rc_prompt
        call vga_print
        call rc_read_line
        test eax, eax
        jz .rcs_done
        mov esi, rc_line
        mov edi, rc_word_exit
        call str_compare
        jz .rcs_done
        mov ecx, [rc_linelen]
        mov esi, rc_line
        call rc_run
        jc .rcs_done
        jmp .rcs_loop
.rcs_done:
        mov esi, msg_rc_closed
        call vga_print
        popad
        ret

;=======================================================================
; cmd_net_console - "net console HOST SHARE SECRET [COMMAND...]"
;
; The secret is not optional here, unlike the other verbs: a console that can
; be reached without one is the thing NETWORK.md section 6 says must not
; exist. With a command the call is one-shot, which is what a script wants;
; with none it is an interactive session.
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_console:
        pushad
        call skip_spaces
        mov edi, share_arg_host
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_host], 0
        je .nc_usage
        call skip_spaces
        mov edi, share_arg_share
        mov ecx, SHARE_NAME_LEN - 1
        call copy_word_n
        cmp byte [share_arg_share], 0
        je .nc_usage
        call skip_spaces
        mov edi, share_arg_secret
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_secret], 0
        je .nc_usage
        call skip_spaces
        mov [.nc_cmd], esi

        call rpc_resolve_host
        jc .nc_nohost
        mov eax, [share_arg_ip]
        call net_is_loopback
        jz .nc_self
        call rpc_open_session
        jc .nc_no_session

        mov esi, [.nc_cmd]
        cmp byte [esi], 0
        je .nc_interactive
        call rc_strlen
        mov ecx, eax
        mov esi, [.nc_cmd]
        call rc_run
        call share_client_close
        popad
        ret
.nc_interactive:
        call rc_session_loop
        call share_client_close
        popad
        ret
.nc_self:
        ; The console is the thing that would execute the command, and it is
        ; the thing waiting for the answer: an address on this machine would
        ; deadlock until the client's deadline. Say so instead.
        mov esi, msg_rc_self
        call vga_print
        popad
        ret
.nc_nohost:
        mov esi, msg_share_no_host
        call vga_print
        popad
        ret
.nc_no_session:
        call rpc_report_open
        popad
        ret
.nc_usage:
        mov esi, msg_rc_usage
        call vga_print
        popad
        ret
.nc_cmd: dd 0

;=======================================================================
; Strings
;=======================================================================
msg_rc_usage:     db "net console HOST SHARE SECRET [COMMAND]", 0x0A
                  db "  Run one command on the other machine, or with none an", 0x0A
                  db "  interactive session until an empty line or 'exit'.", 0x0A, 0
msg_rc_prompt:    db "remote:/> ", 0
msg_rc_session:   db "Remote console: an empty line or 'exit' closes it.", 0x0A, 0
msg_rc_closed:    db "remote: session closed", 0x0A, 0
msg_rc_busy:      db "remote: the machine is running another command", 0x0A, 0
msg_rc_toolong:   db "remote: the command is too long for one message", 0x0A, 0
msg_rc_truncated: db "remote: (output truncated)", 0x0A, 0
msg_rc_lost:      db "remote: the connection failed", 0x0A, 0
msg_rc_self:      db "remote: that address is this machine; a console needs another", 0x0A, 0
