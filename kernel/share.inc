;=======================================================================
; share.inc - the share protocol: a folder exported to another machine
;
; docs/spec/NETWORK.md section 4. One protocol, our own ([D5] in the
; roadmap): SMB is a multi-year compatibility project and a half-finished
; one satisfies nobody. A share is a directory exported under a name; a
; client authenticates with the share's secret, then lists, stats and reads
; the files under it.
;
; One message is a 16-byte header, a payload, and a 16-byte MAC:
;
;    0  magic    "MVSH" in wire order
;    4  version  1
;    5  type     one of SH_MSG_*
;    6  flags    0 (reserved)
;    8  seq      request id, echoed by the reply
;   12  length   payload bytes that follow
;
; The MAC is BLAKE2b(key || BLAKE2b(header || payload)), truncated to 16
; bytes: the outer hash binds the key and the inner one fixes the message
; length, so this is not a plain secret-prefix MAC.
;
; Handshake, one connection per session:
;
;   client  HELLO{share name, 16 bytes}     no MAC: there is no key yet
;   server  CHALLENGE{nonce, 16 bytes}      no MAC
;   client  AUTH{proof, 16 bytes}           keyed with H
;   server  OK / DENY{reason, 1 byte}       OK keyed with H, DENY bare
;   ...     requests and replies            keyed with the session key
;
; H is BLAKE2b(secret)[0..31], so the secret never crosses the wire and the
; server stores only H. proof = BLAKE2b(H || nonce)[0..15]; the session key
; is BLAKE2b(H || nonce || "MVSH-K")[0..31], fresh per connection, so a
; recorded exchange cannot be replayed against a later one. DENY carries no
; MAC because the peer may not have chosen a share yet - it is a refusal,
; not state a client can act on.
;
; VIEW is answered without a session on purpose: a browse list of names is
; disclosure, not access, and `net view` on a machine whose secret you do not
; have is still useful. Everything that touches a file needs a session.
;=======================================================================

SHARE_PORT         equ 4450
SHARE_MAX          equ 8            ; exported directories
SHARE_NAME_LEN     equ 16
SHARE_PATH_LEN     equ 64
SHARE_KEY_LEN      equ 32           ; half of BLAKE2b's output: H and the session key
SHARE_MAC_LEN      equ 16
SHARE_MAX_SESS     equ 4
SHARE_HDR_LEN      equ 16
SHARE_BUF          equ 1400         ; one message payload
SHARE_RX           equ 1500         ; per-session accumulation buffer
SHARE_LOGIN_TICKS  equ 300          ; 3 s to finish the handshake
SHARE_IDLE_TICKS   equ 6000         ; 60 s of silence ends a session
SHARE_CLIENT_TICKS equ 500          ; 5 s for one request to be answered

; Header fields
SH_H_MAGIC        equ 0
SH_H_VERSION      equ 4
SH_H_TYPE         equ 5
SH_H_FLAGS        equ 6
SH_H_SEQ          equ 8
SH_H_LEN          equ 12

; "MVSH" as the little-endian dword the header holds.
SH_MAGIC_VALUE    equ 0x4853564D

; Message types
SH_MSG_HELLO      equ 1
SH_MSG_CHALLENGE  equ 2
SH_MSG_AUTH       equ 3
SH_MSG_OK         equ 4
SH_MSG_DENY       equ 5
SH_MSG_VIEW       equ 0x10
SH_MSG_LIST       equ 0x11
SH_MSG_STAT       equ 0x12
SH_MSG_READ       equ 0x13
SH_MSG_WRITE      equ 0x14
SH_MSG_SVC        equ 0x15
SH_MSG_LOGON      equ 0x16
SH_MSG_LOGONPROOF equ 0x17
SH_MSG_PRINT      equ 0x18

; Refusal reasons
SH_ERR_NO_SHARE   equ 1
SH_ERR_BAD_PROOF  equ 2
SH_ERR_READ_ONLY  equ 3
SH_ERR_NO_ACCOUNT equ 4
SH_ERR_NO_SERVICE equ 5
SH_ERR_NO_SPOOL   equ 6             ; the server cannot open its spool
SH_ERR_WRITE      equ 7             ; the job could not be stored
SH_ERR_SPOOL_FULL equ 8             ; every job slot is taken
SH_ERR_TOO_BIG    equ 9             ; the job is larger than the spool buffer

; Session states
SH_S_FREE         equ 0
SH_S_HELLO        equ 1
SH_S_AUTH         equ 2
SH_S_READY        equ 3

; Session record
SH_S_STATE        equ 0            ; dword
SH_S_SOCK         equ 4            ; dword socket index, -1 when unused
SH_S_SHARE        equ 8            ; dword share slot
SH_S_DEADLINE     equ 12           ; dword tick count that ends the session
SH_S_RXLEN        equ 16           ; dword bytes in this session's buffer
SH_S_KEY          equ 20           ; 32 bytes: H during the handshake, then the session key
SH_S_NONCE        equ 52           ; 16 bytes challenge
SH_S_RX           equ 68           ; SHARE_RX bytes
SH_S_LUSER        equ (SH_S_RX + SHARE_RX) ; 16 bytes: account named by LOGON
SH_S_SIZE         equ (SH_S_LUSER + 16)

; Share record
SH_E_USED         equ 0            ; dword: 1 = exported
SH_E_NAME         equ 4            ; SHARE_NAME_LEN bytes
SH_E_PATH         equ 20           ; SHARE_PATH_LEN bytes
SH_E_HASH         equ 84           ; SHARE_KEY_LEN bytes: BLAKE2b(secret)
SH_E_MODE         equ 116          ; dword: SH_MODE_RO / SH_MODE_RW
SH_E_SIZE         equ 120

SH_MODE_RO        equ 0            ; a share grants nothing the mode does not
SH_MODE_RW        equ 1

;=======================================================================
; share_hash - BLAKE2b, 32 bytes
;   In : ESI = data, ECX = length, EDI = output
;=======================================================================
share_hash:
        pushad
        mov [.sh_out], edi
        mov [.sh_data], esi
        mov [.sh_len], ecx
        mov edi, a2_lock
        call spin_lock
        mov eax, share_hash_out
        mov edx, [.sh_data]
        mov ebx, [.sh_len]
        mov ecx, SHARE_KEY_LEN
        call b2b_hash
        mov edi, a2_lock
        call spin_unlock
        mov esi, share_hash_out
        mov edi, [.sh_out]
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        popad
        ret
.sh_out:  dd 0
.sh_data: dd 0
.sh_len:  dd 0

;=======================================================================
; share_mac - 16-byte MAC over a whole message
;   In : ESI = message (header + payload), ECX = length, EDI = 32-byte key
;   Out: EAX = pointer to the 16-byte MAC
;=======================================================================
share_mac:
        pushad
        mov [.sm_msg], esi
        mov [.sm_len], ecx
        mov [.sm_key], edi
        mov edi, a2_lock
        call spin_lock
        mov eax, share_mac_inner
        mov edx, [.sm_msg]
        mov ebx, [.sm_len]
        mov ecx, SHARE_KEY_LEN
        call b2b_hash
        ; outer = BLAKE2b(key || inner)
        mov esi, [.sm_key]
        mov edi, share_mac_scratch
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov esi, share_mac_inner
        mov ecx, SHARE_KEY_LEN
        rep movsb
        mov eax, share_mac_out
        mov edx, share_mac_scratch
        mov ebx, SHARE_KEY_LEN * 2
        mov ecx, SHARE_MAC_LEN
        call b2b_hash
        mov edi, a2_lock
        call spin_unlock
        popad
        mov eax, share_mac_out
        ret
.sm_msg: dd 0
.sm_len: dd 0
.sm_key: dd 0

;=======================================================================
; share_random - fill a buffer from the generator /dev/random uses
;   In : EDI = buffer, ECX = bytes
;
; A nonce only has to be one the peer has not seen before - freshness is what
; stops a replay - and the proof's strength comes from the secret, so a cheap
; generator is the right tool.
;=======================================================================
share_random:
        pushad
        mov [.sr_buf], edi
        mov [.sr_len], ecx
        mov eax, [tick_count]
        xor [dev_rng_state], eax
        mov edi, [.sr_buf]
        mov ecx, [.sr_len]
.sr_loop:
        test ecx, ecx
        jz .sr_done
        mov eax, [dev_rng_state]
        mov ebx, eax
        shl ebx, 13
        xor eax, ebx
        mov ebx, eax
        shr ebx, 17
        xor eax, ebx
        mov ebx, eax
        shl ebx, 5
        xor eax, ebx
        mov [dev_rng_state], eax
        stosb
        dec ecx
        jmp .sr_loop
.sr_done:
        popad
        ret
.sr_buf: dd 0
.sr_len: dd 0

;=======================================================================
; share_enter - enter a directory quietly, remembering where we were
;   In : ESI = path
;   Out: CF set if the path is not there
;
; Listing is a property of the current directory in this tree, so a server
; that answers LIST has to stand in the share for the length of the request.
; The console stays quiet: a client browsing a folder must not paint paths on
; the machine it is browsing.
;=======================================================================
share_enter:
        pushad
        call path_save_cwd
        mov byte [console_suppress], 1
        call cmd_cd_internal
        mov byte [console_suppress], 0
        test eax, eax
        js .se_fail
        popad
        clc
        ret
.se_fail:
        call path_restore_cwd
        popad
        stc
        ret

share_leave:
        pushad
        call path_restore_cwd
        popad
        ret

;=======================================================================
; share_find - look up an exported name
;   In : ESI = name
;   Out: EAX = share slot or -1
;=======================================================================
share_find:
        pushad
        mov [.sf_name], esi
        mov edi, share_table
        xor ecx, ecx
.sf_loop:
        cmp ecx, SHARE_MAX
        jge .sf_none
        cmp dword [edi + SH_E_USED], 0
        je .sf_next
        push edi
        push ecx
        mov esi, [.sf_name]
        add edi, SH_E_NAME
        call str_compare
        pop ecx
        pop edi
        test eax, eax
        jz .sf_found
.sf_next:
        add edi, SH_E_SIZE
        inc ecx
        jmp .sf_loop
.sf_found:
        mov [esp + 28], ecx
        popad
        ret
.sf_none:
        popad
        mov eax, -1
        ret
.sf_name: dd 0

;   In : EAX = share slot
;   Out: EDI = record pointer
share_slot_ptr:
        imul eax, SH_E_SIZE
        add eax, share_table
        mov edi, eax
        ret

;=======================================================================
; share_hash_secret - H for a secret string
;   In : ESI = secret (0 or empty = the empty secret)
;   Out: share_secret_tmp
;=======================================================================
share_hash_secret:
        pushad
        test esi, esi
        jz .shs_empty
        cmp byte [esi], 0
        jne .shs_have
.shs_empty:
        mov esi, share_empty_secret
.shs_have:
        mov ebx, esi
        xor ecx, ecx
.shs_len:
        cmp byte [ebx + ecx], 0
        je .shs_go
        inc ecx
        jmp .shs_len
.shs_go:
        mov edi, share_secret_tmp
        call share_hash
        popad
        ret

;=======================================================================
; share_add - export a directory, replacing a share of the same name
;   In : ESI = name, EDI = path, EDX = secret (0 = none), EBX = SH_MODE_*
;   Out: EAX = 0, or -1 when the table is full
;=======================================================================
share_add:
        pushad
        mov [.sa_name], esi
        mov [.sa_path], edi
        mov [.sa_secret], edx
        mov [.sa_mode], ebx

        call share_find
        cmp eax, -1
        jne .sa_have
        mov edi, share_table
        xor ecx, ecx
.sa_scan:
        cmp ecx, SHARE_MAX
        jge .sa_full
        cmp dword [edi + SH_E_USED], 0
        je .sa_free
        add edi, SH_E_SIZE
        inc ecx
        jmp .sa_scan
.sa_free:
        mov eax, ecx
.sa_have:
        mov [.sa_slot], eax
        call share_slot_ptr
        push edi
        mov ecx, SH_E_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        pop edi
        mov dword [edi + SH_E_USED], 1
        mov eax, [.sa_mode]
        mov [edi + SH_E_MODE], eax

        ; name, NUL-terminated inside its field
        mov esi, [.sa_name]
        push edi
        add edi, SH_E_NAME
        mov ecx, SHARE_NAME_LEN - 1
        call copy_word_n
        pop edi

        ; path
        mov esi, [.sa_path]
        push edi
        add edi, SH_E_PATH
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n
        pop edi

        ; H for the secret
        mov esi, [.sa_secret]
        call share_hash_secret
        mov eax, [.sa_slot]
        call share_slot_ptr
        add edi, SH_E_HASH
        mov esi, share_secret_tmp
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb

        call share_recount
        popad
        xor eax, eax
        ret
.sa_full:
        popad
        mov eax, -1
        ret
.sa_name:   dd 0
.sa_path:   dd 0
.sa_secret: dd 0
.sa_slot:   dd 0
.sa_mode:   dd 0

; Count the exports that are in use.
share_recount:
        pushad
        mov edi, share_table
        xor ecx, ecx
        xor edx, edx
.sr_count:
        cmp ecx, SHARE_MAX
        jge .sr_store
        cmp dword [edi + SH_E_USED], 0
        je .sr_next
        inc edx
.sr_next:
        add edi, SH_E_SIZE
        inc ecx
        jmp .sr_count
.sr_store:
        mov [share_count], edx
        popad
        ret

;=======================================================================
; share_del - stop exporting a name
;   In : ESI = name
;   Out: EAX = 0, -1 when the name is not exported
;=======================================================================
share_del:
        pushad
        call share_find
        cmp eax, -1
        je .sd_none
        call share_slot_ptr
        mov ecx, SH_E_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        call share_recount
        popad
        xor eax, eax
        ret
.sd_none:
        popad
        mov eax, -1
        ret

;=======================================================================
; share_listener - the listening socket, created on first use
;   Out: EAX = socket index, -1 on failure
;=======================================================================
share_listener:
        pushad
        mov eax, [share_listen_idx]
        cmp eax, -1
        jne .sl_have
        mov eax, SOCK_TCP
        call sock_alloc
        cmp eax, -1
        je .sl_fail
        mov [share_listen_idx], eax
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        mov word [ebp + SOCK_LOCAL_PORT], SHARE_PORT
        mov dword [ebp + SOCK_STATE], TCP_LISTEN
.sl_have:
        mov eax, [share_listen_idx]
        popad
        ret
.sl_fail:
        popad
        mov eax, -1
        ret

;=======================================================================
; share_accept - take an established connection to the listener, if any
;   Out: EAX = socket index or -1 (never blocks)
;=======================================================================
share_accept:
        pushad
        mov eax, [share_listen_idx]
        cmp eax, -1
        je .sa_none
        mov [.sa_parent], eax
        mov edi, net_sock_table
        xor ecx, ecx
.sa_scan:
        cmp ecx, MAX_SOCKETS
        jge .sa_none
        cmp dword [edi + SOCK_TYPE], SOCK_TCP
        jne .sa_next
        mov eax, [edi + SOCK_PARENT]
        cmp eax, [.sa_parent]
        jne .sa_next
        cmp dword [edi + SOCK_STATE], TCP_ESTABLISHED
        je .sa_found
.sa_next:
        add edi, SOCK_SIZE
        inc ecx
        jmp .sa_scan
.sa_found:
        mov dword [edi + SOCK_PARENT], -1
        mov [esp + 28], ecx
        popad
        ret
.sa_none:
        popad
        mov eax, -1
        ret
.sa_parent: dd 0

;=======================================================================
; share_send_frame - build and transmit one message
;   In : AL = type, EBX = seq, ESI = payload (may be 0), ECX = payload length,
;        EDI = 32-byte key (0 = no MAC), EBP = socket record
;=======================================================================
share_send_frame:
        pushad
        mov [.sf_type], al
        mov [.sf_key], edi
        mov [.sf_sock], ebp
        mov [.sf_len], ecx
        mov [.sf_pay], esi
        mov [.sf_seq], ebx

        mov edi, share_tx_buf
        mov dword [edi + SH_H_MAGIC], SH_MAGIC_VALUE
        mov byte [edi + SH_H_VERSION], 1
        mov al, [.sf_type]
        mov byte [edi + SH_H_TYPE], al
        mov word [edi + SH_H_FLAGS], 0
        mov eax, [.sf_seq]
        mov [edi + SH_H_SEQ], eax
        mov eax, [.sf_len]
        mov [edi + SH_H_LEN], eax

        mov esi, [.sf_pay]
        test esi, esi
        jz .sf_no_payload
        cmp dword [.sf_len], 0
        je .sf_no_payload
        lea edi, [share_tx_buf + SHARE_HDR_LEN]
        mov ecx, [.sf_len]
        cld
        rep movsb
.sf_no_payload:
        mov ecx, SHARE_HDR_LEN
        add ecx, [.sf_len]
        mov [.sf_total], ecx
        mov edi, [.sf_key]
        test edi, edi
        jz .sf_send
        mov esi, share_tx_buf
        call share_mac
        mov esi, eax
        lea edi, [share_tx_buf]
        add edi, [.sf_total]
        mov ecx, SHARE_MAC_LEN
        cld
        rep movsb
        add dword [.sf_total], SHARE_MAC_LEN

.sf_send:
        mov ebp, [.sf_sock]
        mov ecx, [.sf_total]
        mov esi, share_tx_buf
        call tcp_send_data
        popad
        clc
        ret
.sf_type:  dd 0
.sf_key:   dd 0
.sf_sock:  dd 0
.sf_len:   dd 0
.sf_pay:   dd 0
.sf_seq:   dd 0
.sf_total: dd 0

;=======================================================================
; share_drain - move everything a socket has into a buffer
;   In : EBP = socket record, EDI = destination, EDX = capacity
;   Out: EAX = bytes moved
;=======================================================================
share_drain:
        pushad
        mov [.sd_dst], edi
        mov eax, [ebp + SOCK_RECV_LEN]
        test eax, eax
        jz .sd_none
        cmp eax, edx
        jbe .sd_take
        mov eax, edx
.sd_take:
        mov [.sd_n], eax
        mov ecx, eax
        mov esi, [ebp + SOCK_RECV_BUF]
        mov edi, [.sd_dst]
        cld
        rep movsb
        ; Drop what was taken and shift the rest down, as SYS_RECV does.
        mov ecx, [ebp + SOCK_RECV_LEN]
        sub ecx, [.sd_n]
        jz .sd_shifted
        mov esi, [ebp + SOCK_RECV_BUF]
        add esi, [.sd_n]
        mov edi, [ebp + SOCK_RECV_BUF]
        cld
        rep movsb
.sd_shifted:
        mov eax, [.sd_n]
        sub [ebp + SOCK_RECV_LEN], eax
        mov [esp + 28], eax
        popad
        ret
.sd_none:
        popad
        xor eax, eax
        ret
.sd_dst: dd 0
.sd_n:   dd 0

;=======================================================================
; share_close_socket - shut a kernel-owned socket down and release it
;   In : EAX = socket index (-1 is ignored)
;=======================================================================
share_close_socket:
        pushad
        cmp eax, -1
        je .scs_done
        ; Close the connection, not just the local socket. The server frees a
        ; session when the peer's FIN arrives; a close it never hears about
        ; holds one of four slots until the 60-second deadline, and a handful
        ; of finished commands then lock out the next connection.
        call sock_close
.scs_done:
        popad
        ret

;=======================================================================
; share_session_free - drop a session
;   In : EDI = session record
;=======================================================================
share_session_free:
        pushad
        mov [.ssf_rec], edi
        mov eax, [edi + SH_S_SOCK]
        call share_close_socket
        mov edi, [.ssf_rec]
        mov ecx, SH_S_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        ; EDI is past the record after the store string: the "unused" marker
        ; has to be written through the record pointer, or it lands on the
        ; next session's state.
        mov edi, [.ssf_rec]
        mov dword [edi + SH_S_SOCK], -1
        popad
        ret
.ssf_rec: dd 0

;=======================================================================
; share_server_poll - accept and answer, one pass over every session
;
; Called from the shell's idle loop and from the client's own wait, so a
; client on this machine and a client across the wire are served by the same
; code and neither needs a second task.
;=======================================================================
share_server_poll:
        pushad
        cmp dword [share_count], 0
        je .ssp_done
        ; A SYN that arrives while the stack is in its receive path leaves its
        ; SYN-ACK deferred, and nothing in this server path would ever send
        ; it: the accept loop below is the non-ISR context that flushes.
        call tcp_flush_all_pending
        call share_listener
        cmp eax, -1
        je .ssp_done

        call share_accept
        cmp eax, -1
        je .ssp_serve
        mov [.ssp_new], eax
        mov edi, share_session_table
        xor ecx, ecx
.ssp_slot:
        cmp ecx, SHARE_MAX_SESS
        jge .ssp_reject
        cmp dword [edi + SH_S_STATE], SH_S_FREE
        je .ssp_slot_ok
        add edi, SH_S_SIZE
        inc ecx
        jmp .ssp_slot
.ssp_slot_ok:
        mov dword [edi + SH_S_STATE], SH_S_HELLO
        mov eax, [.ssp_new]
        mov [edi + SH_S_SOCK], eax
        mov dword [edi + SH_S_SHARE], -1
        mov dword [edi + SH_S_RXLEN], 0
        mov eax, [tick_count]
        add eax, SHARE_LOGIN_TICKS
        mov [edi + SH_S_DEADLINE], eax
        ; The challenge goes out in answer to HELLO, not before it: two
        ; challenges would leave the second one in the client's stream.
        push edi
        add edi, SH_S_NONCE
        mov ecx, 16
        call share_random
        pop edi
        jmp .ssp_serve

.ssp_reject:
        mov eax, [.ssp_new]
        call share_close_socket

.ssp_serve:
        mov edi, share_session_table
        xor ecx, ecx
.ssp_loop:
        cmp ecx, SHARE_MAX_SESS
        jge .ssp_done
        cmp dword [edi + SH_S_STATE], SH_S_FREE
        je .ssp_next
        push ecx
        push edi
        call share_session_service
        pop edi
        pop ecx
.ssp_next:
        add edi, SH_S_SIZE
        inc ecx
        jmp .ssp_loop
.ssp_done:
        popad
        ret
.ssp_new: dd 0

;=======================================================================
; share_session_service - read and answer whatever one session has waiting
;   In : EDI = session record
;=======================================================================
share_session_service:
        pushad
        mov [.ssv_rec], edi

        ; A peer that closed its end leaves the socket in CLOSE_WAIT, or the
        ; stack drops it entirely: free the slot at once rather than holding
        ; it to the deadline, or four finished commands lock out the fifth.
        mov eax, [edi + SH_S_SOCK]
        cmp eax, -1
        je .ssv_dead
        imul eax, SOCK_SIZE
        mov eax, [net_sock_table + eax + SOCK_STATE]
        cmp eax, TCP_CLOSE_WAIT
        je .ssv_dead
        cmp eax, TCP_CLOSED
        je .ssv_dead
        cmp eax, TCP_LAST_ACK
        je .ssv_dead
        cmp eax, TCP_CLOSING
        je .ssv_dead
        cmp eax, TCP_TIME_WAIT
        je .ssv_dead
.ssv_live:

        ; Deadlines: a handshake that never finishes, or a session gone quiet.
        mov eax, [tick_count]
        cmp eax, [edi + SH_S_DEADLINE]
        jb .ssv_read
.ssv_dead:
        call share_session_free
        popad
        ret

.ssv_read:
        mov eax, [edi + SH_S_SOCK]
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        mov edi, [.ssv_rec]
        mov ecx, [edi + SH_S_RXLEN]
        add edi, SH_S_RX
        add edi, ecx                    ; append after what is buffered
        mov edx, SHARE_RX
        sub edx, ecx
        test edx, edx
        jz .ssv_parse                   ; buffer full: parse before reading more
        call share_drain
        mov edi, [.ssv_rec]
        add [edi + SH_S_RXLEN], eax
.ssv_parse:
        ; One message at a time: a client waits for its reply before it sends
        ; the next request, so there is never a queue to walk.
        mov edi, [.ssv_rec]
        call share_session_parse
        popad
        ret
.ssv_rec: dd 0

; Send the current request's type as a reply carrying one byte of reason.
;   In : AL = reason, EDI = key (0 for none)
share_send_reason:
        pushad
        mov [share_reason], al
        mov [share_send_key], edi
        mov esi, share_reason
        call share_session_send_raw
        popad
        ret

; Send [share_reply_buf]/[share_reply_len] as the reply, keyed with the
; session key.
share_session_reply:
        pushad
        mov eax, [share_cur_type]
        mov [share_send_type], al
        mov edi, [share_cur_sess]
        add edi, SH_S_KEY
        mov [share_send_key], edi
        mov esi, share_reply_buf
        mov ecx, [share_reply_len]
        call share_session_send_raw
        popad
        ret

;   In : [share_send_type] = message type, ESI = payload, ECX = length,
;        [share_send_key] = key or 0
share_session_send_raw:
        pushad
        mov [.ssr_pay], esi
        mov [.ssr_len], ecx
        mov eax, [share_cur_sess]
        mov eax, [eax + SH_S_SOCK]
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        mov ebx, [share_cur_seq]
        mov edi, [share_send_key]
        mov esi, [.ssr_pay]
        mov ecx, [.ssr_len]
        mov al, [share_send_type]
        call share_send_frame
        popad
        ret
.ssr_pay: dd 0
.ssr_len: dd 0

;=======================================================================
; share_session_parse - handle the first complete message in a session
;   In : EDI = session record
;
; A framing or authentication failure drops the connection: a peer that
; cannot produce a valid message is not one to keep talking to.
;=======================================================================
share_session_parse:
        pushad
        mov [share_cur_sess], edi
        cmp dword [edi + SH_S_RXLEN], SHARE_HDR_LEN
        jb .spr_done
        lea esi, [edi + SH_S_RX]
        cmp dword [esi + SH_H_MAGIC], SH_MAGIC_VALUE
        jne .spr_drop
        cmp byte [esi + SH_H_VERSION], 1
        jne .spr_drop
        mov eax, [esi + SH_H_LEN]
        cmp eax, SHARE_BUF
        ja .spr_drop
        movzx ecx, byte [esi + SH_H_TYPE]
        mov [share_cur_type], ecx
        mov eax, [esi + SH_H_SEQ]
        mov [share_cur_seq], eax

        ; How long is the whole message?
        mov eax, [esi + SH_H_LEN]
        add eax, SHARE_HDR_LEN
        mov [share_cur_body], eax       ; header + payload
        cmp dword [share_cur_type], SH_MSG_HELLO
        je .spr_len_ok
        cmp dword [share_cur_type], SH_MSG_DENY
        je .spr_len_ok
        cmp dword [share_cur_type], SH_MSG_VIEW
        je .spr_len_ok
        cmp dword [share_cur_type], SH_MSG_LOGON
        je .spr_len_ok
        cmp dword [share_cur_type], SH_MSG_LOGONPROOF
        je .spr_len_ok
        add eax, SHARE_MAC_LEN
.spr_len_ok:
        mov [share_cur_total], eax
        mov edi, [share_cur_sess]
        cmp [edi + SH_S_RXLEN], eax
        jb .spr_done                    ; not all here yet

        ; Verify the MAC where the protocol carries one.
        cmp dword [share_cur_type], SH_MSG_HELLO
        je .spr_dispatch
        cmp dword [share_cur_type], SH_MSG_DENY
        je .spr_dispatch
        cmp dword [share_cur_type], SH_MSG_VIEW
        je .spr_dispatch
        cmp dword [share_cur_type], SH_MSG_LOGON
        je .spr_dispatch
        cmp dword [share_cur_type], SH_MSG_LOGONPROOF
        je .spr_dispatch
        mov edi, [share_cur_sess]
        add edi, SH_S_KEY
        mov ecx, [share_cur_body]
        mov esi, [share_cur_sess]
        add esi, SH_S_RX
        call share_mac
        mov esi, eax
        mov edi, [share_cur_sess]
        add edi, SH_S_RX
        add edi, [share_cur_body]
        mov ecx, SHARE_MAC_LEN
.spr_mac_cmp:
        lodsb
        cmp al, [edi]
        jne .spr_drop
        inc edi
        dec ecx
        jnz .spr_mac_cmp

.spr_dispatch:
        mov eax, [share_cur_type]
        cmp eax, SH_MSG_HELLO
        je .spr_hello
        cmp eax, SH_MSG_AUTH
        je .spr_auth
        cmp eax, SH_MSG_VIEW
        je .spr_view
        cmp eax, SH_MSG_LIST
        je .spr_list
        cmp eax, SH_MSG_STAT
        je .spr_stat
        cmp eax, SH_MSG_READ
        je .spr_read
        cmp eax, SH_MSG_WRITE
        je .spr_write
        cmp eax, SH_MSG_SVC
        je .spr_svc
        cmp eax, SH_MSG_LOGON
        je .spr_logon
        cmp eax, SH_MSG_LOGONPROOF
        je .spr_logonproof
        cmp eax, SH_MSG_PRINT
        je .spr_print
        jmp .spr_drop

;--- HELLO: choose the share, answer with a challenge --------------------
.spr_hello:
        mov edi, [share_cur_sess]
        cmp dword [edi + SH_S_STATE], SH_S_HELLO
        jne .spr_drop
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_find
        cmp eax, -1
        je .spr_deny_share
        mov edi, [share_cur_sess]
        mov [edi + SH_S_SHARE], eax
        ; H becomes the handshake key
        call share_slot_ptr
        add edi, SH_E_HASH
        mov esi, edi
        mov edi, [share_cur_sess]
        add edi, SH_S_KEY
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov edi, [share_cur_sess]
        mov dword [edi + SH_S_STATE], SH_S_AUTH
        lea esi, [edi + SH_S_NONCE]
        mov ecx, 16
        mov dword [share_send_type], SH_MSG_CHALLENGE
        mov dword [share_send_key], 0
        call share_session_send_raw
        call share_consume
        jmp .spr_done

.spr_deny_share:
        mov al, SH_ERR_NO_SHARE
        jmp .spr_deny

;--- AUTH: proof = BLAKE2b(H || nonce)[0..15] ---------------------------
.spr_auth:
        mov edi, [share_cur_sess]
        cmp dword [edi + SH_S_STATE], SH_S_AUTH
        jne .spr_drop
        lea esi, [edi + SH_S_KEY]
        mov edi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_NONCE]
        mov edi, share_auth_tmp + SHARE_KEY_LEN
        mov ecx, 16
        cld
        rep movsb
        mov esi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN + 16
        mov edi, share_auth_out
        call share_hash

        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        mov edi, share_auth_out
        mov ecx, SHARE_MAC_LEN
.spr_proof_cmp:
        lodsb
        cmp al, [edi]
        jne .spr_deny_proof
        inc edi
        dec ecx
        jnz .spr_proof_cmp

        ; OK is the last message keyed with H, so it goes out before the key
        ; becomes the session key.
        mov edi, [share_cur_sess]
        mov dword [edi + SH_S_STATE], SH_S_READY
        mov eax, [tick_count]
        add eax, SHARE_IDLE_TICKS
        mov [edi + SH_S_DEADLINE], eax
        add edi, SH_S_KEY
        xor al, al
        mov dword [share_send_type], SH_MSG_OK
        call share_send_reason
        call share_consume

        ; Session key = BLAKE2b(H || nonce || tag)
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_KEY]
        mov edi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_NONCE]
        mov edi, share_auth_tmp + SHARE_KEY_LEN
        mov ecx, 16
        cld
        rep movsb
        mov esi, share_key_tag
        mov edi, share_auth_tmp + SHARE_KEY_LEN + 16
        mov ecx, 7
        cld
        rep movsb
        mov esi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN + 16 + 7
        mov edi, share_key_new
        call share_hash
        mov edi, [share_cur_sess]
        lea edi, [edi + SH_S_KEY]
        mov esi, share_key_new
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        jmp .spr_done

.spr_deny_proof:
        mov al, SH_ERR_BAD_PROOF
        jmp .spr_deny

;--- VIEW ---------------------------------------------------------------
.spr_view:
        ; A browse: allowed before a session exists, answered without a key.
        call share_view_build
        mov eax, [share_cur_type]
        mov [share_send_type], al
        mov dword [share_send_key], 0
        mov esi, share_reply_buf
        mov ecx, [share_reply_len]
        call share_session_send_raw
        call share_consume
        jmp .spr_done

;--- LIST ---------------------------------------------------------------
.spr_list:
        call share_require_ready
        jc .spr_drop
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_list_build
        call share_session_reply
        call share_consume
        jmp .spr_done

;--- STAT ---------------------------------------------------------------
.spr_stat:
        call share_require_ready
        jc .spr_drop
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_stat_build
        call share_session_reply
        call share_consume
        jmp .spr_done

;--- READ ---------------------------------------------------------------
.spr_read:
        call share_require_ready
        jc .spr_drop
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_read_build
        call share_session_reply
        call share_consume
        jmp .spr_done

;--- WRITE: offset, length, path, then the bytes -------------------------
;
; The reply is a dword of bytes written, or nothing when the share refuses.
; A share is read-only unless it was exported with /rw, and that check comes
; before the filename is even looked at.
.spr_write:
        call share_require_ready
        jc .spr_drop
        mov dword [share_reply_sent], 0
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_write_apply
        cmp dword [share_reply_sent], 0
        jne .spr_write_answered
        call share_session_reply
.spr_write_answered:
        call share_consume
        jmp .spr_done

;--- SVC: service control on the server's machine ------------------------
.spr_svc:
        call share_require_ready
        jc .spr_drop
        mov dword [share_reply_sent], 0
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_svc_apply
        cmp dword [share_reply_sent], 0
        jne .spr_svc_answered
        call share_session_reply
.spr_svc_answered:
        call share_consume
        jmp .spr_done

;--- PRINT: queue a job on the server's spooler --------------------------
.spr_print:
        call share_require_ready
        jc .spr_drop
        mov dword [share_reply_sent], 0
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_print_apply
        cmp dword [share_reply_sent], 0
        jne .spr_print_answered
        call share_session_reply
.spr_print_answered:
        call share_consume
        jmp .spr_done

;--- LOGON: the user's password is proved, never sent -------------------
.spr_logon:
        mov edi, [share_cur_sess]
        cmp dword [edi + SH_S_STATE], SH_S_HELLO
        jne .spr_drop
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_logon_challenge
        jc .spr_deny_account
        mov dword [share_send_type], SH_MSG_CHALLENGE
        mov dword [share_send_key], 0
        mov esi, share_logon_challenge_buf
        mov ecx, 24                     ; salt[8] + nonce[16]
        call share_session_send_raw
        call share_consume
        jmp .spr_done
.spr_deny_account:
        mov al, SH_ERR_NO_ACCOUNT
        jmp .spr_deny

.spr_logonproof:
        mov edi, [share_cur_sess]
        cmp dword [edi + SH_S_STATE], SH_S_HELLO
        jne .spr_drop
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        call share_logon_verify
        jc .spr_deny_proof
        ; OK carries the uid the peer logged on as, and is authenticated with
        ; the verifier: a server that does not know the password cannot forge
        ; it, so the proof is mutual.
        mov [share_reason], al
        mov dword [share_send_type], SH_MSG_OK
        mov edi, [share_cur_sess]
        add edi, SH_S_KEY
        mov [share_send_key], edi
        mov esi, share_reason
        mov ecx, 1
        call share_session_send_raw
        call share_consume
        jmp .spr_done

.spr_deny:
        movzx eax, al
        mov [share_reason], al
        mov edi, 0                      ; DENY is not keyed
        mov dword [share_send_type], SH_MSG_DENY
        call share_send_reason
        mov edi, [share_cur_sess]
        call share_session_free
        jmp .spr_done

.spr_drop:
        mov edi, [share_cur_sess]
        call share_session_free
.spr_done:
        popad
        ret

share_require_ready:
        mov edi, [share_cur_sess]
        cmp dword [edi + SH_S_STATE], SH_S_READY
        jne .srr_no
        clc
        ret
.srr_no:
        stc
        ret

;=======================================================================
; share_consume - remove the message just handled from the session buffer
;=======================================================================
share_consume:
        pushad
        mov edi, [share_cur_sess]
        mov eax, [share_cur_total]
        mov ecx, [edi + SH_S_RXLEN]
        sub ecx, eax
        jz .sc_zero
        lea esi, [edi + SH_S_RX]
        add esi, eax
        lea edi, [edi + SH_S_RX]
        cld
        rep movsb
        mov edi, [share_cur_sess]
        sub [edi + SH_S_RXLEN], eax
        popad
        ret
.sc_zero:
        mov edi, [share_cur_sess]
        mov dword [edi + SH_S_RXLEN], 0
        popad
        ret

;=======================================================================
; share_full_path - the on-disk path a request names
;   In : ESI = subpath inside the share (may be empty), EDI = session record
;   Out: share_work_path
;=======================================================================
share_full_path:
        pushad
        mov [.sfp_saved], esi
        mov eax, [edi + SH_S_SHARE]
        call share_slot_ptr
        add edi, SH_E_PATH
        mov esi, edi
        mov edi, share_work_path
        mov ecx, SHARE_PATH_LEN - 1
.sfp_copy:
        lodsb
        test al, al
        jz .sfp_share_done
        stosb
        dec ecx
        jnz .sfp_copy
        xor al, al
.sfp_share_done:
        mov [edi], al
        mov esi, [.sfp_saved]
        cmp byte [esi], 0
        je .sfp_done
        cmp byte [edi - 1], '/'         ; one separator, not two
        je .sfp_append
        mov al, '/'
        stosb
.sfp_append:
        mov ecx, SHARE_PATH_LEN - 1
.sfp_sub_loop:
        lodsb
        test al, al
        jz .sfp_sub_done
        stosb
        dec ecx
        jnz .sfp_sub_loop
        xor al, al
.sfp_sub_done:
        mov [edi], al
.sfp_done:
        popad
        ret
.sfp_saved: dd 0

;=======================================================================
; share_view_build - the share names, as VIEW answers them
;   Out: [share_reply_len], share_reply_buf: count(1) then 16 bytes each
;=======================================================================
share_view_build:
        pushad
        mov edi, share_reply_buf
        xor al, al
        stosb
        mov esi, share_table
        xor ecx, ecx
        xor edx, edx
.svb_loop:
        cmp ecx, SHARE_MAX
        jge .svb_done
        cmp dword [esi + SH_E_USED], 0
        je .svb_next
        push esi
        push ecx
        add esi, SH_E_NAME
        mov ecx, SHARE_NAME_LEN
        cld
        rep movsb
        pop ecx
        pop esi
        inc edx
.svb_next:
        add esi, SH_E_SIZE
        inc ecx
        jmp .svb_loop
.svb_done:
        mov byte [share_reply_buf], dl
        mov eax, edx
        imul eax, SHARE_NAME_LEN
        inc eax
        mov [share_reply_len], eax
        popad
        ret

;=======================================================================
; share_list_build - one directory of a share
;   In : ESI = subpath inside the share
;   Out: [share_reply_len], share_reply_buf: count(dword) then 40-byte entries
;        name[32], type(byte), pad(3), size(dword)
;=======================================================================
share_list_build:
        pushad
        mov edi, [share_cur_sess]
        call share_full_path
        mov esi, share_work_path
        call share_enter
        jc .slb_fail
        call hbfs_load_root_dir

        mov edi, share_reply_buf
        mov dword [edi], 0
        mov dword [share_reply_len], 4
        mov esi, hbfs_dir_buf
        xor ecx, ecx                    ; entry index
        xor ebx, ebx                    ; entries written
.slb_scan:
        cmp ecx, HBFS_MAX_FILES
        jge .slb_done
        cmp byte [esi + HBFS_DE_TYPE], FTYPE_FREE
        je .slb_next
        mov eax, [share_reply_len]
        add eax, 40
        cmp eax, SHARE_BUF
        ja .slb_done
        ; zero a 40-byte entry at the end of the reply
        push esi
        push ecx
        push ebx
        mov edi, share_reply_buf
        add edi, [share_reply_len]
        mov ecx, 10
        xor eax, eax
        cld
        rep stosd
        pop ebx
        pop ecx
        pop esi
        ; name, at most 31 bytes
        push esi
        push ecx
        push ebx
        mov edi, share_reply_buf
        add edi, [share_reply_len]
        mov ecx, 31
.slb_name:
        lodsb
        test al, al
        jz .slb_name_done
        stosb
        dec ecx
        jnz .slb_name
.slb_name_done:
        pop ebx
        pop ecx
        pop esi
        ; type and size, from the directory entry
        mov edi, share_reply_buf
        add edi, [share_reply_len]
        mov al, [esi + HBFS_DE_TYPE]
        mov [edi + 32], al
        mov eax, [esi + HBFS_DE_SIZE]
        mov [edi + 36], eax
        add dword [share_reply_len], 40
        inc ebx
.slb_next:
        add esi, HBFS_DIR_ENTRY_SIZE
        inc ecx
        jmp .slb_scan
.slb_done:
        mov edi, share_reply_buf
        mov [edi], ebx
        call share_leave
        popad
        ret
.slb_fail:
        ; A path that is not a directory answers with no entries: the client
        ; sees an empty folder, which is something it can act on.
        mov edi, share_reply_buf
        mov dword [edi], 0
        mov dword [share_reply_len], 4
        popad
        ret

;=======================================================================
; share_stat_build - size, type and mtime of one name
;   In : ESI = subpath inside the share
;   Out: [share_reply_len] = 9, or 0 when the name is not there
;=======================================================================
share_stat_build:
        pushad
        mov edi, [share_cur_sess]
        call share_full_path
        mov ebx, share_work_path
        call vfs_route
        test eax, eax
        jz .sst_fail
        mov edi, eax
        mov ebx, share_work_path
        mov ecx, share_stat_tmp
        call [edi + VFS_MT_STAT]
        cmp eax, -1
        je .sst_fail
        mov edi, share_reply_buf
        mov eax, [share_stat_tmp + 0]
        mov [edi + 0], eax
        mov eax, [share_stat_tmp + 4]
        mov [edi + 4], al
        mov eax, [share_stat_tmp + 8]
        mov [edi + 5], eax
        mov dword [share_reply_len], 9
        popad
        ret
.sst_fail:
        mov dword [share_reply_len], 0
        popad
        ret

;=======================================================================
; share_read_build - bytes of one file
;   In : ESI = request payload: offset(dword), length(dword), path
;   Out: [share_reply_len], share_reply_buf
;=======================================================================
share_read_build:
        pushad
        mov [.srd_req], esi
        mov eax, [esi + 0]
        mov [.srd_off], eax
        mov eax, [esi + 4]
        cmp eax, SHARE_BUF
        jbe .srd_len_ok
        mov eax, SHARE_BUF
.srd_len_ok:
        mov [.srd_len], eax
        mov esi, [.srd_req]
        add esi, 8
        mov edi, [share_cur_sess]
        call share_full_path

        mov ebx, share_work_path
        mov ecx, FD_FLAG_READ
        call vfs_open
        cmp eax, -1
        je .srd_fail
        mov [.srd_vfd], eax

        mov ebx, eax
        mov ecx, [.srd_off]
        xor edx, edx                    ; SEEK_SET
        call vfs_seek

        mov ebx, [.srd_vfd]
        mov ecx, share_reply_buf
        mov edx, [.srd_len]
        call vfs_read
        cmp eax, -1
        jne .srd_store
        xor eax, eax
.srd_store:
        mov [share_reply_len], eax
        mov ebx, [.srd_vfd]
        call vfs_close
        popad
        ret
.srd_fail:
        mov dword [share_reply_len], 0
        popad
        ret
.srd_req: dd 0
.srd_off: dd 0
.srd_len: dd 0
.srd_vfd: dd 0

;=======================================================================
; CLIENT
;=======================================================================
; share_client_open - connect to a machine's share service and authenticate
;   In : EAX = IP, EBX = share name, EDX = secret (0 = none)
;   Out: EAX = 0, or -1 with share_client_err set
;=======================================================================
; share_client_connect - open a TCP connection to a machine's share port
;   In : EAX = IP
;   Out: EAX = 0, or -1 with the socket already released
share_client_connect:
        pushad
        mov [.scc_ip], eax
        mov dword [share_client_sock], -1
        mov byte [share_client_err], 0
        mov byte [share_cl_authed], 0
        mov dword [share_cl_seq], 0

        mov eax, SOCK_TCP
        call sock_alloc
        cmp eax, -1
        je .scc_fail
        mov [share_client_sock], eax
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        mov eax, [.scc_ip]
        mov [ebp + SOCK_REMOTE_IP], eax
        mov word [ebp + SOCK_REMOTE_PORT], SHARE_PORT
        call net_alloc_port
        mov [ebp + SOCK_LOCAL_PORT], ax
        mov eax, [tick_count]
        imul eax, 1103515245
        add eax, 12345
        mov [ebp + SOCK_SEQ], eax
        mov dword [ebp + SOCK_STATE], TCP_SYN_SENT
        mov al, TCP_SYN
        call tcp_send_flags

        ; Wait for ESTABLISHED. Every pass runs the local server, so a client
        ; and a server on one machine hand each other the CPU here.
        mov ecx, SHARE_CLIENT_TICKS
.sco_conn:
        call tcp_flush_pending
        mov eax, [share_client_sock]
        imul eax, SOCK_SIZE
        mov eax, [net_sock_table + eax + SOCK_STATE]
        cmp eax, TCP_ESTABLISHED
        je .scc_connected
        cmp eax, TCP_CLOSED
        je .scc_fail
        call share_server_poll
        hlt
        dec ecx
        jnz .sco_conn
        jmp .scc_fail

.scc_connected:
        popad
        xor eax, eax
        ret
.scc_fail:
        mov eax, [share_client_sock]
        call share_close_socket
        mov dword [share_client_sock], -1
        popad
        mov eax, -1
        ret
.scc_ip: dd 0

; share_client_open - connect and authenticate
;   In : EAX = IP, EBX = share name, EDX = secret (0 = none)
;   Out: EAX = 0, or -1 with share_client_err set
share_client_open:
        pushad
        mov [.sco_share], ebx
        mov [.sco_secret], edx
        call share_client_connect
        cmp eax, -1
        je .sco_no_socket

        ; HELLO with the share name, NUL-padded to a fixed 16 bytes so the
        ; server can copy it straight out of the frame.
        mov edi, share_cl_payload
        mov ecx, SHARE_NAME_LEN
        xor eax, eax
        cld
        rep stosb
        mov esi, [.sco_share]
        mov edi, share_cl_payload
        xor ecx, ecx
.sco_name:
        mov al, [esi + ecx]
        test al, al
        jz .sco_name_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, SHARE_NAME_LEN - 1
        jb .sco_name
.sco_name_done:
        xor ebx, ebx
        mov esi, share_cl_payload
        mov ecx, SHARE_NAME_LEN
        xor edi, edi
        call share_cl_sock
        mov al, SH_MSG_HELLO
        call share_send_frame

        ; CHALLENGE
        call share_client_exchange
        jnc .sco_got_challenge
        mov byte [share_client_err], 0x10
        jmp .sco_fail
.sco_got_challenge:
        cmp byte [share_cl_type], SH_MSG_CHALLENGE
        je .sco_have_nonce
        mov byte [share_client_err], 0x11
        cmp byte [share_cl_type], SH_MSG_DENY
        jne .sco_fail
        mov al, [share_cl_reply]
        mov [share_client_err], al
        jmp .sco_fail
.sco_have_nonce:
        cmp dword [share_cl_len], 16
        jae .sco_nonce_ok
        mov byte [share_client_err], 0x12
        jmp .sco_fail
.sco_nonce_ok:
        mov esi, share_cl_reply
        mov edi, share_cl_nonce
        mov ecx, 16
        cld
        rep movsb

        ; H = BLAKE2b(secret); proof = BLAKE2b(H || nonce)
        mov esi, [.sco_secret]
        call share_hash_secret
        mov esi, share_secret_tmp
        mov edi, share_cl_key
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov esi, share_cl_key
        mov edi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov esi, share_cl_nonce
        mov edi, share_auth_tmp + SHARE_KEY_LEN
        mov ecx, 16
        cld
        rep movsb
        mov esi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN + 16
        mov edi, share_auth_out
        call share_hash

        mov esi, share_auth_out
        mov ecx, SHARE_MAC_LEN
        mov edi, share_cl_key            ; AUTH is the last message keyed with H
        xor ebx, ebx
        call share_cl_sock
        mov al, SH_MSG_AUTH
        call share_send_frame

        ; OK or DENY, both verified against H
        call share_client_exchange
        jnc .sco_got_ok
        mov byte [share_client_err], 0x13
        jmp .sco_fail
.sco_got_ok:
        cmp byte [share_cl_type], SH_MSG_OK
        jne .sco_denied

        ; Session key = BLAKE2b(H || nonce || tag)
        mov esi, share_cl_key
        mov edi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov esi, share_cl_nonce
        mov edi, share_auth_tmp + SHARE_KEY_LEN
        mov ecx, 16
        cld
        rep movsb
        mov esi, share_key_tag
        mov edi, share_auth_tmp + SHARE_KEY_LEN + 16
        mov ecx, 7
        cld
        rep movsb
        mov esi, share_auth_tmp
        mov ecx, SHARE_KEY_LEN + 16 + 7
        mov edi, share_key_new
        call share_hash
        mov esi, share_key_new
        mov edi, share_cl_sesskey
        mov ecx, SHARE_KEY_LEN
        cld
        rep movsb
        mov byte [share_cl_authed], 1
        popad
        xor eax, eax
        ret

.sco_denied:
        cmp byte [share_cl_type], SH_MSG_DENY
        jne .sco_fail
        mov al, [share_cl_reply]
        mov [share_client_err], al
        jmp .sco_fail
.sco_no_socket:
        popad
        mov eax, -1
        ret
.sco_fail:
        mov eax, [share_client_sock]
        call share_close_socket
        mov dword [share_client_sock], -1
        popad
        mov eax, -1
        ret
.sco_share:  dd 0
.sco_secret: dd 0

; EBP = the client's socket record
share_cl_sock:
        mov eax, [share_client_sock]
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        ret

;=======================================================================
; share_client_exchange - wait for one message from the server
;   Out: CF clear, share_cl_type/len/reply filled; CF set on failure
;=======================================================================
share_client_exchange:
        pushad
        mov dword [share_cl_rxlen], 0
        mov ecx, SHARE_CLIENT_TICKS
.sce_wait:
        mov eax, [share_client_sock]
        cmp eax, -1
        je .sce_fail
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        mov edi, share_cl_rx
        add edi, [share_cl_rxlen]
        mov edx, SHARE_RX
        sub edx, [share_cl_rxlen]
        test edx, edx
        jz .sce_parse
        call share_drain
        add [share_cl_rxlen], eax
.sce_parse:
        call share_client_parse
        jnc .sce_ok

        ; A connection that is gone is a failure, not a slow peer.
        mov eax, [share_client_sock]
        imul eax, SOCK_SIZE
        mov eax, [net_sock_table + eax + SOCK_STATE]
        cmp eax, TCP_ESTABLISHED
        jne .sce_fail
        call share_server_poll
        hlt
        dec ecx
        jnz .sce_wait
.sce_fail:
        popad
        stc
        ret
.sce_ok:
        popad
        clc
        ret

;=======================================================================
; share_client_parse - take one message out of share_cl_rx
;   Out: CF clear when a whole message was parsed and removed
;=======================================================================
share_client_parse:
        pushad
        cmp dword [share_cl_rxlen], SHARE_HDR_LEN
        jb .scp_more
        mov esi, share_cl_rx
        cmp dword [esi + SH_H_MAGIC], SH_MAGIC_VALUE
        jne .scp_more
        cmp byte [esi + SH_H_VERSION], 1
        jne .scp_more
        mov eax, [esi + SH_H_LEN]
        cmp eax, SHARE_BUF
        ja .scp_more
        movzx ecx, byte [esi + SH_H_TYPE]
        mov [share_cl_type], cl
        mov eax, [esi + SH_H_SEQ]
        mov [share_cl_seq_reply], eax

        mov eax, [esi + SH_H_LEN]
        add eax, SHARE_HDR_LEN
        mov [.scp_body], eax
        cmp ecx, SH_MSG_CHALLENGE
        je .scp_len_ok
        cmp ecx, SH_MSG_DENY
        je .scp_len_ok
        cmp ecx, SH_MSG_VIEW
        je .scp_len_ok
        cmp ecx, SH_MSG_LOGON
        je .scp_len_ok
        cmp ecx, SH_MSG_LOGONPROOF
        je .scp_len_ok
        add eax, SHARE_MAC_LEN
.scp_len_ok:
        mov [.scp_total], eax
        cmp [share_cl_rxlen], eax
        jb .scp_more

        ; Verify the MAC where the protocol carries one.
        cmp ecx, SH_MSG_CHALLENGE
        je .scp_copy
        cmp ecx, SH_MSG_DENY
        je .scp_copy
        cmp ecx, SH_MSG_VIEW
        je .scp_copy
        cmp ecx, SH_MSG_LOGON
        je .scp_copy
        cmp ecx, SH_MSG_LOGONPROOF
        je .scp_copy
        mov edi, share_cl_key            ; H until the session exists
        cmp byte [share_cl_authed], 0
        je .scp_have_key
        mov edi, share_cl_sesskey
.scp_have_key:
        mov ecx, [.scp_body]
        mov esi, share_cl_rx
        call share_mac
        mov esi, eax
        mov edi, share_cl_rx
        add edi, [.scp_body]
        mov ecx, SHARE_MAC_LEN
.scp_mac_cmp:
        lodsb
        cmp al, [edi]
        jne .scp_more
        inc edi
        dec ecx
        jnz .scp_mac_cmp

.scp_copy:
        mov ecx, [share_cl_rx + SH_H_LEN]
        mov [share_cl_len], ecx
        mov esi, share_cl_rx + SHARE_HDR_LEN
        mov edi, share_cl_reply
        cld
        rep movsb
        ; drop the message from the buffer
        mov eax, [.scp_total]
        mov ecx, [share_cl_rxlen]
        sub ecx, eax
        jz .scp_empty
        mov esi, share_cl_rx
        add esi, eax
        mov edi, share_cl_rx
        cld
        rep movsb
        sub [share_cl_rxlen], eax
        jmp .scp_ok
.scp_empty:
        mov dword [share_cl_rxlen], 0
.scp_ok:
        popad
        clc
        ret
.scp_more:
        popad
        stc
        ret
.scp_body:  dd 0
.scp_total: dd 0

;=======================================================================
; share_client_call - one request, one reply
;   In : AL = type, ESI = payload, ECX = length
;   Out: EAX = reply payload length, or -1
;=======================================================================
share_client_call:
        pushad
        mov [.scc_pay], esi
        mov [.scc_len], ecx
        mov [.scc_type], al
        inc dword [share_cl_seq]
        mov ebx, [share_cl_seq]
        mov esi, [.scc_pay]
        mov ecx, [.scc_len]
        mov edi, share_cl_sesskey
        call share_cl_sock
        mov al, [.scc_type]
        call share_send_frame
        call share_client_exchange
        jc .scc_fail
        mov al, [share_cl_type]
        cmp al, [.scc_type]
        jne .scc_mismatch
        mov eax, [share_cl_len]
        mov [esp + 28], eax
        popad
        ret
.scc_mismatch:
.scc_fail:
        popad
        mov eax, -1
        ret
.scc_pay:  dd 0
.scc_len:  dd 0
.scc_type: dd 0

share_client_close:
        pushad
        mov eax, [share_client_sock]
        call share_close_socket
        mov dword [share_client_sock], -1
        popad
        ret

;=======================================================================
; SHELL COMMANDS
;=======================================================================
; cmd_net_sess - dump the server session and socket tables
;
; Both ends of the share protocol run in this kernel, so a field report of
; "the server refused the session" is answered with the tables themselves
; rather than a guess.
cmd_net_sess:
        pushad
        mov esi, msg_ns_hdr
        call vga_print
        mov ebx, share_session_table
        xor ecx, ecx
.nss_loop:
        cmp ecx, SHARE_MAX_SESS
        jge .nss_sock
        mov esi, msg_ns_sess
        call vga_print
        mov eax, ecx
        call share_dec
        mov esi, msg_ns_st
        call vga_print
        mov eax, [ebx + SH_S_STATE]
        call share_dec
        mov esi, msg_ns_sk
        call vga_print
        mov eax, [ebx + SH_S_SOCK]
        cmp eax, 0
        jge .nss_pos
        mov al, '-'
        call vga_putchar
        mov al, '1'
        call vga_putchar
        jmp .nss_nl
.nss_pos:
        call share_dec
.nss_nl:
        mov al, 0x0A
        call vga_putchar
        add ebx, SH_S_SIZE
        inc ecx
        jmp .nss_loop
.nss_sock:
        mov esi, msg_ns_hsock
        call vga_print
        mov ebx, net_sock_table
        xor ecx, ecx
.nss_sloop:
        cmp ecx, MAX_SOCKETS
        jge .nss_done
        cmp dword [ebx + SOCK_TYPE], 0
        je .nss_snext
        mov esi, msg_ns_sess
        call vga_print
        mov eax, ecx
        call share_dec
        mov esi, msg_ns_st
        call vga_print
        mov eax, [ebx + SOCK_STATE]
        call share_dec
        mov esi, msg_ns_sk
        call vga_print
        mov eax, [ebx + SOCK_PARENT]
        call share_dec
        mov esi, msg_ns_port
        call vga_print
        movzx eax, word [ebx + SOCK_LOCAL_PORT]
        call share_dec
        mov al, 0x0A
        call vga_putchar
.nss_snext:
        add ebx, SOCK_SIZE
        inc ecx
        jmp .nss_sloop
.nss_done:
        popad
        ret

; Print EAX in decimal.
share_dec:
        pushad
        mov ebx, 10
        xor ecx, ecx
.sd_loop:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .sd_loop
.sd_out:
        pop eax
        add al, '0'
        call vga_putchar
        dec ecx
        jnz .sd_out
        popad
        ret

; cmd_net_share - "net share" [NAME PATH [SECRET] | -d NAME]
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_share:
        pushad
        mov dword [.ns_mode], SH_MODE_RO
        call skip_spaces
        cmp byte [esi], 0
        je .ns_list
        cmp byte [esi], '/'
        jne .ns_option_done
        ; /rw makes the export writable; the default is read-only, because a
        ; share that grants writes by accident is the failure that matters.
        inc esi
        mov al, [esi]
        or al, 0x20
        cmp al, 'r'
        jne .ns_option_bad
        inc esi
        mov al, [esi]
        or al, 0x20
        cmp al, 'w'
        jne .ns_option_bad
        inc esi
        mov dword [.ns_mode], SH_MODE_RW
        call skip_spaces
.ns_option_done:
        cmp byte [esi], 0
        je .ns_list
        cmp byte [esi], '-'
        jne .ns_add
        jmp .ns_dash
.ns_option_bad:
        jmp .ns_usage

.ns_dash:
        inc esi
        mov al, [esi]
        or al, 0x20
        cmp al, 'd'
        jne .ns_usage
        inc esi
        call skip_spaces
        call share_del
        cmp eax, -1
        je .ns_no_such
        mov esi, msg_share_deleted
        call vga_print
        popad
        ret

.ns_add:
        mov edi, share_arg_name
        mov ecx, SHARE_NAME_LEN - 1
        call copy_word_n
        cmp byte [share_arg_name], 0
        je .ns_usage
        call skip_spaces
        mov edi, share_arg_path
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n
        cmp byte [share_arg_path], 0
        je .ns_usage
        call skip_spaces
        mov edx, 0
        cmp byte [esi], 0
        je .ns_add_go
        mov edi, share_arg_secret
        mov ecx, 63
        call copy_word_n
        mov edx, share_arg_secret
.ns_add_go:
        mov esi, share_arg_name
        mov edi, share_arg_path
        mov ebx, [.ns_mode]
        call share_add
        cmp eax, -1
        je .ns_full
        mov esi, msg_share_added
        call vga_print
        mov esi, share_arg_name
        call vga_print
        mov esi, msg_share_at
        call vga_print
        mov esi, share_arg_path
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.ns_list:
        mov edi, share_table
        xor ecx, ecx
        xor ebx, ebx
.ns_list_loop:
        cmp ecx, SHARE_MAX
        jge .ns_list_done
        cmp dword [edi + SH_E_USED], 0
        je .ns_list_next
        inc ebx
        push ecx
        push edi
        mov esi, edi
        add esi, SH_E_NAME
        call vga_print
        mov esi, msg_share_at
        call vga_print
        pop edi
        push edi
        mov esi, edi
        add esi, SH_E_PATH
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop edi
        pop ecx
.ns_list_next:
        add edi, SH_E_SIZE
        inc ecx
        jmp .ns_list_loop
.ns_list_done:
        test ebx, ebx
        jnz .ns_list_out
        mov esi, msg_share_none
        call vga_print
.ns_list_out:
        mov esi, msg_share_port
        call vga_print
        mov eax, SHARE_PORT
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.ns_no_such:
        mov esi, msg_share_no_such
        call vga_print
        popad
        ret
.ns_full:
        mov esi, msg_share_full
        call vga_print
        popad
        ret
.ns_usage:
        mov esi, msg_share_usage
        call vga_print
        popad
        ret
.ns_mode: dd 0

;=======================================================================
; cmd_net_view - "net view HOST"
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_view:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .nv_usage
        mov edi, share_arg_host
        mov ecx, 63
        call copy_word_n
        mov esi, share_arg_host
        call parse_ip
        test eax, eax
        jnz .nv_have_ip
        mov esi, share_arg_host
        call dns_resolve
        test eax, eax
        jz .nv_dns_fail
.nv_have_ip:
        mov [share_arg_ip], eax
        mov esi, msg_share_view
        call vga_print
        mov eax, [share_arg_ip]
        call vga_print_ip
        mov esi, msg_share_port_short
        call vga_print
        mov eax, SHARE_PORT
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        ; VIEW is the one request that needs no session, so the client asks
        ; for it on a bare connection: no HELLO, no key.
        mov eax, [share_arg_ip]
        call share_client_connect
        cmp eax, -1
        je .nv_unreachable

        mov al, SH_MSG_VIEW
        xor esi, esi
        xor ecx, ecx
        xor ebx, ebx
        xor edi, edi                    ; no MAC
        call share_cl_sock
        mov al, SH_MSG_VIEW
        call share_send_frame
        call share_client_exchange
        jc .nv_failed
        call share_client_close

        mov esi, share_cl_reply
        movzx ecx, byte [esi]
        inc esi
        test ecx, ecx
        jnz .nv_each
        mov esi, msg_share_view_empty
        call vga_print
        popad
        ret
.nv_each:
        push ecx
        push esi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
        add esi, SHARE_NAME_LEN
        pop ecx
        dec ecx
        jnz .nv_each
        popad
        ret

.nv_unreachable:
        mov esi, msg_share_unreachable
        call vga_print
        popad
        ret
.nv_failed:
        call share_client_close
        mov esi, msg_share_refused
        call vga_print
        popad
        ret
.nv_dns_fail:
        mov esi, msg_share_no_host
        call vga_print
        popad
        ret
.nv_usage:
        mov esi, msg_share_view_usage
        call vga_print
        popad
        ret

;=======================================================================
; cmd_net_use - "net use HOST SHARE [SECRET]": open a session and list it
;   In : ESI = rest of the command line
;
; This is the share as a client sees it: an authenticated session, then a
; LIST of the exported folder. A mount point comes next; the session and the
; protocol are what this proves.
;=======================================================================
cmd_net_use:
        pushad
        mov ebx, 1
        call share_parse_target
        jc .nu_usage
        ; A third word that starts with '/' is where the share is mounted;
        ; without one, `net use` proves the session and lists the folder, and
        ; that third word is the secret - which is what it meant before
        ; mounting existed, and what an existing command line still says.
        cmp byte [share_arg_file], '/'
        je .nu_mount
        cmp byte [share_arg_file], 0
        je .nu_list
        cmp byte [share_arg_secret], 0
        jne .nu_list
        mov esi, share_arg_file
        mov edi, share_arg_secret
        mov ecx, 63
        call copy_word_n
.nu_list:
        mov eax, [share_arg_ip]
        mov ebx, share_arg_share
        mov edx, share_arg_secret
        cmp byte [share_arg_secret], 0
        jne .nu_have_secret
        xor edx, edx
.nu_have_secret:
        call share_client_open
        cmp eax, -1
        je .nu_refused

        mov al, SH_MSG_LIST
        mov esi, share_req_path
        mov ecx, 1                      ; the share root: an empty path
        call share_client_call
        cmp eax, -1
        je .nu_failed
        mov [.nu_len], eax
        call share_client_close

        mov esi, msg_share_used
        call vga_print
        mov esi, share_arg_share
        call vga_print
        mov esi, msg_share_on
        call vga_print
        mov esi, share_arg_host
        call vga_print
        mov al, 0x0A
        call vga_putchar

        mov esi, share_cl_reply
        mov eax, [esi]
        add esi, 4
        mov [.nu_count], eax
        test eax, eax
        jz .nu_empty
.nu_each:
        push esi
        call vga_print
        pop esi
        ; size and, for a directory, a trailing slash
        mov eax, [esi + 36]
        push esi
        push eax
        mov esi, msg_share_space
        call vga_print
        pop eax
        call vga_print_dec
        pop esi
        cmp byte [esi + 32], FTYPE_DIR
        jne .nu_line_end
        mov al, '/'
        call vga_putchar
.nu_line_end:
        mov al, 0x0A
        call vga_putchar
        add esi, 40
        dec dword [.nu_count]
        jnz .nu_each
        popad
        ret
.nu_empty:
        mov esi, msg_share_empty_dir
        call vga_print
        popad
        ret

; --- mount the share as a directory -------------------------------------
.nu_mount:
        mov eax, [share_arg_ip]
        mov ebx, share_arg_share
        mov edi, share_arg_secret
        cmp byte [share_arg_secret], 0
        jne .nu_mount_secret
        xor edi, edi
.nu_mount_secret:
        mov esi, share_arg_file
        call net_mount_add
        cmp eax, -1
        je .nu_mount_failed
        ; Prove the mount now rather than at the first `dir`: a wrong secret
        ; should fail where it was typed. The VFS convention is EBX = path,
        ; ECX = the stat block.
        mov ebx, share_arg_file
        mov ecx, share_net_stat_buf
        call vfs_net_stat
        cmp eax, -1
        je .nu_mount_refused
        mov esi, msg_share_mounted
        call vga_print
        mov esi, share_arg_file
        call vga_print
        mov esi, msg_share_mounted_at
        call vga_print
        mov esi, share_arg_share
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.nu_mount_refused:
        mov esi, msg_share_mount_refused
        call vga_print
        popad
        ret
.nu_mount_failed:
        mov esi, msg_share_mount_failed
        call vga_print
        popad
        ret
.nu_refused:
        cmp byte [share_client_err], 0x13
        jne .nu_refused_plain
        ; The server dropped the connection during the handshake, which is how
        ; a wrong secret looks from the client side.
        mov esi, msg_share_dropped
        call vga_print
        popad
        ret
.nu_refused_plain:
        mov esi, msg_share_refused
        call vga_print
        popad
        ret
.nu_failed:
        call share_client_close
        mov esi, msg_share_failed
        call vga_print
        popad
        ret
.nu_usage:
        mov esi, msg_share_use_usage
        call vga_print
        popad
        ret
.nu_len:   dd 0
.nu_count: dd 0

;=======================================================================
; cmd_net_get - "net get HOST SHARE FILE [SECRET]": read a file and print it
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_get:
        pushad
        mov ebx, 1
        call share_parse_target
        jc .ng_usage
        mov eax, [share_arg_ip]
        mov ebx, share_arg_share
        mov edx, share_arg_secret
        cmp byte [share_arg_secret], 0
        jne .ng_have_secret
        xor edx, edx
.ng_have_secret:
        call share_client_open
        cmp eax, -1
        je .ng_refused

        ; request: offset, length, path
        mov esi, share_req_path
        mov dword [esi], 0
        mov dword [esi + 4], SHARE_BUF
        mov edi, esi
        add edi, 8
        mov esi, share_arg_file
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n

        mov al, SH_MSG_READ
        mov esi, share_req_path
        mov ecx, 8
        add ecx, SHARE_PATH_LEN
        call share_client_call
        cmp eax, -1
        je .ng_failed
        mov [.ng_len], eax
        call share_client_close
        mov ecx, [.ng_len]
        test ecx, ecx
        jz .ng_empty
        ; print the bytes, bounded and NUL-terminated
        mov edi, share_cl_reply
        add edi, ecx
        mov byte [edi], 0
        mov esi, share_cl_reply
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.ng_empty:
        mov esi, msg_share_no_file
        call vga_print
        popad
        ret
.ng_refused:
        cmp byte [share_client_err], 0x13
        jne .ng_refused_plain
        mov esi, msg_share_dropped
        call vga_print
        popad
        ret
.ng_refused_plain:
        mov esi, msg_share_refused
        call vga_print
        popad
        ret
.ng_failed:
        call share_client_close
        mov esi, msg_share_failed
        call vga_print
        popad
        ret
.ng_usage:
        mov esi, msg_share_get_usage
        call vga_print
        popad
        ret
.ng_len: dd 0

;=======================================================================
; share_parse_target - HOST SHARE [FILE] [SECRET] from the command line
;   In : ESI = rest of the command line, BL = 1 when a FILE word is expected
;   Out: share_arg_ip, share_arg_host, share_arg_share, share_arg_file,
;        share_arg_secret; CF set on a parse failure
;=======================================================================
share_parse_target:
        pushad
        call skip_spaces
        mov edi, share_arg_host
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_host], 0
        je .spt_bad
        call skip_spaces
        mov edi, share_arg_share
        mov ecx, SHARE_NAME_LEN - 1
        call copy_word_n
        cmp byte [share_arg_share], 0
        je .spt_bad
        call skip_spaces
        mov byte [share_arg_file], 0
        mov byte [share_arg_secret], 0
        cmp byte [esi], 0
        je .spt_resolve
        cmp bl, 1
        jne .spt_secret
        mov edi, share_arg_file
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n
        call skip_spaces
        cmp byte [esi], 0
        je .spt_resolve
.spt_secret:
        mov edi, share_arg_secret
        mov ecx, 63
        call copy_word_n
.spt_resolve:
        mov esi, share_arg_host
        call parse_ip
        test eax, eax
        jnz .spt_have_ip
        mov esi, share_arg_host
        call dns_resolve
        test eax, eax
        jz .spt_bad
.spt_have_ip:
        mov [share_arg_ip], eax
        popad
        clc
        ret
.spt_bad:
        popad
        stc
        ret

;=======================================================================
; A SHARE AS A VOLUME
;
; NETWORK.md section 4: "the redirector seam in the VFS is what makes a share
; look like a volume". The mount table gets an entry whose file operations are
; protocol requests, so `dir /mnt` and `cat /mnt/file` do not know the bytes
; come from another machine.
;
; One client session is kept for the mount being used: the protocol is one
; connection per session, and a directory listing or a read is cheap enough
; that reopening for each call would only add handshakes.
;=======================================================================
NETM_MAX          equ 4
NETM_PATH         equ 0            ; 32 bytes: the mount point
NETM_IP           equ 32
NETM_SHARE        equ 36           ; SHARE_NAME_LEN
NETM_SECRET       equ 52           ; SHARE_KEY_LEN
NETM_LPATH        equ 84           ; 96 bytes: the directory the cache holds
NETM_LCOUNT       equ 180          ; dword
NETM_LVALID       equ 184          ; dword: 1 when the cache describes LPATH
NETM_SIZE         equ 188

NETO_MAX          equ 8
NETO_PATH         equ 0            ; 128 bytes
NETO_SLOT         equ 128          ; dword: the mount it belongs to
NETO_SIZE         equ 132

;=======================================================================
; net_mount_find - the mount a path belongs to
;   In : ESI = path
;   Out: EAX = mount slot or -1
;=======================================================================
net_mount_find:
        pushad
        mov edi, net_mounts
        xor ecx, ecx
.nmf_loop:
        cmp ecx, NETM_MAX
        jge .nmf_none
        cmp byte [edi + NETM_PATH], 0
        je .nmf_next
        mov ebx, esi
        mov edx, 0
.nmf_cmp:
        mov al, [edi + NETM_PATH + edx]
        test al, al
        jz .nmf_end            ; the mount point is a prefix
        cmp al, [ebx + edx]
        jne .nmf_next
        inc edx
        jmp .nmf_cmp
.nmf_end:
        ; it must be the whole path or a directory below it
        mov al, [ebx + edx]
        cmp al, 0
        je .nmf_found
        cmp al, '/'
        je .nmf_found
.nmf_next:
        add edi, NETM_SIZE
        inc ecx
        jmp .nmf_loop
.nmf_found:
        mov [esp + 28], ecx
        popad
        ret
.nmf_none:
        popad
        mov eax, -1
        ret

;=======================================================================
; net_rel_of - the part of a path below its mount point
;   In : ESI = path, EAX = mount slot
;   Out: net_rel_path
;=======================================================================
net_rel_of:
        pushad
        mov edi, eax
        mov ecx, eax
        imul ecx, NETM_SIZE
        lea edx, [net_mounts + ecx + NETM_PATH]
        xor ecx, ecx
.nro_skip:
        cmp byte [edx + ecx], 0
        je .nro_at_end
        inc ecx
        jmp .nro_skip
.nro_at_end:
        add esi, ecx
        cmp byte [esi], '/'
        jne .nro_copy
        inc esi
.nro_copy:
        mov edi, net_rel_path
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n
        popad
        ret

;=======================================================================
; net_ensure_session - a live, authenticated session for a mount
;   In : EAX = mount slot
;   Out: CF set when the machine cannot be reached or refuses the secret
;=======================================================================
net_ensure_session:
        pushad
        mov [.nes_slot], eax
        cmp dword [share_client_sock], -1
        je .nes_open
        mov ecx, eax
        imul ecx, NETM_SIZE
        mov edx, [net_mounts + ecx + NETM_IP]
        cmp edx, [share_sess_ip]
        jne .nes_open
        lea esi, [net_mounts + ecx + NETM_SHARE]
        mov edi, share_sess_name
        call str_compare
        test eax, eax
        jz .nes_ok
.nes_open:
        call share_client_close
        mov eax, [.nes_slot]
        mov ecx, eax
        imul ecx, NETM_SIZE
        lea ebx, [net_mounts + ecx + NETM_SHARE]
        lea edx, [net_mounts + ecx + NETM_SECRET]
        cmp byte [edx], 0
        jne .nes_have_secret
        xor edx, edx
.nes_have_secret:
        mov eax, [net_mounts + ecx + NETM_IP]
        mov [.nes_ip], eax
        call share_client_open
        cmp eax, -1
        je .nes_fail
        mov eax, [.nes_ip]
        mov [share_sess_ip], eax
        mov eax, [.nes_slot]
        imul eax, NETM_SIZE
        lea esi, [net_mounts + eax + NETM_SHARE]
        mov edi, share_sess_name
        mov ecx, SHARE_NAME_LEN
        cld
        rep movsb
.nes_ok:
        popad
        clc
        ret
.nes_fail:
        popad
        stc
        ret
.nes_slot: dd 0
.nes_ip:   dd 0

;=======================================================================
; vfs_net_stat - EBX = path, ECX = stat buffer -> EAX = 0 / -1
;=======================================================================
vfs_net_stat:
        pushad
        mov [.vns_path], ebx
        mov [.vns_buf], ecx
        mov esi, ebx
        call net_mount_find
        cmp eax, -1
        je .vns_fail
        mov [.vns_slot], eax
        mov esi, [.vns_path]
        call net_rel_of
        call net_ensure_session
        jc .vns_fail
        mov al, SH_MSG_STAT
        mov esi, net_rel_path
        mov ecx, SHARE_PATH_LEN
        call share_client_call
        cmp eax, 9
        jne .vns_fail
        mov edi, [.vns_buf]
        mov eax, [share_cl_reply + 0]
        mov [edi + 0], eax              ; size
        movzx eax, byte [share_cl_reply + 4]
        mov [edi + 4], eax              ; type (a dword in the stat block)
        mov eax, [share_cl_reply + 5]
        mov [edi + 8], eax              ; mtime
        popad
        xor eax, eax
        ret
.vns_fail:
        popad
        mov eax, -1
        ret
.vns_path: dd 0
.vns_buf:  dd 0
.vns_slot: dd 0

;=======================================================================
; vfs_net_readdir - EBX = path, ECX = name buffer, EDX = index
;   Out: EAX = entry type, or -1 at the end
;=======================================================================
vfs_net_readdir:
        pushad
        mov [.vnr_path], ebx
        mov [.vnr_buf], ecx
        mov [.vnr_index], edx
        mov esi, ebx
        call net_mount_find
        cmp eax, -1
        je .vnr_fail
        mov [.vnr_slot], eax
        mov esi, [.vnr_path]
        call net_rel_of

        ; Re-list only when the cache describes a different directory.
        mov eax, [.vnr_slot]
        imul eax, NETM_SIZE
        lea edi, [net_mounts + eax + NETM_LPATH]
        cmp dword [net_mounts + eax + NETM_LVALID], 0
        je .vnr_list
        mov esi, net_rel_path
        call str_compare
        test eax, eax
        jnz .vnr_list
        jmp .vnr_have

.vnr_list:
        mov eax, [.vnr_slot]
        call net_ensure_session
        jc .vnr_fail
        call .vnr_ask
        jnc .vnr_listed
        ; A cached session can be out of step with the server: a reply left
        ; over from an earlier request is read as this one's answer. That is
        ; how a 9-byte STAT reply became an empty folder - its first word is a
        ; file size, which for a directory is 0. Reopen once and ask again,
        ; because "cannot list" must not be reported as "nothing here".
        call share_client_close
        mov eax, [.vnr_slot]
        call net_ensure_session
        jc .vnr_fail
        call .vnr_ask
        jc .vnr_fail
.vnr_listed:

        ; keep the listing for the calls that follow
        mov eax, [.vnr_slot]
        imul eax, NETM_SIZE
        lea edi, [net_list_bufs]
        mov ecx, [.vnr_slot]
        imul ecx, NET_LIST_SIZE
        add edi, ecx
        mov esi, share_cl_reply
        mov ecx, [share_cl_len]
        cmp ecx, NET_LIST_SIZE
        jbe .vnr_copy
        mov ecx, NET_LIST_SIZE
.vnr_copy:
        cld
        rep movsb
        mov eax, [.vnr_slot]
        imul eax, NETM_SIZE
        mov edi, [share_cl_reply]
        mov eax, [.vnr_slot]
        imul eax, NETM_SIZE
        mov dword [net_mounts + eax + NETM_LVALID], 1
        mov edi, net_mounts
        add edi, eax
        add edi, NETM_LPATH
        mov esi, net_rel_path
        mov ecx, SHARE_PATH_LEN
        cld
        rep movsb
        ; remember how many entries the cache holds
        mov eax, [.vnr_slot]
        imul eax, NETM_SIZE
        mov ecx, [.vnr_slot]
        imul ecx, NET_LIST_SIZE
        mov edi, net_list_bufs
        add edi, ecx
        mov ecx, [edi]
        mov [net_mounts + eax + NETM_LCOUNT], ecx

.vnr_have:
        mov eax, [.vnr_slot]
        imul eax, NETM_SIZE
        mov ecx, [.vnr_index]
        cmp ecx, [net_mounts + eax + NETM_LCOUNT]
        jae .vnr_fail
        ; entry = cache + 4 + index * 40
        imul ecx, 40
        add ecx, 4
        mov edi, [.vnr_slot]
        imul edi, NET_LIST_SIZE
        add edi, net_list_bufs
        add edi, ecx
        cmp byte [edi], 0
        je .vnr_fail                    ; an empty name is the end of the listing
        ; name to the caller, NUL-terminated
        push edi
        mov esi, edi
        mov edi, [.vnr_buf]
        mov ecx, 31
.vnr_name:
        lodsb
        stosb
        test al, al
        jz .vnr_named
        dec ecx
        jnz .vnr_name
        xor al, al
        stosb
.vnr_named:
        pop edi
        movzx eax, byte [edi + 32]
        mov [esp + 28], eax
        popad
        ret
.vnr_fail:
        popad
        mov eax, -1
        ret

; Ask for the listing once. CF set when the reply is not a listing: a LIST
; reply is a count followed by 40 bytes per entry, so anything else is a
; leftover from another request, not an empty folder.
.vnr_ask:
        mov al, SH_MSG_LIST
        mov esi, net_rel_path
        mov ecx, SHARE_PATH_LEN
        call share_client_call
        cmp eax, 4
        jb .vnr_ask_bad
        mov edx, [share_cl_reply]
        imul edx, 40
        add edx, 4
        cmp edx, [share_cl_len]
        jne .vnr_ask_bad
        clc
        ret
.vnr_ask_bad:
        stc
        ret

.vnr_path:  dd 0
.vnr_buf:   dd 0
.vnr_index: dd 0
.vnr_slot:  dd 0

;=======================================================================
; vfs_net_open - EBX = path, ECX = flags -> EAX = vfd / -1
;=======================================================================
vfs_net_open:
        pushad
        mov [.vno_path], ebx
        mov [.vno_flags], ecx
        mov esi, ebx
        call net_mount_find
        cmp eax, -1
        je .vno_fail
        mov [.vno_slot], eax
        ; a free open slot
        mov edi, net_opens
        xor ecx, ecx
.vno_scan:
        cmp ecx, NETO_MAX
        jge .vno_fail
        cmp byte [edi + NETO_PATH], 0
        je .vno_free
        add edi, NETO_SIZE
        inc ecx
        jmp .vno_scan
.vno_free:
        mov [.vno_open], ecx
        ; the path, share-relative
        mov esi, [.vno_path]
        mov eax, [.vno_slot]
        call net_rel_of
        mov edi, net_opens
        mov eax, [.vno_open]
        imul eax, NETO_SIZE
        add edi, eax
        mov esi, net_rel_path
        mov ecx, 127
        call copy_word_n
        mov edi, net_opens
        mov eax, [.vno_open]
        imul eax, NETO_SIZE
        add edi, eax
        mov eax, [.vno_slot]
        mov [edi + NETO_SLOT], eax
        ; allocate the VFD
        call vfs_alloc_fd
        cmp eax, -1
        je .vno_fail
        mov [.vno_vfd], eax
        mov ebx, eax
        call vfs_fd_ptr
        mov dword [edi + VFD_STATE], VFD_OPEN
        mov dword [edi + VFD_FS_TYPE], VFS_FS_NET
        mov dword [edi + VFD_POS], 0
        mov eax, [.vno_open]
        mov [edi + VFD_REAL_FD], eax
        mov eax, [.vno_vfd]
        mov [esp + 28], eax
        popad
        ret
.vno_fail:
        popad
        mov eax, -1
        ret
.vno_path:  dd 0
.vno_flags: dd 0
.vno_slot:  dd 0
.vno_open:  dd 0
.vno_vfd:   dd 0

;=======================================================================
; vfs_net_read - EBX = vfd, ECX = buffer, EDX = length -> EAX = bytes
;=======================================================================
vfs_net_read:
        pushad
        mov [.vnrd_buf], ecx
        mov [.vnrd_len], edx
        mov [.vnrd_vfd], ebx
        call vfs_fd_ptr
        mov eax, [edi + VFD_REAL_FD]
        mov [.vnrd_open], eax
        mov eax, [edi + VFD_POS]
        mov [.vnrd_off], eax
        mov eax, [.vnrd_open]
        imul eax, NETO_SIZE
        lea esi, [net_opens + eax]
        cmp byte [esi + NETO_PATH], 0
        je .vnrd_fail
        mov eax, [esi + NETO_SLOT]
        call net_ensure_session
        jc .vnrd_fail
        ; request: offset, length, path
        mov esi, share_req_path
        mov eax, [.vnrd_off]
        mov [esi], eax
        mov eax, [.vnrd_len]
        cmp eax, SHARE_BUF - SHARE_PATH_LEN - 8
        jbe .vnrd_len_ok
        mov eax, SHARE_BUF - SHARE_PATH_LEN - 8
.vnrd_len_ok:
        mov [.vnrd_ask], eax
        mov [esi + 4], eax
        mov eax, [.vnrd_open]
        imul eax, NETO_SIZE
        lea esi, [net_opens + eax + NETO_PATH]
        lea edi, [share_req_path + 8]
        mov ecx, SHARE_PATH_LEN
        cld
        rep movsb
        mov al, SH_MSG_READ
        mov esi, share_req_path
        mov ecx, 8 + SHARE_PATH_LEN
        call share_client_call
        cmp eax, -1
        je .vnrd_fail
        mov [.vnrd_got], eax
        test eax, eax
        jz .vnrd_zero
        mov ecx, eax
        mov esi, share_cl_reply
        mov edi, [.vnrd_buf]
        cld
        rep movsb
        ; advance the handle's position
        mov ebx, [.vnrd_vfd]
        call vfs_fd_ptr
        mov eax, [.vnrd_got]
        add [edi + VFD_POS], eax
        mov [esp + 28], eax
        popad
        ret
.vnrd_zero:
        popad
        xor eax, eax
        ret
.vnrd_fail:
        popad
        mov eax, -1
        ret
.vnrd_buf:  dd 0
.vnrd_len:  dd 0
.vnrd_vfd:  dd 0
.vnrd_open: dd 0
.vnrd_off:  dd 0
.vnrd_ask:  dd 0
.vnrd_got:  dd 0

;=======================================================================
; vfs_net_write - EBX = vfd, ECX = buffer, EDX = length -> EAX = bytes
;=======================================================================
vfs_net_write:
        pushad
        mov [.vnw_buf], ecx
        mov [.vnw_len], edx
        mov [.vnw_vfd], ebx
        call vfs_fd_ptr
        mov eax, [edi + VFD_REAL_FD]
        mov [.vnw_open], eax
        mov eax, [edi + VFD_POS]
        mov [.vnw_off], eax
        mov eax, [.vnw_open]
        imul eax, NETO_SIZE
        lea esi, [net_opens + eax]
        cmp byte [esi + NETO_PATH], 0
        je .vnw_fail
        mov eax, [esi + NETO_SLOT]
        call net_ensure_session
        jc .vnw_fail
        mov esi, share_req_path
        mov eax, [.vnw_off]
        mov [esi], eax
        mov eax, [.vnw_len]
        cmp eax, 1200
        jbe .vnw_len_ok
        mov eax, 1200
.vnw_len_ok:
        mov [.vnw_ask], eax
        mov [esi + 4], eax
        mov eax, [.vnw_open]
        imul eax, NETO_SIZE
        lea esi, [net_opens + eax + NETO_PATH]
        lea edi, [share_req_path + 8]
        mov ecx, SHARE_PATH_LEN
        cld
        rep movsb
        lea edi, [share_req_path + 8 + SHARE_PATH_LEN]
        mov esi, [.vnw_buf]
        mov ecx, [.vnw_ask]
        cld
        rep movsb
        mov al, SH_MSG_WRITE
        mov esi, share_req_path
        mov ecx, 8 + SHARE_PATH_LEN
        add ecx, [.vnw_ask]
        call share_client_call
        cmp eax, 4
        jne .vnw_fail
        mov eax, [share_cl_reply]
        test eax, eax
        jz .vnw_fail
        mov [.vnw_got], eax
        mov ebx, [.vnw_vfd]
        call vfs_fd_ptr
        mov eax, [.vnw_got]
        add [edi + VFD_POS], eax
        mov [esp + 28], eax
        popad
        ret
.vnw_fail:
        popad
        mov eax, -1
        ret
.vnw_buf:  dd 0
.vnw_len:  dd 0
.vnw_vfd:  dd 0
.vnw_open: dd 0
.vnw_off:  dd 0
.vnw_ask:  dd 0
.vnw_got:  dd 0

;=======================================================================
; vfs_net_close - EBX = vfd
;=======================================================================
vfs_net_close:
        pushad
        mov [.vnc_vfd], ebx
        call vfs_fd_ptr
        mov eax, [edi + VFD_REAL_FD]
        cmp eax, NETO_MAX
        jae .vnc_done
        imul eax, NETO_SIZE
        lea edi, [net_opens + eax]
        mov byte [edi + NETO_PATH], 0
.vnc_done:
        popad
        ret
.vnc_vfd: dd 0

;=======================================================================
; net_mount_add - register a share as a directory
;   In : ESI = mount point, EAX = IP, EBX = share name, EDI = secret
;   Out: EAX = 0, -1 when there is no slot or the name is unusable
;=======================================================================
net_mount_add:
        pushad
        mov [.nma_path], esi
        mov [.nma_ip], eax
        mov [.nma_share], ebx
        mov [.nma_secret], edi

        ; An existing mount point is replaced, so `net use` twice is a
        ; refresh. A mount point owned by another file system is refused.
        cmp byte [esi], '/'
        jne .nma_fail
        cmp byte [esi + 1], 0
        je .nma_fail                    ; "/" belongs to the volume itself
        mov ebx, esi
        call vfs_route
        test eax, eax
        jz .nma_free_slot
        cmp byte [eax + VFS_MT_TYPE], VFS_FS_NET
        je .nma_free_slot               ; a NET mount at this path: reuse it
        ; Anything else that matches this exact path is a collision.
        push eax
        lea esi, [eax + VFS_MT_PATH]
        mov edi, [.nma_path]
        call str_compare
        pop eax
        test eax, eax
        jz .nma_fail

.nma_free_slot:
        mov edi, net_mounts
        xor ecx, ecx
.nma_scan:
        cmp ecx, NETM_MAX
        jge .nma_fail
        cmp byte [edi + NETM_PATH], 0
        je .nma_found_slot
        add edi, NETM_SIZE
        inc ecx
        jmp .nma_scan
.nma_found_slot:
        mov eax, ecx
.nma_have:
        mov [.nma_slot], eax
        ; the mount table entry
        mov ecx, eax
        imul ecx, NETM_SIZE
        lea edi, [net_mounts + ecx]
        mov [.nma_rec], edi
        ; path
        mov esi, [.nma_path]
        mov ecx, 31
        call copy_word_n
        mov edi, [.nma_rec]
        mov eax, [.nma_ip]
        mov [edi + NETM_IP], eax
        mov esi, [.nma_share]
        lea edi, [edi + NETM_SHARE]
        mov ecx, SHARE_NAME_LEN
        cld
        rep movsb
        mov edi, [.nma_rec]
        add edi, NETM_SECRET
        mov esi, [.nma_secret]
        test esi, esi
        jz .nma_no_secret
        cmp byte [esi], 0
        jne .nma_copy_secret
.nma_no_secret:
        mov byte [edi], 0
        jmp .nma_finish
.nma_copy_secret:
        mov ecx, SHARE_KEY_LEN - 1
        call copy_word_n
.nma_finish:
        mov edi, [.nma_rec]
        mov dword [edi + NETM_LVALID], 0
        mov dword [edi + NETM_LCOUNT], 0
        ; register the VFS mount
        mov esi, [.nma_path]
        mov ebx, esi
        call vfs_mount_net
        popad
        xor eax, eax
        ret
.nma_fail:
        popad
        mov eax, -1
        ret
.nma_path:   dd 0
.nma_ip:     dd 0
.nma_share:  dd 0
.nma_secret: dd 0
.nma_slot:   dd 0
.nma_rec:    dd 0

;=======================================================================
; vfs_mount_net - put a NET entry in the VFS mount table
;   In : EBX = mount point (its first 16 bytes are the prefix)
;=======================================================================
vfs_mount_net:
        pushad
        mov [.vmn_path], ebx
        ; Reuse the entry when this path is already a NET mount, otherwise
        ; take the first free slot. Checking first keeps `net use` idempotent.
        mov edi, vfs_mount_table + VFS_MT_SIZE
        mov ecx, 1
.vmn_scan:
        cmp ecx, VFS_MAX_MOUNTS
        jge .vmn_fail
        cmp byte [edi + VFS_MT_TYPE], VFS_FS_NONE
        je .vmn_free
        cmp byte [edi + VFS_MT_TYPE], VFS_FS_NET
        jne .vmn_next
        push edi
        push ecx
        lea esi, [edi + VFS_MT_PATH]
        mov edi, [.vmn_path]
        call str_compare
        pop ecx
        pop edi
        test eax, eax
        jz .vmn_free                    ; same path: refresh the entry
.vmn_next:
        add edi, VFS_MT_SIZE
        inc ecx
        jmp .vmn_scan
.vmn_free:
        push edi
        mov esi, [.vmn_path]
        mov ecx, 15
        call copy_word_n
        pop edi
        mov byte [edi + VFS_MT_TYPE], VFS_FS_NET
        mov dword [edi + VFS_MT_OPEN],    vfs_net_open
        mov dword [edi + VFS_MT_READ],    vfs_net_read
        mov dword [edi + VFS_MT_WRITE],   vfs_net_write
        mov dword [edi + VFS_MT_READDIR], vfs_net_readdir
        mov dword [edi + VFS_MT_STAT],    vfs_net_stat
        mov dword [edi + VFS_MT_MKDIR],   vfs_stub_ro
        mov dword [edi + VFS_MT_UNLINK],  vfs_stub_ro
        popad
        xor eax, eax
        ret
.vmn_fail:
        popad
        mov eax, -1
        ret
.vmn_path: dd 0
.vmn_slot: dd 0

;=======================================================================
; CLIENT COMMANDS beyond listing and reading
;=======================================================================
; cmd_net_put - "net put HOST SHARE LOCALFILE [SECRET]"
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_put:
        pushad
        mov ebx, 1
        call share_parse_target        ; HOST SHARE LOCALFILE [SECRET]
        jc .np_usage
        ; Read the local file first: a missing source is not a network error.
        ; The legacy fd layer is used because it resolves a name against the
        ; shell's directory, which is what a bare `net put ... file.txt` means.
        mov esi, share_arg_file
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .np_nofile
        mov [.np_vfd], eax
        mov ebx, eax
        mov ecx, share_wbuf
        mov edx, SHARE_WBUF
        call fd_read
        mov [.np_size], eax
        mov ebx, [.np_vfd]
        call fd_close
        cmp dword [.np_size], 0
        jg .np_send
        mov esi, msg_share_put_empty
        call vga_print
        popad
        ret
.np_send:
        mov eax, [share_arg_ip]
        mov ebx, share_arg_share
        mov edx, share_arg_secret
        cmp byte [share_arg_secret], 0
        jne .np_secret
        xor edx, edx
.np_secret:
        call share_client_open
        cmp eax, -1
        je .np_refused

        ; request = offset, length, remote name[128], bytes
        mov dword [.np_off], 0
.np_chunk:
        mov esi, share_req_path
        mov eax, [.np_off]
        mov [esi], eax
        mov eax, [.np_size]
        sub eax, [.np_off]
        cmp eax, 1200                   ; one segment per request
        jbe .np_chunk_ok
        mov eax, 1200
.np_chunk_ok:
        mov [.np_len], eax
        mov [esi + 4], eax
        ; remote name = the file's base name
        lea edi, [esi + 8]
        mov esi, share_arg_file
        mov ecx, SHARE_PATH_LEN - 1
        call share_base_of
        ; bytes after the 128-byte name field
        lea edi, [share_req_path + 8 + SHARE_PATH_LEN]
        mov esi, share_wbuf
        add esi, [.np_off]
        mov ecx, [.np_len]
        cld
        rep movsb
        mov al, SH_MSG_WRITE
        mov esi, share_req_path
        mov ecx, 8 + SHARE_PATH_LEN
        add ecx, [.np_len]
        call share_client_call
        cmp eax, -1
        je .np_failed
        mov ecx, [share_cl_len]
        cmp ecx, 4                      ; a dword of bytes written
        jne .np_denied
        mov eax, [share_cl_reply]
        test eax, eax
        jz .np_denied
        add [.np_off], eax
        mov eax, [.np_off]
        cmp eax, [.np_size]
        jb .np_chunk
        call share_client_close
        mov esi, msg_share_put_ok
        call vga_print
        mov eax, [.np_off]
        call vga_print_dec
        mov esi, msg_share_put_tail
        call vga_print
        popad
        ret
.np_denied:
        call share_client_close
        mov esi, msg_share_put_denied
        call vga_print
        popad
        ret
.np_nofile:
        mov esi, msg_share_put_nofile
        call vga_print
        mov esi, share_arg_file
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.np_refused:
        mov esi, msg_share_refused
        call vga_print
        popad
        ret
.np_failed:
        call share_client_close
        mov esi, msg_share_failed
        call vga_print
        popad
        ret
.np_usage:
        mov esi, msg_share_put_usage
        call vga_print
        popad
        ret
.np_vfd:  dd 0
.np_size: dd 0
.np_off:  dd 0
.np_len:  dd 0

; Copy the base name of ESI into EDI (bounded).
; In : ESI = path, EDI = destination, ECX = bytes available
share_base_of:
        pushad
        mov [.sbo_dst], edi
        mov [.sbo_max], ecx
        mov ebx, esi
        mov edx, esi
.sbo_scan:
        mov al, [ebx]
        test al, al
        jz .sbo_copy
        cmp al, '/'
        jne .sbo_next
        lea edx, [ebx + 1]
.sbo_next:
        inc ebx
        jmp .sbo_scan
.sbo_copy:
        mov esi, edx
        mov edi, [.sbo_dst]
        mov ecx, [.sbo_max]
        call copy_word_n
        popad
        ret
.sbo_dst: dd 0
.sbo_max: dd 0

;=======================================================================
; cmd_net_svc - "net svc HOST SHARE SERVICE start|stop|restart [SECRET]"
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_svc:
        pushad
        ; HOST SHARE SERVICE OP [SECRET] - five words, so the target parser
        ; (which stops at four) cannot be used here.
        call skip_spaces
        mov edi, share_arg_host
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_host], 0
        je .nsv_usage
        call skip_spaces
        mov edi, share_arg_share
        mov ecx, SHARE_NAME_LEN - 1
        call copy_word_n
        cmp byte [share_arg_share], 0
        je .nsv_usage
        call skip_spaces
        mov edi, share_arg_file
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_file], 0
        je .nsv_usage
        call skip_spaces
        mov edi, share_arg_op
        mov ecx, 15
        call copy_word_n
        ; The line pointer has to survive the verb lookup: share_svc_op_of
        ; takes ESI as its input and leaves it there, so reading the secret
        ; through ESI afterwards would read the verb back off share_arg_op.
        mov [.nsv_line], esi
        mov esi, share_arg_op
        call share_svc_op_of
        cmp eax, -1
        je .nsv_usage
        mov [.nsv_op], eax
        mov esi, [.nsv_line]
        call skip_spaces
        mov byte [share_arg_secret], 0
        cmp byte [esi], 0
        je .nsv_have_secret
        mov edi, share_arg_secret
        mov ecx, 63
        call copy_word_n
.nsv_have_secret:
        ; the host may be a name
        mov esi, share_arg_host
        call parse_ip
        test eax, eax
        jnz .nsv_resolved
        mov esi, share_arg_host
        call dns_resolve
        test eax, eax
        jz .nsv_nohost
.nsv_resolved:
        mov [share_arg_ip], eax
        mov eax, [share_arg_ip]
        mov ebx, share_arg_share
        mov edx, share_arg_secret
        cmp byte [share_arg_secret], 0
        jne .nsv_secret
        xor edx, edx
.nsv_secret:
        call share_client_open
        cmp eax, -1
        je .nsv_refused
        ; request = name[16], op(1)
        mov esi, share_req_path
        mov edi, esi
        mov esi, share_arg_file
        xor eax, eax
        mov ecx, 16
        cld
        rep stosb
        mov esi, share_arg_file
        mov edi, share_req_path
        xor ecx, ecx
.nsv_name:
        mov al, [esi + ecx]
        test al, al
        jz .nsv_name_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, 15
        jb .nsv_name
.nsv_name_done:
        mov eax, [.nsv_op]
        mov [share_req_path + 16], al
        mov esi, share_req_path
        mov al, SH_MSG_SVC
        mov ecx, 17
        call share_client_call
        cmp eax, -1
        je .nsv_failed
        call share_client_close
        cmp eax, 1
        jne .nsv_noservice
        mov esi, msg_share_svc_done
        call vga_print
        mov esi, share_arg_file
        call vga_print
        mov esi, msg_share_svc_state
        call vga_print
        movzx eax, byte [share_cl_reply]
        call share_print_svc_state
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.nsv_noservice:
        mov esi, msg_share_no_service
        call vga_print
        popad
        ret
.nsv_refused:
        mov esi, msg_share_refused
        call vga_print
        popad
        ret
.nsv_failed:
        call share_client_close
        mov esi, msg_share_failed
        call vga_print
        popad
        ret
.nsv_nohost:
        mov esi, msg_share_no_host
        call vga_print
        popad
        ret
.nsv_usage:
        mov esi, msg_share_svc_usage
        call vga_print
        popad
        ret
.nsv_op: dd 0
.nsv_line: dd 0

; share_svc_op_of - ESI = word -> EAX = 0 start, 1 stop, 2 restart, -1 other
share_svc_op_of:
        push esi
        push edi
        push ebx
        mov ebx, esi
        mov edi, share_op_start
        call str_compare
        test eax, eax
        jz .sso_start
        mov esi, ebx
        mov edi, share_op_stop
        call str_compare
        test eax, eax
        jz .sso_stop
        mov esi, ebx
        mov edi, share_op_restart
        call str_compare
        test eax, eax
        jz .sso_restart
        pop ebx
        pop edi
        pop esi
        mov eax, -1
        ret
.sso_start:
        xor eax, eax
        jmp .sso_out
.sso_stop:
        mov eax, 1
        jmp .sso_out
.sso_restart:
        mov eax, 2
.sso_out:
        pop ebx
        pop edi
        pop esi
        ret

; EAX = service state -> name on the console
share_print_svc_state:
        pushad
        mov esi, msg_share_svc_stopped
        cmp eax, SVC_RUNNING
        jne .spss_stopping
        mov esi, msg_share_svc_running
        jmp .spss_out
.spss_stopping:
        cmp eax, SVC_STOPPING
        jne .spss_failed
        mov esi, msg_share_svc_stopping
        jmp .spss_out
.spss_failed:
        cmp eax, SVC_FAILED
        jne .spss_retry
        mov esi, msg_share_svc_failed
        jmp .spss_out
.spss_retry:
        cmp eax, SVC_RETRY
        jne .spss_out
        mov esi, msg_share_svc_retry
.spss_out:
        call vga_print
        popad
        ret

;=======================================================================
; cmd_net_logon - "net logon HOST USER PASSWORD"
;   In : ESI = rest of the command line
;
; The password is used where it is typed and never leaves this machine: the
; server sends a salt and a nonce, the client derives the verifier with the
; same KDF the logon screen uses, and only the proof crosses the wire.
;=======================================================================
cmd_net_logon:
        pushad
        call skip_spaces
        mov edi, share_arg_host
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_host], 0
        je .nlg_usage
        call skip_spaces
        mov edi, share_arg_user
        mov ecx, 31
        call copy_word_n
        cmp byte [share_arg_user], 0
        je .nlg_usage
        call skip_spaces
        mov edi, share_arg_password
        mov ecx, 63
        call copy_word_n
        cmp byte [share_arg_password], 0
        je .nlg_usage

        mov esi, share_arg_host
        call parse_ip
        test eax, eax
        jnz .nlg_have_ip
        mov esi, share_arg_host
        call dns_resolve
        test eax, eax
        jz .nlg_nohost
.nlg_have_ip:
        mov [share_arg_ip], eax
        call share_client_connect
        cmp eax, -1
        je .nlg_unreachable

        ; LOGON{user[16]}
        mov edi, share_cl_payload
        xor eax, eax
        mov ecx, 16
        cld
        rep stosb
        mov esi, share_arg_user
        mov edi, share_cl_payload
        xor ecx, ecx
.nlg_name:
        mov al, [esi + ecx]
        test al, al
        jz .nlg_name_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, 15
        jb .nlg_name
.nlg_name_done:
        xor ebx, ebx
        mov esi, share_cl_payload
        mov ecx, 16
        xor edi, edi
        call share_cl_sock
        mov al, SH_MSG_LOGON
        call share_send_frame

        call share_client_exchange
        jc .nlg_failed
        cmp byte [share_cl_type], SH_MSG_CHALLENGE
        je .nlg_have_salt
        cmp byte [share_cl_type], SH_MSG_DENY
        jne .nlg_failed
        mov esi, msg_share_no_account
        call vga_print
        call share_client_close
        popad
        ret
.nlg_have_salt:
        cmp dword [share_cl_len], 24
        jb .nlg_failed
        ; verifier = KDF(password, salt), the same one the logon screen uses
        mov esi, share_arg_password
        mov edi, share_cl_reply
        mov ebx, share_cl_key
        call sess_hash_password
        jc .nlg_failed
        ; proof = BLAKE2b(verifier || nonce)
        mov esi, share_cl_key
        mov edi, share_auth_tmp
        mov ecx, 32
        cld
        rep movsb
        mov esi, share_cl_reply
        add esi, 8
        mov edi, share_auth_tmp + 32
        mov ecx, 16
        cld
        rep movsb
        mov esi, share_auth_tmp
        mov ecx, 48
        mov edi, share_auth_out
        call share_hash
        mov esi, share_auth_out
        mov ecx, SHARE_MAC_LEN
        xor ebx, ebx
        xor edi, edi
        call share_cl_sock
        mov al, SH_MSG_LOGONPROOF
        call share_send_frame

        call share_client_exchange
        jc .nlg_failed
        call share_client_close
        cmp byte [share_cl_type], SH_MSG_OK
        jne .nlg_refused
        mov esi, msg_share_logged_on
        call vga_print
        mov esi, share_arg_user
        call vga_print
        mov esi, msg_share_logon_at
        call vga_print
        mov esi, share_arg_host
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.nlg_refused:
        mov esi, msg_share_logon_refused
        call vga_print
        popad
        ret
.nlg_unreachable:
        mov esi, msg_share_unreachable
        call vga_print
        popad
        ret
.nlg_failed:
        call share_client_close
        mov esi, msg_share_failed
        call vga_print
        popad
        ret
.nlg_nohost:
        mov esi, msg_share_no_host
        call vga_print
        popad
        ret
.nlg_usage:
        mov esi, msg_share_logon_usage
        call vga_print
        popad
        ret

;=======================================================================
; cmd_net_print - "net print HOST SHARE LOCALFILE [SECRET]"
;   In : ESI = rest of the command line
;=======================================================================
cmd_net_print:
        pushad
        mov ebx, 1
        call share_parse_target        ; HOST SHARE LOCALFILE [SECRET]
        jc .npr_usage
        mov esi, share_arg_file
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .npr_nofile
        mov [.npr_vfd], eax
        mov ebx, eax
        mov ecx, share_wbuf
        mov edx, SHARE_WBUF
        call fd_read
        mov [.npr_size], eax
        mov ebx, [.npr_vfd]
        call fd_close
        cmp dword [.npr_size], 0
        jg .npr_send
        mov esi, msg_share_put_empty
        call vga_print
        popad
        ret
.npr_send:
        mov eax, [share_arg_ip]
        mov ebx, share_arg_share
        mov edx, share_arg_secret
        cmp byte [share_arg_secret], 0
        jne .npr_secret
        xor edx, edx
.npr_secret:
        call share_client_open
        cmp eax, -1
        je .npr_refused
        ; One job, several messages: a message carries at most SHARE_BUF
        ; bytes, and a document is usually larger.
        mov eax, [.npr_size]
        cmp eax, SHARE_WBUF
        jbe .npr_total_ok
        mov eax, SHARE_WBUF             ; the spool buffer is the limit
.npr_total_ok:
        mov [.npr_total], eax
        mov dword [.npr_off], 0
.npr_chunk:
        ; request = offset, total, name[24], then this chunk's bytes
        mov edi, share_req_path
        xor eax, eax
        mov ecx, 32
        cld
        rep stosb
        mov eax, [.npr_off]
        mov [share_req_path], eax
        mov eax, [.npr_total]
        mov [share_req_path + 4], eax
        lea edi, [share_req_path + 8]
        mov esi, share_arg_file
        mov ecx, 23
        call share_base_of
        lea edi, [share_req_path + 32]
        mov esi, share_wbuf
        add esi, [.npr_off]
        mov ecx, [.npr_total]
        sub ecx, [.npr_off]
        cmp ecx, 1200
        jbe .npr_chunk_ok
        mov ecx, 1200
.npr_chunk_ok:
        mov [.npr_len], ecx
        cld
        rep movsb
        mov al, SH_MSG_PRINT
        mov esi, share_req_path
        mov ecx, 32
        add ecx, [.npr_len]
        call share_client_call
        cmp eax, -1
        je .npr_failed
        cmp eax, 4
        je .npr_queued
        cmp eax, 1
        jne .npr_failed
        cmp byte [share_cl_reply], 0
        jne .npr_reason
        mov eax, [.npr_len]
        add [.npr_off], eax
        mov eax, [.npr_off]
        cmp eax, [.npr_total]
        jb .npr_chunk
        jmp .npr_failed
.npr_reason:
        ; One byte of reason came back instead: name it.
        movzx eax, byte [share_cl_reply]
        cmp eax, SH_ERR_NO_SPOOL
        je .npr_no_spool
        cmp eax, SH_ERR_WRITE
        je .npr_no_write
        cmp eax, SH_ERR_SPOOL_FULL
        je .npr_spool_full
        cmp eax, SH_ERR_TOO_BIG
        je .npr_too_big
        jmp .npr_failed_plain
.npr_no_spool:
        mov esi, msg_share_no_spool
        jmp .npr_refused_msg
.npr_no_write:
        mov esi, msg_share_no_write
        jmp .npr_refused_msg
.npr_spool_full:
        mov esi, msg_share_spool_full
        jmp .npr_refused_msg
.npr_too_big:
        mov esi, msg_share_too_big
        jmp .npr_refused_msg
.npr_queued:
        call share_client_close
        mov esi, msg_share_printed
        call vga_print
        mov eax, [share_cl_reply]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.npr_nofile:
        mov esi, msg_share_put_nofile
        call vga_print
        mov esi, share_arg_file
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.npr_refused:
        mov esi, msg_share_refused
.npr_refused_msg:
        call vga_print
        popad
        ret
.npr_failed:
        call share_client_close
.npr_failed_plain:
        mov esi, msg_share_failed
        call vga_print
        popad
        ret
.npr_usage:
        mov esi, msg_share_print_usage
        call vga_print
        popad
        ret
.npr_vfd:   dd 0
.npr_size:  dd 0
.npr_total: dd 0
.npr_off:   dd 0
.npr_len:   dd 0

;=======================================================================
; SERVER OPERATIONS the protocol dispatches to
;=======================================================================

;=======================================================================
; share_enter_parent - enter the directory that holds a path
;   In : ESI = path inside the share
;   Out: share_base_name holds the last component, CF set when the
;        directory is not there
;
; Writes inside a share go through hbfs_create_file, which acts on the
; current directory, so the server stands in the directory and names the file.
;=======================================================================
share_enter_parent:
        pushad
        mov [.sep_path], esi
        ; split at the last '/'
        mov ebx, esi
        mov edi, share_base_name
        mov [.sep_last], edi
        xor ecx, ecx
.sep_scan:
        mov al, [ebx]
        test al, al
        jz .sep_split
        inc ebx
        jmp .sep_scan
.sep_split:
        ; EBX = end of the string; walk back to the last '/'
        mov esi, [.sep_path]
        mov edx, esi
.sep_back:
        cmp ebx, esi
        jbe .sep_no_dir
        dec ebx
        cmp byte [ebx], '/'
        je .sep_found
        jmp .sep_back
.sep_found:
        ; name = after the slash, directory = before it
        lea esi, [ebx + 1]
        mov edi, share_base_name
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n
        cmp byte [share_base_name], 0
        je .sep_fail
        ; the directory part: copy [path, slash)
        mov esi, [.sep_path]
        cmp ebx, esi
        je .sep_root
        mov edi, share_dir_path
        mov ecx, ebx
        sub ecx, esi
        cld
        rep movsb
        mov byte [edi], 0
        jmp .sep_enter
.sep_root:
        mov byte [share_dir_path], 0
        jmp .sep_enter
.sep_no_dir:
        ; no slash: the file sits in the share root
        mov esi, [.sep_path]
        mov edi, share_base_name
        mov ecx, SHARE_PATH_LEN - 1
        call copy_word_n
        cmp byte [share_base_name], 0
        je .sep_fail
        mov byte [share_dir_path], 0
.sep_enter:
        ; full directory = share path [+ "/" + dir]
        mov edi, [share_cur_sess]
        mov esi, share_dir_path
        call share_full_path
        mov esi, share_work_path
        call share_enter
        jc .sep_fail
        popad
        clc
        ret
.sep_fail:
        popad
        stc
        ret
.sep_path: dd 0
.sep_last: dd 0

;=======================================================================
; share_write_apply - a WRITE request
;   In : ESI = payload: offset(dword), length(dword), path, then the bytes
;   Out: [share_reply_len] = 4 and the reply holds the bytes written,
;        or 0 when the share refused
;
; The file is rebuilt from what it held plus the new bytes: the tree's file
; primitives replace a whole file (hbfs_create_file), and a share that says
; "4 KB maximum" is honest about it rather than pretending to be a database.
;=======================================================================
share_write_apply:
        pushad
        mov [.swa_req], esi
        mov dword [share_reply_len], 0
        mov edi, [share_cur_sess]
        mov eax, [edi + SH_S_SHARE]
        call share_slot_ptr
        cmp dword [edi + SH_E_MODE], SH_MODE_RW
        je .swa_writable
        ; A read-only share refuses before it looks at the name: the share's
        ; own permission is checked as well as the file's. The refusal is a
        ; normal, keyed reply of one byte, so the client can tell it apart
        ; from a reply that carries a byte count.
        mov edi, share_reply_buf
        mov al, SH_ERR_READ_ONLY
        mov [edi], al
        mov dword [share_reply_len], 1
        popad
        ret
.swa_writable:
        mov esi, [.swa_req]
        mov eax, [esi + 0]
        mov [.swa_off], eax
        mov eax, [esi + 4]
        mov [.swa_len], eax
        ; bytes follow the 8-byte offset/length and a 128-byte path
        lea ecx, [esi + 8 + SHARE_PATH_LEN]
        mov [.swa_data], ecx

        ; read the file as it is, so an offset write keeps the rest
        mov dword [.swa_old], 0
        cmp dword [.swa_off], 0
        jne .swa_read_old
        jmp .swa_new_bytes
.swa_read_old:
        mov esi, [.swa_req]
        add esi, 8
        mov ebx, esi
        mov ecx, FD_FLAG_READ
        call vfs_open
        cmp eax, -1
        je .swa_new_bytes
        mov [.swa_vfd], eax
        mov ebx, eax
        mov ecx, share_wbuf
        mov edx, SHARE_WBUF
        call vfs_read
        cmp eax, -1
        jne .swa_old_read
        xor eax, eax
.swa_old_read:
        mov [.swa_old], eax
        mov ebx, [.swa_vfd]
        call vfs_close
.swa_new_bytes:
        ; splice the new bytes in at the offset
        mov edi, share_wbuf
        mov eax, [.swa_off]
        cmp eax, SHARE_WBUF
        jbe .swa_off_ok
        mov eax, SHARE_WBUF
        mov [.swa_off], eax
.swa_off_ok:
        add edi, eax
        mov esi, [.swa_data]
        mov ecx, [.swa_len]
        ; never write past the buffer
        mov eax, SHARE_WBUF
        sub eax, [.swa_off]
        cmp ecx, eax
        jbe .swa_len_ok
        mov ecx, eax
        mov [.swa_len], eax
.swa_len_ok:
        cld
        rep movsb
        ; new size = max(old, offset+len)
        mov eax, [.swa_off]
        add eax, [.swa_len]
        cmp eax, [.swa_old]
        jae .swa_size_ok
        mov eax, [.swa_old]
.swa_size_ok:
        mov [.swa_size], eax

        ; Create or replace it, in its directory. The path in the request is
        ; share-relative; share_enter_parent resolves it against the export.
        mov esi, [.swa_req]
        add esi, 8
        call share_enter_parent
        jc .swa_fail
        mov esi, share_base_name
        mov edi, share_wbuf
        mov ecx, [.swa_size]
        mov edx, FTYPE_TEXT
        call hbfs_create_file
        pushfd
        call path_leave
        popfd
        jc .swa_fail

        mov edi, share_reply_buf
        mov eax, [.swa_len]
        mov [edi], eax
        mov dword [share_reply_len], 4
        popad
        ret
.swa_fail:
        mov dword [share_reply_len], 0
        popad
        ret
.swa_req:  dd 0
.swa_off:  dd 0
.swa_len:  dd 0
.swa_data: dd 0
.swa_old:  dd 0
.swa_size: dd 0
.swa_vfd:  dd 0

;=======================================================================
; share_svc_apply - service control on this machine
;   In : ESI = payload: name[16], op(1)
;   Out: [share_reply_len] = 1 (state) or 0
;=======================================================================
share_svc_apply:
        pushad
        mov dword [share_reply_len], 0
        mov [.sva_req], esi
        call svc_find
        jc .sva_none
        mov [.sva_slot], eax
        ; (the operation is carried in the last byte of the request)
        mov esi, [.sva_req]
        movzx ecx, byte [esi + 16]
        mov [.sva_op], ecx
        ; stop first for everything: start on a running service is a no-op,
        ; and a restart is a stop and a start.
        cmp ecx, 0                      ; 0 = start
        je .sva_start
        mov eax, [.sva_slot]
        call svc_stop
        cmp dword [.sva_op], 2          ; 2 = restart
        jne .sva_report
.sva_start:
        mov eax, [.sva_slot]
        call svc_start
.sva_report:
        mov eax, [.sva_slot]
        call svc_slot
        mov eax, [edi + SVC_F_STATE]
        mov edi, share_reply_buf
        mov [edi], al
        mov dword [share_reply_len], 1
        popad
        ret
.sva_none:
        mov edi, share_reply_buf
        mov al, SH_ERR_NO_SERVICE
        mov [edi], al
        mov dword [share_reply_len], 1
        mov dword [share_reply_sent], 1
        popad
        ret
.sva_req:  dd 0
.sva_slot: dd 0
.sva_op:   dd 0

;=======================================================================
; share_print_apply - queue a job on this machine's printer
;   In : ESI = payload: name[32], then the bytes
;   Out: [share_reply_len] = 4 (job id), or 0
;=======================================================================
share_print_apply:
        pushad
        mov [.spa_req], esi
        mov dword [share_reply_len], 0

        ; A job does not fit one message, so the client streams it: each
        ; request carries the offset it belongs at, the total size, the name
        ; and this chunk's bytes. Everything lands in one assembly buffer and
        ; the file is written once, when the last chunk arrives.
        mov eax, [esi]
        mov [.spa_off], eax
        mov eax, [esi + 4]
        mov [.spa_total], eax
        cmp eax, SHARE_WBUF
        ja .spa_too_big
        mov ecx, [share_cur_body]
        sub ecx, SHARE_HDR_LEN
        sub ecx, 32
        jbe .spa_nowrite
        mov [.spa_len], ecx

        cmp dword [.spa_off], 0
        jne .spa_have_name
        ; HBFS keeps the string it is given as the name - it does not make a
        ; directory - so a job is stored in the root under a "Spool-" prefix
        ; instead of inside a spool directory that would have to exist first.
        mov esi, [.spa_req]
        add esi, 8
        mov edi, share_print_file + 6
        mov ecx, 24
        cld
        rep movsb
.spa_have_name:
        mov eax, [.spa_off]
        add eax, [.spa_len]
        cmp eax, [.spa_total]
        ja .spa_too_big
        mov edi, share_print_buf
        add edi, [.spa_off]
        mov esi, [.spa_req]
        add esi, 32
        mov ecx, [.spa_len]
        cld
        rep movsb
        mov eax, [.spa_off]
        add eax, [.spa_len]
        cmp eax, [.spa_total]
        jae .spa_complete
        mov edi, share_reply_buf
        mov byte [edi], 0               ; chunk taken, send the next one
        mov dword [share_reply_len], 1
        popad
        ret

.spa_complete:
        mov esi, share_root_dir
        call share_enter
        jc .spa_no_spool
        mov esi, share_print_file
        mov edi, share_print_buf
        mov ecx, [.spa_total]
        mov edx, FTYPE_TEXT
        call hbfs_create_file
        pushfd
        call path_leave
        popfd
        jc .spa_nowrite
        ; queue it in the spooler the shell's print/printq share
        xor ecx, ecx
.spa_slot:
        cmp ecx, SPOOL_MAX_JOBS
        jae .spa_full
        cmp byte [spool_status + ecx], 0
        je .spa_have
        inc ecx
        jmp .spa_slot
.spa_have:
        ; queue the stored file the way the console's print command queues
        ; one: the spooler only needs a name to report.
        mov eax, ecx
        imul eax, SPOOL_NAME_LEN
        lea edi, [spool_names + eax]
        mov esi, share_print_file
        call str_copy
        ; find the slot again for the status byte
        xor ecx, ecx
.spa_status:
        cmp ecx, SPOOL_MAX_JOBS
        jae .spa_full
        mov eax, ecx
        imul eax, SPOOL_NAME_LEN
        lea edi, [spool_names + eax]
        cmp byte [spool_status + ecx], 0
        je .spa_mark
        inc ecx
        jmp .spa_status
.spa_mark:
        mov byte [spool_status + ecx], 1
        mov edi, share_reply_buf
        mov eax, ecx
        inc eax
        mov [edi], eax
        mov dword [share_reply_len], 4
        popad
        ret
.spa_leave_nowrite:
        call path_leave
        jmp .spa_nowrite
.spa_too_big:
        mov al, SH_ERR_TOO_BIG
        jmp .spa_refuse
.spa_no_spool:
        mov al, SH_ERR_NO_SPOOL
        jmp .spa_refuse
.spa_nowrite:
        mov al, SH_ERR_WRITE
        jmp .spa_refuse
.spa_full:
        mov al, SH_ERR_SPOOL_FULL
.spa_refuse:
        ; One byte of reason, as every other refusal does: a job silently
        ; dropped is worse than a job refused with a name for the reason.
        mov edi, share_reply_buf
        mov [edi], al
        mov dword [share_reply_len], 1
        popad
        ret
.spa_req:   dd 0
.spa_off:   dd 0
.spa_total: dd 0
.spa_len:   dd 0

;=======================================================================
; share_logon_challenge - start a network logon
;   In : ESI = payload: user[16]
;   Out: CF set when the account is not there; otherwise
;        share_logon_challenge_buf = salt[8] + nonce[16] and the session
;        remembers the stored verifier to check the proof against
;=======================================================================
share_logon_challenge:
        pushad
        mov edi, [share_cur_sess]
        lea edi, [edi + SH_S_LUSER]
        mov ecx, 16
        cld
        rep movsb
        mov esi, [share_cur_sess]
        add esi, SH_S_LUSER
        call sess_load_account          ; uid/salt/hash for the name
        jc .slc_none
        ; This session is an account logon now, not a share session.
        mov edi, [share_cur_sess]
        mov eax, [sess_uid_buf]
        mov [edi + SH_S_SHARE], eax     ; the uid, for the reply
        ; the verifier goes where the handshake key would live
        lea edi, [edi + SH_S_KEY]
        mov esi, sess_hash_buf
        mov ecx, 32
        cld
        rep movsb
        ; salt then nonce
        mov esi, sess_salt_buf
        mov edi, share_logon_challenge_buf
        mov ecx, 8
        cld
        rep movsb
        mov edi, [share_cur_sess]
        lea edi, [edi + SH_S_NONCE]
        mov ecx, 16
        call share_random
        mov esi, [share_cur_sess]
        lea esi, [esi + SH_S_NONCE]
        mov edi, share_logon_challenge_buf + 8
        mov ecx, 16
        cld
        rep movsb
        popad
        clc
        ret
.slc_none:
        popad
        stc
        ret

;=======================================================================
; share_logon_verify - check a logon proof
;   In : ESI = payload: proof[16]
;   Out: CF set when it does not match; else EAX = uid (low byte in AL)
;=======================================================================
share_logon_verify:
        pushad
        ; expected = BLAKE2b(stored verifier || nonce)
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_KEY]
        mov edi, share_auth_tmp
        mov ecx, 32
        cld
        rep movsb
        mov esi, [share_cur_sess]
        lea esi, [esi + SH_S_NONCE]
        mov edi, share_auth_tmp + 32
        mov ecx, 16
        cld
        rep movsb
        mov esi, share_auth_tmp
        mov ecx, 48
        mov edi, share_auth_out
        call share_hash
        mov edi, [share_cur_sess]
        lea esi, [edi + SH_S_RX + SHARE_HDR_LEN]
        mov edi, share_auth_out
        mov ecx, SHARE_MAC_LEN
.slv_cmp:
        lodsb
        cmp al, [edi]
        jne .slv_bad
        inc edi
        dec ecx
        jnz .slv_cmp
        mov edi, [share_cur_sess]
        mov eax, [edi + SH_S_SHARE]     ; the uid parked by the challenge
        mov [esp + 28], eax
        popad
        clc
        ret
.slv_bad:
        popad
        stc
        ret

;=======================================================================
; share_init - one-time setup
;=======================================================================
share_init:
        pushad
        mov dword [share_listen_idx], -1
        mov dword [share_client_sock], -1
        mov dword [share_count], 0
        mov edi, share_session_table
        mov ecx, (SHARE_MAX_SESS * SH_S_SIZE) / 4
        xor eax, eax
        cld
        rep stosd
        mov edi, share_session_table
        mov ecx, SHARE_MAX_SESS
.si_sess:
        mov dword [edi + SH_S_SOCK], -1
        add edi, SH_S_SIZE
        dec ecx
        jnz .si_sess
        popad
        ret

;=======================================================================
; State and strings
;=======================================================================
share_table:        times SHARE_MAX * SH_E_SIZE db 0
share_session_table: times SHARE_MAX_SESS * SH_S_SIZE db 0
share_count:        dd 0
share_listen_idx:   dd -1
share_client_sock:  dd -1

net_share_word: db "share", 0
net_view_word:  db "view", 0
net_use_word:   db "use", 0
net_get_word:   db "get", 0
net_put_word:   db "put", 0
net_svc_word:   db "svc", 0
net_logon_word: db "logon", 0
net_print_word: db "print", 0
net_sess_word:  db "sess", 0

; Drop the word at ESI and the spaces after it.
skip_one_word:
        cmp byte [esi], 0
        je .sow_done
        cmp byte [esi], ' '
        je .sow_spaces
        inc esi
        jmp skip_one_word
.sow_spaces:
        call skip_spaces
.sow_done:
        ret

share_empty_secret: db 0
share_key_tag:      db "MVSH-K", 0
share_browse_name:  db "*", 0
share_op_start:     db "start", 0
share_op_stop:      db "stop", 0
share_op_restart:   db "restart", 0

share_reason:       db 0
share_send_type:    dd 0
share_send_key:     dd 0
share_cur_type:     dd 0
share_cur_seq:      dd 0
share_cur_body:     dd 0
share_cur_total:    dd 0
share_cur_sess:     dd 0

share_work_path:    times SHARE_PATH_LEN + 96 db 0
share_dir_path:     times SHARE_PATH_LEN + 96 db 0
share_base_name:    times SHARE_PATH_LEN db 0
share_root_dir:     db "/", 0
share_print_file:   db "Spool-", 0
                    times 32 db 0
; Its own assembly buffer, not share_wbuf: a client on this machine is using
; share_wbuf to hold the document it is sending, and both ends run here.
share_print_buf:    times 4096 db 0          ; SHARE_WBUF, declared below
share_logon_challenge_buf: times 24 db 0
share_reply_sent:   dd 0
share_sess_ip:      dd 0
share_sess_name:    times SHARE_NAME_LEN db 0
net_mounts:         times NETM_MAX * NETM_SIZE db 0
net_opens:          times NETO_MAX * NETO_SIZE db 0
net_rel_path:       times SHARE_PATH_LEN db 0
NET_LIST_SIZE      equ 600
net_list_bufs:      times NETM_MAX * NET_LIST_SIZE db 0
share_arg_name:     times SHARE_NAME_LEN db 0
share_arg_path:     times SHARE_PATH_LEN db 0
share_arg_secret:   times 64 db 0
share_arg_host:     times 64 db 0
share_arg_user:     times 32 db 0
share_arg_op:       times 16 db 0
share_arg_password: times 64 db 0
share_arg_share:    times SHARE_NAME_LEN db 0
share_arg_file:     times SHARE_PATH_LEN db 0
; One request buffer for every command: 8 bytes of offset/length, a
; 64-byte name, and room for the bytes a PUT or PRINT carries with it.
share_req_path:     times 8 + SHARE_PATH_LEN + 4200 db 0
share_net_stat_buf: times 12 db 0
share_arg_ip:       dd 0

share_reply_buf:    times SHARE_BUF db 0
share_reply_len:    dd 0
share_tx_buf:       times SHARE_HDR_LEN + SHARE_BUF + SHARE_MAC_LEN db 0
SHARE_WBUF         equ 4096
share_wbuf:         times SHARE_WBUF db 0
share_stat_tmp:     times 12 db 0
share_secret_tmp:   times SHARE_KEY_LEN db 0
share_hash_out:     times SHARE_KEY_LEN db 0
share_auth_tmp:     times SHARE_KEY_LEN + 16 + 8 db 0
share_auth_out:     times SHARE_KEY_LEN db 0
share_key_new:      times SHARE_KEY_LEN db 0

share_mac_inner:    times SHARE_KEY_LEN db 0
share_mac_scratch:  times SHARE_KEY_LEN * 2 db 0
share_mac_out:      times SHARE_MAC_LEN db 0

share_cl_payload:   times SHARE_NAME_LEN db 0
share_cl_nonce:     times 16 db 0
share_cl_key:       times SHARE_KEY_LEN db 0
share_cl_sesskey:   times SHARE_KEY_LEN db 0
share_cl_rx:        times SHARE_RX db 0
share_cl_reply:     times SHARE_BUF db 0
share_cl_authed:    db 0
share_client_err:   db 0
share_cl_type:      db 0
share_cl_len:       dd 0
share_cl_rxlen:     dd 0
share_cl_seq:       dd 0
share_cl_seq_reply: dd 0

; Strings, above bss_start in data.inc: a message placed below it reads as an
; empty string because the boot code zeroes that region.
msg_share_added:       db "Shared: ", 0
msg_share_deleted:     db "Share removed", 0x0A, 0
msg_share_at:          db " = ", 0
msg_share_none:        db "No shares", 0x0A, 0
msg_share_port:        db "Share service port: ", 0
msg_share_port_short:  db ":", 0
msg_share_usage:       db "Usage: net share [/rw] [NAME PATH [SECRET]] | net share -d NAME", 0x0A, 0
msg_share_no_such:     db "net: no such share", 0x0A, 0
msg_share_full:        db "net: share table is full", 0x0A, 0
msg_share_view:        db "Shares on ", 0
msg_share_view_empty:  db "  (none)", 0x0A, 0
msg_share_view_usage:  db "Usage: net view HOST", 0x0A, 0
msg_share_unreachable: db "net: cannot reach the share service", 0x0A, 0
msg_share_refused:     db "net: the server refused the session", 0x0A, 0
msg_share_no_host:     db "net: unknown host", 0x0A, 0
msg_share_put_ok:      db "Wrote ", 0
msg_share_put_tail:    db " bytes to the share", 0x0A, 0
msg_share_put_denied:  db "net: the share refused the write (read-only?)", 0x0A, 0
msg_share_put_nofile:  db "net: no such local file", 0x0A, 0
msg_share_put_empty:   db "net: the file is empty", 0x0A, 0
msg_share_put_usage:   db "Usage: net put HOST SHARE LOCALFILE [SECRET]", 0x0A, 0
msg_share_svc_done:    db "Service ", 0
msg_share_svc_state:   db " is ", 0
msg_share_no_service:  db "net: no such service on the server", 0x0A, 0
msg_share_svc_usage:   db "Usage: net svc HOST SHARE SERVICE start|stop|restart [SECRET]", 0x0A, 0
msg_share_logged_on:   db "Logged on as ", 0
msg_share_logon_at:    db " on ", 0
msg_share_logon_refused: db "net: the server refused the logon", 0x0A, 0
msg_share_no_account:  db "net: no such account on the server", 0x0A, 0
msg_share_logon_usage: db "Usage: net logon HOST USER PASSWORD", 0x0A, 0
msg_share_printed:     db "Queued job ", 0
msg_share_no_spool:    db "net: the server cannot open its print spool", 0x0A, 0
msg_share_no_write:    db "net: the server could not store the job", 0x0A, 0
msg_share_spool_full:  db "net: the server's print spool is full", 0x0A, 0
msg_share_too_big:     db "net: the job is larger than the spool buffer", 0x0A, 0
msg_share_print_usage: db "Usage: net print HOST SHARE LOCALFILE [SECRET]", 0x0A, 0
msg_share_svc_stopped:  db "stopped", 0
msg_share_svc_running:  db "running", 0
msg_share_svc_stopping: db "stopping", 0
msg_share_svc_failed:   db "failed", 0
msg_share_svc_retry:    db "waiting to retry", 0
msg_share_dropped:     db "net: the server dropped the session (wrong secret?)", 0x0A, 0
msg_share_used:        db "Session on ", 0
msg_share_on:          db " at ", 0
msg_share_space:       db "  ", 0
msg_share_empty_dir:   db "  (empty)", 0x0A, 0
msg_share_failed:      db "net: the request failed", 0x0A, 0
msg_share_no_file:     db "net: no such file in the share", 0x0A, 0
msg_share_use_usage:   db "Usage: net use HOST SHARE [/MOUNT] [SECRET]", 0x0A, 0
msg_share_mounted:     db "Mounted ", 0
msg_ns_sess:  db "  s", 0
msg_ns_st:    db " st", 0
msg_ns_sk:    db " sk", 0
msg_ns_port:  db " lp", 0
msg_ns_hdr:   db "Sessions (index, state, socket):", 0x0A, 0
msg_ns_hsock: db "Sockets (index, state, parent, local port):", 0x0A, 0
msg_share_mounted_at:  db " from ", 0
msg_share_mount_refused: db "net: the mount could not open a session (wrong secret?)", 0x0A, 0
msg_share_mount_failed:  db "net: no free mount point (or it is in use)", 0x0A, 0
msg_share_get_usage:   db "Usage: net get HOST SHARE FILE [SECRET]", 0x0A, 0
