;=======================================================================
; firewall.inc - the inbound rule set of NETWORK.md section 7
;
; The contract has six parts and this file is all six:
;
;   Default        inbound deny, outbound allow
;   Match          protocol, local port, remote address range
;   State          a reply to something this machine sent is allowed back
;                  without a rule
;   Loopback       always allowed, and never counted as inbound
;   Enforcement    the TCP listen path, before a socket is spent on the
;                  connection, and the UDP receive path, before any handler
;                  sees the datagram
;   Audit          a block is counted, and a block for a given remote address
;                  is written to the boot log once
;
; A rule is one Sett value under \Registry\Machine\System\Firewall, named
; Rule0, Rule1, ... and holding three words:
;
;   <tcp|udp|any> <port|0> [<remote>[/bits]|any]
;
;   tcp 4450 any            the share server, from anywhere
;   tcp 8000 10.0.0.0/24    a local web server, from one subnet
;   udp 53 any              a name server
;
; `Enabled` holds "0" when the firewall is off; anything else means on. The
; live table is built from those values at boot and rewritten after every
; change `net fw` makes, so the Sett is the single source of truth and the
; table and the store cannot disagree. A rule is stored as normalized text -
; the same text `net fw list` prints - so what reloads is what was accepted.
;
; What this is not, and the spec's "as built" table says so:
;
;   - No application-image match and no per-package port declaration with an
;     approval prompt. A rule names a protocol, a port and an address range,
;     and whoever may write the firewall key may add one.
;   - Outbound is allowed without a rule and is not counted, so a rule that
;     *denies* outbound is not expressible.
;   - A blocked packet is dropped, not answered with a reset: a reset would
;     tell a scanner which ports exist, and the contract asks for a count and
;     an audit record, not a reply.
;=======================================================================

FW_MAX_RULES    equ 16
FW_R_SIZE       equ 16
FW_R_PROTO      equ 0                 ; byte: 0 any, 6 tcp, 17 udp
FW_R_USED       equ 1                 ; byte
FW_R_PORT       equ 2                 ; word, host order, 0 = any
FW_R_IP         equ 4                 ; dword, memory order
FW_R_MASK       equ 8                 ; dword, memory order

; A reply is allowed back when it answers a datagram this machine sent: the
; remote address and port and our own port, kept for FW_FLOW_MAX flows and
; replaced oldest-first. TCP needs no table, because an inbound segment for an
; established connection is matched to its socket before the listen path.
FW_FLOW_MAX     equ 16
FW_F_SIZE       equ 12
FW_F_IP         equ 0
FW_F_RPORT      equ 4                 ; word
FW_F_LPORT      equ 6                 ; word
FW_F_TICK       equ 8                 ; dword

FW_AUDIT_MAX    equ 8

align 4
fw_enabled:     dd 1
fw_rule_count:  dd 0
fw_allowed:     dd 0
fw_blocked:     dd 0
fw_blocked_udp: dd 0
fw_flow_next:   dd 0
fw_audit_n:     dd 0
fw_bad_rules:   dd 0
fw_rules:  times FW_MAX_RULES * FW_R_SIZE db 0
fw_flows:  times FW_FLOW_MAX * FW_F_SIZE db 0
fw_audit:  times FW_AUDIT_MAX * 4 db 0

fw_key_path:      db "\Registry\Machine\System\Firewall", 0
fw_name_enabled:  db "Enabled", 0
fw_prefix_rule:   db "Rule", 0
fw_proto_tcp:     db "tcp", 0
fw_proto_udp:     db "udp", 0
fw_proto_any:     db "any", 0
fw_word_star:     db "*", 0
fw_word_on:       db "on", 0
fw_word_off:      db "off", 0
fw_word_list:     db "list", 0
fw_word_add:      db "add", 0
fw_word_del:      db "del", 0
fw_word_reload:   db "reload", 0
fw_text_on:       db "1", 0
fw_text_off:      db "0", 0

fw_name_buf:      times 16 db 0
fw_word_buf:      times 24 db 0
fw_text_buf:      times 64 db 0       ; one rule as text
fw_audit_buf:     times 64 db 0
fw_parse_entry:   dd 0
fw_text_ptr:      dd 0
fw_loop_i:        dd 0
fw_loop_j:        dd 0
fw_udp_port:      dd 0

;-----------------------------------------------------------------------
; fw_rule_name - "Rule<N>" into a buffer
;   In : EBX = N, EDI = destination
;-----------------------------------------------------------------------
fw_rule_name:
        push eax
        push esi
        push edi
        mov esi, fw_prefix_rule
.rn_copy:
        lodsb
        test al, al
        jz .rn_num
        stosb
        jmp .rn_copy
.rn_num:
        mov eax, ebx
        call itoa_dec_edi
        mov byte [edi], 0
        pop edi
        pop esi
        pop eax
        ret

;=======================================================================
; fw_init - build the table from the Sett, once, after sett_load
;=======================================================================
fw_init:
        pushad
        mov dword [fw_allowed], 0
        mov dword [fw_blocked], 0
        mov dword [fw_blocked_udp], 0
        mov dword [fw_flow_next], 0
        mov dword [fw_audit_n], 0
        call fw_reload
        popad
        ret

;-----------------------------------------------------------------------
; fw_reload - read Enabled and the rules out of the Sett again
;
; The same read runs at boot and from `net fw reload`, which is how a rule
; written with `sett set` is applied without restarting the machine. The
; counters are the caller's: this re-reads the policy, it does not forget
; what has already happened.
;-----------------------------------------------------------------------
fw_reload:
        pushad
        mov dword [fw_enabled], 1
        mov edi, fw_key_path
        mov esi, fw_name_enabled
        call sett_kv_get_str
        jc .frl_rules
        cmp byte [esi], '0'
        jne .frl_rules
        mov dword [fw_enabled], 0
.frl_rules:
        call fw_load_rules
        popad
        ret

;-----------------------------------------------------------------------
; fw_load_rules - rebuild the live table from Rule0..RuleN
;
; The walk stops at the first name that is not there, so a gap ends the list:
; `net fw del` compacts the store, and a hand-written Sett with Rule0 and
; Rule5 serves one rule. A value that does not parse is skipped and the walk
; carries on, because one bad line must not disarm the rest of the set.
;-----------------------------------------------------------------------
fw_load_rules:
        pushad
        mov dword [fw_rule_count], 0
        mov dword [fw_bad_rules], 0
        mov dword [fw_loop_i], 0
.flr_loop:
        mov ebx, [fw_loop_i]
        cmp ebx, FW_MAX_RULES
        jae .flr_done
        mov edi, fw_name_buf
        call fw_rule_name
        mov edi, fw_key_path
        mov esi, fw_name_buf
        call sett_kv_get_str
        jc .flr_done
        mov eax, [fw_rule_count]
        imul eax, FW_R_SIZE
        lea edi, [fw_rules + eax]
        call fw_parse_rule
        jnc .flr_have
        ; Skipped, not fatal - and not silent either: a rule that was typed
        ; wrong must not look like a rule that is being honoured.
        mov edi, fw_audit_buf
        mov esi, msg_fw_bad_rule
        call fw_copy_text
        mov ebx, [fw_loop_i]
        call fw_rule_name
        mov al, LOG_WARN
        mov esi, fw_audit_buf
        call bootlog_write
        inc dword [fw_bad_rules]
        jmp .flr_next
.flr_have:
        inc dword [fw_rule_count]
.flr_next:
        inc dword [fw_loop_i]
        jmp .flr_loop
.flr_done:
        popad
        ret

;=======================================================================
; fw_parse_rule - "<proto> <port> [remote[/bits]|any]" into an entry
;   In : ESI = text, EDI = entry
;   Out: CF clear when it parsed; the entry is filled in and marked used
;=======================================================================
fw_parse_rule:
        pushad
        mov [fw_parse_entry], edi
        mov [fw_text_ptr], esi
        mov byte [edi + FW_R_PROTO], 0
        mov byte [edi + FW_R_USED], 0
        mov word [edi + FW_R_PORT], 0
        mov dword [edi + FW_R_IP], 0
        mov dword [edi + FW_R_MASK], 0

        ; Every stage starts from the saved text, not from whatever ESI has
        ; become: copy_word_n advances it, parse_decimal advances it, and
        ; str_compare restores it to whatever it was given. Reading the line
        ; through a pointer that three helpers each move is how the port
        ; parser ended up looking at the protocol word again.
        mov esi, [fw_text_ptr]
        call skip_spaces
        mov edi, fw_word_buf
        mov ecx, 8
        call copy_word_n
        cmp byte [fw_word_buf], 0
        je .pr_bad

        mov esi, fw_word_buf
        mov edi, fw_proto_tcp
        call str_compare
        jz .pr_tcp
        mov esi, fw_word_buf
        mov edi, fw_proto_udp
        call str_compare
        jz .pr_udp
        mov esi, fw_word_buf
        mov edi, fw_proto_any
        call str_compare
        jz .pr_port
        mov esi, fw_word_buf
        mov edi, fw_word_star
        call str_compare
        jz .pr_port
        jmp .pr_bad
.pr_tcp:
        mov edi, [fw_parse_entry]
        mov byte [edi + FW_R_PROTO], IP_PROTO_TCP
        jmp .pr_port
.pr_udp:
        mov edi, [fw_parse_entry]
        mov byte [edi + FW_R_PROTO], IP_PROTO_UDP

.pr_port:
        mov esi, [fw_text_ptr]
        call skip_one_word             ; past the protocol
        call skip_spaces
        cmp byte [esi], 0
        je .pr_ok                     ; proto alone: any port, any address
        mov edi, fw_word_buf
        mov ecx, 8
        call copy_word_n
        mov esi, fw_word_buf
        cmp byte [esi], 0
        je .pr_ok
        mov edi, fw_proto_any
        call str_compare
        jz .pr_remote
        mov esi, fw_word_buf
        mov edi, fw_word_star
        call str_compare
        jz .pr_remote
        mov esi, fw_word_buf
        call parse_decimal
        cmp eax, 65535
        ja .pr_bad
        mov edi, [fw_parse_entry]
        mov [edi + FW_R_PORT], ax

.pr_remote:
        mov esi, [fw_text_ptr]
        call skip_one_word             ; past the protocol
        call skip_one_word             ; past the port
        call skip_spaces
        cmp byte [esi], 0
        je .pr_ok
        mov edi, fw_word_buf
        mov ecx, 20
        call copy_word_n
        mov esi, fw_word_buf
        mov edi, fw_proto_any
        call str_compare
        jz .pr_ok
        mov esi, fw_word_buf
        mov edi, fw_word_star
        call str_compare
        jz .pr_ok

        mov esi, fw_word_buf
        call parse_ip
        test eax, eax
        jz .pr_bad
        mov edi, [fw_parse_entry]
        mov [edi + FW_R_IP], eax
        mov dword [edi + FW_R_MASK], 0xFFFFFFFF

        ; "/bits". parse_ip leaves ESI just past the fourth octet, which is
        ; where a slash would be.
        cmp byte [esi], '/'
        jne .pr_ok
        inc esi
        call parse_decimal
        cmp eax, 32
        ja .pr_bad
        mov ecx, eax
        xor eax, eax
        test ecx, ecx
        jz .pr_mask_ready
        mov eax, 1
        shl eax, cl
        dec eax                        ; a /24 covers the first three octets,
                                       ; and the first octet is the low byte
                                       ; here, so its mask is 0x00FFFFFF.
.pr_mask_ready:
        mov edi, [fw_parse_entry]
        mov [edi + FW_R_MASK], eax

.pr_ok:
        mov edi, [fw_parse_entry]
        mov byte [edi + FW_R_USED], 1
        popad
        clc
        ret
.pr_bad:
        popad
        stc
        ret

;=======================================================================
; fw_rule_match - does any rule admit this inbound packet?
;   In : EAX = protocol, EBX = local port (host order), EDX = source address
;   Out: ZF set when a rule admits it. Uses EAX/ECX/EDI.
;=======================================================================
fw_rule_match:
        push esi
        push edx
        mov esi, fw_rules
        mov ecx, [fw_rule_count]
.frm_loop:
        test ecx, ecx
        jz .frm_no
        cmp byte [esi + FW_R_USED], 0
        je .frm_next
        movzx edi, byte [esi + FW_R_PROTO]
        test edi, edi
        jz .frm_proto_ok
        cmp edi, eax
        jne .frm_next
.frm_proto_ok:
        movzx edi, word [esi + FW_R_PORT]
        test edi, edi
        jz .frm_port_ok
        cmp edi, ebx
        jne .frm_next
.frm_port_ok:
        mov edi, [esi + FW_R_MASK]
        and edi, edx
        mov eax, [esi + FW_R_IP]
        and eax, [esi + FW_R_MASK]
        cmp eax, edi
        jne .frm_next
        pop edx
        pop esi
        xor eax, eax                   ; ZF=1
        ret
.frm_next:
        add esi, FW_R_SIZE
        dec ecx
        jmp .frm_loop
.frm_no:
        pop edx
        pop esi
        or eax, 1                      ; ZF=0
        ret

;=======================================================================
; fw_inbound_ok - the decision for one inbound packet
;   In : EAX = protocol, EBX = local port, EDX = source address
;   Out: CF clear = admit, CF set = drop
;
; Loopback is admitted before anything is looked at and is not counted: an
; address on this machine is not an inbound connection, which is what the
; contract asks for and what keeps the loopback tests honest.
;=======================================================================
fw_inbound_ok:
        pushad
        cmp dword [fw_enabled], 0
        je .fio_allow

        mov eax, [esp + 20]            ; EDX on entry: the source address
        call net_is_loopback
        jz .fio_allow_local

        mov eax, [esp + 28]            ; EAX on entry: the protocol
        mov ebx, [esp + 16]            ; EBX on entry: the local port
        and ebx, 0xFFFF
        mov edx, [esp + 20]
        call fw_rule_match
        jz .fio_allow_rule

        inc dword [fw_blocked]
        mov eax, [esp + 28]
        cmp eax, IP_PROTO_UDP
        jne .fio_audit
        inc dword [fw_blocked_udp]
.fio_audit:
        mov edx, [esp + 20]
        call fw_audit_block
        popad
        stc
        ret

.fio_allow_rule:
        inc dword [fw_allowed]
.fio_allow:
.fio_allow_local:
        popad
        clc
        ret

;=======================================================================
; fw_udp_gate - the UDP receive-path hook, called with the destination port
;   In : EBX = destination (local) port
;   Out: CF clear = deliver, CF set = drop
;=======================================================================
fw_udp_gate:
        pushad
        mov [fw_udp_port], ebx
        ; State first: a datagram that answers something this machine sent is
        ; not an inbound connection, so DHCP and DNS replies come back without
        ; a rule - and so does any other reply.
        mov eax, [net_recv_src_ip]
        movzx ebx, word [net_udp_src_port]
        mov ecx, [fw_udp_port]
        and ecx, 0xFFFF
        call fw_udp_state
        jz .fug_allow
        mov eax, IP_PROTO_UDP
        mov ebx, [fw_udp_port]
        and ebx, 0xFFFF
        mov edx, [net_recv_src_ip]
        call fw_inbound_ok
        jc .fug_block
.fug_allow:
        popad
        clc
        ret
.fug_block:
        popad
        stc
        ret

;=======================================================================
; fw_udp_state - is this datagram a reply to something we sent?
;   In : EAX = source address, EBX = source port, ECX = local port
;   Out: ZF set when it matches a flow
;=======================================================================
fw_udp_state:
        push esi
        push ecx
        push edx
        mov esi, fw_flows
        mov edx, FW_FLOW_MAX
.fus_loop:
        test edx, edx
        jz .fus_no
        movzx ecx, word [esi + FW_F_LPORT]
        cmp cx, [esp + 4]              ; the local port: [esp] = EDX,
                                       ; [esp+4] = ECX, [esp+8] = ESI
        jne .fus_next
        movzx ecx, word [esi + FW_F_RPORT]
        cmp ecx, ebx
        jne .fus_next
        ; The address has to match too - or the flow was sent to a broadcast,
        ; which is how DHCP asks for an address and is answered by a server
        ; whose address it did not know.
        cmp dword [esi + FW_F_IP], 0xFFFFFFFF
        je .fus_yes
        cmp [esi + FW_F_IP], eax
        jne .fus_next
.fus_yes:
        pop edx
        pop ecx
        pop esi
        xor eax, eax
        ret
.fus_next:
        add esi, FW_F_SIZE
        dec edx
        jmp .fus_loop
.fus_no:
        pop edx
        pop ecx
        pop esi
        or eax, 1
        ret

;=======================================================================
; fw_flow_record - remember an outbound datagram, so its reply is admitted
;   In : EAX = destination address, EBX = destination port, ECX = local port
;=======================================================================
fw_flow_record:
        pushad
        mov edx, [fw_flow_next]
        imul edx, FW_F_SIZE
        lea edi, [fw_flows + edx]
        mov [edi + FW_F_IP], eax
        mov [edi + FW_F_RPORT], bx
        mov [edi + FW_F_LPORT], cx
        mov eax, [tick_count]
        mov [edi + FW_F_TICK], eax
        inc dword [fw_flow_next]
        cmp dword [fw_flow_next], FW_FLOW_MAX
        jb .ffr_done
        mov dword [fw_flow_next], 0
.ffr_done:
        popad
        ret

;=======================================================================
; fw_audit_block - record a block against a remote address, once
;   In : EDX = source address
;=======================================================================
fw_audit_block:
        pushad
        mov [fw_text_ptr], edx
        mov esi, fw_audit
        mov ecx, [fw_audit_n]
.fab_scan:
        test ecx, ecx
        jz .fab_add
        cmp [esi], edx
        je .fab_done                   ; already reported: the count is enough
        add esi, 4
        dec ecx
        jmp .fab_scan
.fab_add:
        mov ecx, [fw_audit_n]
        cmp ecx, FW_AUDIT_MAX
        jae .fab_done                  ; the list is full: keep counting only
        mov esi, fw_audit
        mov eax, ecx
        shl eax, 2
        mov [esi + eax], edx
        inc dword [fw_audit_n]

        mov edi, fw_audit_buf
        mov esi, msg_fw_audit
        call fw_copy_text
        mov eax, [fw_text_ptr]
        call fw_fmt_ip
        mov byte [edi], 0
        mov al, LOG_WARN
        mov esi, fw_audit_buf
        call bootlog_write
.fab_done:
        popad
        ret

;-----------------------------------------------------------------------
; fw_copy_text - copy a NUL-terminated string, EDI advanced past the NUL
;-----------------------------------------------------------------------
fw_copy_text:
        push eax
.fct_loop:
        lodsb
        test al, al
        jz .fct_done
        stosb
        jmp .fct_loop
.fct_done:
        pop eax
        ret

;-----------------------------------------------------------------------
; fw_fmt_ip - write an address as four decimal octets, EDI advanced
;   In : EAX = address in memory order (10.0.0.1 = 0x0100000A)
;-----------------------------------------------------------------------
fw_fmt_ip:
        push eax
        push ebx
        push ecx
        push edx
        mov ebx, eax
        movzx eax, bl
        call itoa_dec_edi
        mov al, '.'
        stosb
        movzx eax, bh
        call itoa_dec_edi
        mov al, '.'
        stosb
        shr ebx, 16
        movzx eax, bl
        call itoa_dec_edi
        mov al, '.'
        stosb
        movzx eax, bh
        call itoa_dec_edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; fw_mask_bits - prefix length from a mask in octet order
;   In : EAX = mask; Out: EAX = 0..32
;-----------------------------------------------------------------------
fw_mask_bits:
        push ebx
        push ecx
        xor ecx, ecx
.fmb_loop:
        cmp ecx, 32
        jae .fmb_done
        mov ebx, 1
        shl ebx, cl
        test eax, ebx
        jz .fmb_done
        inc ecx
        jmp .fmb_loop
.fmb_done:
        mov eax, ecx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; fw_fmt_rule - write one entry as the text the Sett holds, EDI advanced
;   In : ESI = entry, EDI = destination
;-----------------------------------------------------------------------
fw_fmt_rule:
        push eax
        push ebx
        push ecx
        push edx
        mov [fw_text_ptr], esi
        movzx eax, byte [esi + FW_R_PROTO]
        cmp eax, IP_PROTO_UDP
        je .fff_udp
        cmp eax, IP_PROTO_TCP
        je .fff_tcp
        mov esi, fw_proto_any
        jmp .fff_proto
.fff_tcp:
        mov esi, fw_proto_tcp
        jmp .fff_proto
.fff_udp:
        mov esi, fw_proto_udp
.fff_proto:
        call fw_copy_text
        mov al, ' '
        stosb
        mov esi, [fw_text_ptr]
        movzx eax, word [esi + FW_R_PORT]
        call itoa_dec_edi
        mov al, ' '
        stosb
        mov esi, [fw_text_ptr]
        mov eax, [esi + FW_R_MASK]
        test eax, eax
        jnz .fff_addr
        mov esi, fw_proto_any
        call fw_copy_text
        jmp .fff_done
.fff_addr:
        mov eax, [esi + FW_R_IP]
        call fw_fmt_ip
        mov al, '/'
        stosb
        mov esi, [fw_text_ptr]
        mov eax, [esi + FW_R_MASK]
        call fw_mask_bits
        call itoa_dec_edi
.fff_done:
        mov byte [edi], 0
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; fw_rule_append - parse a rule text and store it, live and in the Sett
;   In : ESI = text
;   Out: CF clear on success, EAX = the index it went in at
;=======================================================================
fw_rule_append:
        pushad
        mov [fw_text_ptr], esi
        mov eax, [fw_rule_count]
        cmp eax, FW_MAX_RULES
        jae .fra_bad
        imul eax, FW_R_SIZE
        lea edi, [fw_rules + eax]
        mov esi, [fw_text_ptr]
        call fw_parse_rule
        jc .fra_bad

        ; Normalize before storing: the entry is the truth, and a rule typed
        ; as "TCP 4450" must not reload as something else.
        mov eax, [fw_rule_count]
        imul eax, FW_R_SIZE
        lea esi, [fw_rules + eax]
        mov edi, fw_text_buf
        call fw_fmt_rule

        mov ebx, [fw_rule_count]
        mov edi, fw_name_buf
        call fw_rule_name
        mov edi, fw_key_path
        mov edx, fw_text_buf
        mov esi, fw_name_buf
        call sett_kv_set_str
        jc .fra_undo

        inc dword [fw_rule_count]
        mov eax, [fw_rule_count]
        dec eax
        mov [esp + 28], eax
        popad
        clc
        ret
.fra_undo:
        ; The store refused it, so the live table must not keep it either.
        mov eax, [fw_rule_count]
        imul eax, FW_R_SIZE
        lea edi, [fw_rules + eax]
        mov byte [edi + FW_R_USED], 0
.fra_bad:
        mov eax, -1
        popad
        stc
        ret

;=======================================================================
; fw_rule_delete - drop rule N, and compact the store so the next boot
;                  sees the same list
;   In : EAX = index
;   Out: CF clear on success
;=======================================================================
fw_rule_delete:
        pushad
        mov [fw_loop_i], eax
        cmp eax, [fw_rule_count]
        jae .frd_bad
        ; shift the live table down over the hole
        mov eax, [fw_loop_i]
        inc eax
        mov [fw_loop_j], eax
.frd_shift:
        mov eax, [fw_loop_j]
        cmp eax, [fw_rule_count]
        jae .frd_store
        imul eax, FW_R_SIZE
        lea esi, [fw_rules + eax]
        sub eax, FW_R_SIZE
        lea edi, [fw_rules + eax]
        push ecx
        mov ecx, FW_R_SIZE
        cld
        rep movsb
        pop ecx
        inc dword [fw_loop_j]
        jmp .frd_shift
.frd_store:
        dec dword [fw_rule_count]
        mov dword [fw_loop_j], 0
.frd_write:
        mov eax, [fw_loop_j]
        cmp eax, [fw_rule_count]
        jae .frd_trim
        imul eax, FW_R_SIZE
        lea esi, [fw_rules + eax]
        mov edi, fw_text_buf
        call fw_fmt_rule
        mov ebx, [fw_loop_j]
        mov edi, fw_name_buf
        call fw_rule_name
        mov edi, fw_key_path
        mov edx, fw_text_buf
        mov esi, fw_name_buf
        call sett_kv_set_str
        inc dword [fw_loop_j]
        jmp .frd_write
.frd_trim:
        ; The name one past the end is the one that was just removed.
        mov ebx, [fw_rule_count]
        mov edi, fw_name_buf
        call fw_rule_name
        mov edi, fw_key_path
        mov esi, fw_name_buf
        call sett_kv_del               ; absent is not an error here
        popad
        clc
        ret
.frd_bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; fw_save_enabled - write Enabled so the next boot agrees with this one
;-----------------------------------------------------------------------
fw_save_enabled:
        pushad
        mov edx, fw_text_off
        cmp dword [fw_enabled], 0
        je .fse_write
        mov edx, fw_text_on
.fse_write:
        mov edi, fw_key_path
        mov esi, fw_name_enabled
        call sett_kv_set_str
        popad
        ret

;=======================================================================
; cmd_net_fw - "net fw [list | add <rule> | del <n> | on | off]"
;
; The rules interface NETWORK.md section 7 asks for. With no verb it reports
; the state and the counters, which is the thing to run when a connection
; that should work does not.
;   In : ESI = rest of the command line, past the verb
;=======================================================================
cmd_net_fw:
        pushad
        call skip_spaces
        mov [.nfw_line], esi
        cmp byte [esi], 0
        je .nfw_show

        ; str_starts_with, not str_compare: the line carries arguments
        ; ("add tcp 4450") and needs a word boundary rather than a match to
        ; the end of the string. On a match ESI is already past the verb.
        mov edi, fw_word_list
        call str_starts_with
        jc .nfw_list
        mov esi, [.nfw_line]
        mov edi, fw_word_add
        call str_starts_with
        jc .nfw_add
        mov esi, [.nfw_line]
        mov edi, fw_word_del
        call str_starts_with
        jc .nfw_del
        mov esi, [.nfw_line]
        mov edi, fw_word_reload
        call str_starts_with
        jc .nfw_reload
        mov esi, [.nfw_line]
        mov edi, fw_word_on
        call str_starts_with
        jc .nfw_on
        mov esi, [.nfw_line]
        mov edi, fw_word_off
        call str_starts_with
        jc .nfw_off
        jmp .nfw_usage

.nfw_show:
        mov esi, msg_fw_state
        call vga_print
        cmp dword [fw_enabled], 0
        je .nfw_show_off
        mov esi, msg_fw_on
        jmp .nfw_show_state
.nfw_show_off:
        mov esi, msg_fw_off
.nfw_show_state:
        call vga_print
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_fw_rules
        call vga_print
        mov eax, [fw_rule_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_fw_allowed
        call vga_print
        mov eax, [fw_allowed]
        call vga_print_dec
        mov esi, msg_fw_blocked
        call vga_print
        mov eax, [fw_blocked]
        call vga_print_dec
        mov esi, msg_fw_blocked_udp
        call vga_print
        mov eax, [fw_blocked_udp]
        call vga_print_dec
        mov al, ')'
        call vga_putchar
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.nfw_list:
        mov esi, msg_fw_list_head
        call vga_print
        mov dword [.nfw_i], 0
.nfw_list_loop:
        mov eax, [.nfw_i]
        cmp eax, [fw_rule_count]
        jae .nfw_list_done
        mov esi, msg_fw_two_spaces
        call vga_print
        mov eax, [.nfw_i]
        call vga_print_dec
        mov esi, msg_fw_gap
        call vga_print
        mov eax, [.nfw_i]
        imul eax, FW_R_SIZE
        lea esi, [fw_rules + eax]
        mov edi, fw_text_buf
        call fw_fmt_rule
        mov esi, fw_text_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar
        inc dword [.nfw_i]
        jmp .nfw_list_loop
.nfw_list_done:
        popad
        ret

.nfw_add:
        cmp byte [esi], 0
        je .nfw_usage
        call fw_rule_append
        jc .nfw_add_bad
        mov [.nfw_i], eax
        mov esi, msg_fw_added
        call vga_print
        mov eax, [.nfw_i]
        call vga_print_dec
        mov esi, msg_fw_gap
        call vga_print
        mov eax, [.nfw_i]
        imul eax, FW_R_SIZE
        lea esi, [fw_rules + eax]
        mov edi, fw_text_buf
        call fw_fmt_rule
        mov esi, fw_text_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.nfw_add_bad:
        mov esi, msg_fw_add_failed
        call vga_print
        popad
        ret

.nfw_del:
        cmp byte [esi], 0
        je .nfw_usage
        call parse_decimal
        cmp byte [esi], 0
        jne .nfw_usage
        mov [.nfw_i], eax
        call fw_rule_delete
        jc .nfw_del_bad
        mov esi, msg_fw_deleted
        call vga_print
        mov eax, [.nfw_i]
        call vga_print_dec
        mov esi, msg_fw_deleted2
        call vga_print
        popad
        ret
.nfw_del_bad:
        mov esi, msg_fw_del_failed
        call vga_print
        popad
        ret

.nfw_reload:
        call fw_reload
        mov esi, msg_fw_reloaded
        call vga_print
        mov eax, [fw_rule_count]
        call vga_print_dec
        mov esi, msg_fw_rules_word
        call vga_print
        mov eax, [fw_bad_rules]
        test eax, eax
        jz .nfw_reload_done
        mov esi, msg_fw_refused
        call vga_print
        mov eax, [fw_bad_rules]
        call vga_print_dec
.nfw_reload_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.nfw_on:
        mov dword [fw_enabled], 1
        call fw_save_enabled
        mov esi, msg_fw_enabled
        call vga_print
        popad
        ret
.nfw_off:
        mov dword [fw_enabled], 0
        call fw_save_enabled
        mov esi, msg_fw_disabled
        call vga_print
        popad
        ret
.nfw_usage:
        mov esi, msg_fw_usage
        call vga_print
        popad
        ret

.nfw_line: dd 0
.nfw_i:    dd 0

;=======================================================================
; Strings
;=======================================================================
msg_fw_state:       db "Firewall: ", 0
msg_fw_on:          db "on", 0
msg_fw_off:         db "off", 0
msg_fw_rules:       db "Rules: ", 0
msg_fw_allowed:     db "Admitted: ", 0
msg_fw_blocked:     db "   Blocked: ", 0
msg_fw_blocked_udp: db " (udp ", 0
msg_fw_list_head:   db "Rules (index, then the line the Sett holds):", 0x0A, 0
msg_fw_two_spaces:  db "  ", 0
msg_fw_gap:         db "  ", 0
msg_fw_added:       db "fw: rule ", 0
msg_fw_deleted:     db "fw: rule ", 0
msg_fw_deleted2:    db " removed", 0x0A, 0
msg_fw_add_failed:  db "fw: the rule was not added; 16 is the limit and the line", 0x0A
                    db "    is '<tcp|udp|any> <port> [remote[/bits]|any]'", 0x0A, 0
msg_fw_del_failed:  db "fw: no such rule", 0x0A, 0
msg_fw_enabled:     db "fw: on", 0x0A, 0
msg_fw_disabled:    db "fw: off, an inbound packet is no longer checked", 0x0A, 0
msg_fw_usage:       db "net fw | net fw list | net fw add <tcp|udp|any> <port> [remote[/bits]]", 0x0A
                    db "       net fw del <n> | net fw on | net fw off | net fw reload", 0x0A, 0
msg_fw_audit:       db "fw: blocked inbound from ", 0
msg_fw_bad_rule:    db "fw: a stored rule was refused: ", 0
msg_fw_reloaded:    db "fw: reloaded ", 0
msg_fw_rules_word:  db " rule(s)", 0
msg_fw_refused:     db ", and refused ", 0
