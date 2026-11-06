;=======================================================================
; rpc.inc - remote procedure call: one frame, one table, two transports
;
; NETWORK.md section 5. A call is dispatched by (interface, version, opnum):
; the interface is a 16-byte identifier, the version a major.minor pair, and
; the opnum a number the interface allocates. The frame is fixed-layout and
; little-endian, so a stub needs no marshaller and the kernel needs no
; compiler.
;
;   request   uuid[16] major:1 minor:1 opnum:2 impersonation:1 pad:3 len:4 body
;   reply     status:4 len:4 body
;
; An interface writes its answer to rpc_body_buf and sets [rpc_body_len];
; rpc_dispatch wraps that in the reply header. Both transports - a call from
; this machine and a call over a share connection - therefore carry the same
; bytes, and a stub does not know which it is on.
;
; What this is not, and the spec should say so where it says "as built":
;
;   - No sections and no handle transfer: one frame fits one share message
;     (SHARE_BUF), so the inline limit is 1200 bytes of body, not 64 KiB.
;   - No generated stubs. A handler is a function with the calling convention
;     below, and the header is written by hand.
;   - The identifiers are 16 bytes of text rather than RFC 4122 UUIDs: the
;     kernel has no UUID formatter, and a legible id is worth more here than a
;     canonical one. They are still 16 opaque bytes to the dispatcher.
;   - Impersonation is parsed and enforced only as far as this machine has an
;     identity to hand on, which is not at all: `delegate` is refused with its
;     own status, and `identify`/`impersonate` are accepted but do not change
;     the authority of the call, because a share session is authenticated as
;     "the holder of the secret", not as a user.
;=======================================================================

;-----------------------------------------------------------------------
; The frame
;-----------------------------------------------------------------------
RPC_HDR_UUID       equ 0              ; 16 bytes
RPC_HDR_MAJOR      equ 16
RPC_HDR_MINOR      equ 17
RPC_HDR_OPNUM      equ 18             ; word
RPC_HDR_IMPERSON   equ 20
RPC_HDR_BODYLEN    equ 24             ; dword
RPC_HDR_LEN        equ 28

RPC_REP_STATUS     equ 0
RPC_REP_BODYLEN    equ 4
RPC_REP_LEN        equ 8

;-----------------------------------------------------------------------
; Statuses. A transport failure is not one of these: the caller that cannot
; reach the server returns -1 from its own layer, so "the wire was down" and
; "the server said no" are never the same answer.
;-----------------------------------------------------------------------
RPC_OK                   equ 0
RPC_S_UNKNOWN_IF         equ 1
RPC_S_BAD_VERSION        equ 2
RPC_S_UNKNOWN_OPNUM      equ 3
RPC_S_BAD_ARG            equ 4
RPC_S_ACCESS_DENIED      equ 5
RPC_S_NOT_AUTHENTICATED  equ 6
RPC_S_NO_DELEGATE        equ 7
RPC_S_NOT_FOUND          equ 8
RPC_S_TOO_BIG            equ 9

;-----------------------------------------------------------------------
; Impersonation levels (NETWORK.md section 5)
;-----------------------------------------------------------------------
RPC_IMP_IDENTIFY    equ 0
RPC_IMP_IMPERSONATE equ 1
RPC_IMP_DELEGATE    equ 2

;-----------------------------------------------------------------------
; Interface table
;-----------------------------------------------------------------------
RPC_IF_MAX      equ 8
RPC_E_ID        equ 0                 ; 16 bytes
RPC_E_MAJOR     equ 16
RPC_E_MINOR     equ 17
RPC_E_OPCOUNT   equ 18                ; word
RPC_E_DISPATCH  equ 20                ; dword
RPC_E_SIZE      equ 24

; One body, so both directions fit inside SHARE_BUF (1400) once the header is
; added, whichever way a future transport splits its messages.
RPC_BODY_MAX    equ 1200

align 4
rpc_ifaces:        times RPC_IF_MAX * RPC_E_SIZE db 0
rpc_iface_count:   dd 0
rpc_body_buf:      times RPC_BODY_MAX db 0
rpc_body_len:      dd 0
rpc_reply_buf:     times RPC_REP_LEN + RPC_BODY_MAX db 0
rpc_reply_len:     dd 0
rpc_call_status:   dd 0
rpc_cur_iface:     dd 0
rpc_req_frame:     dd 0
rpc_req_len:       dd 0
rpc_req_buf:       times RPC_HDR_LEN + RPC_BODY_MAX db 0
rpc_frame_len:     dd 0
rpc_key_buf:       times 128 db 0
rpc_path_buf:      times 128 db 0
rpc_tmp_val:       dd 0
rpc_audit_buf:     times 96 db 0

;-----------------------------------------------------------------------
; rpc_split_path - split "\Registry\Machine\Key\Name" at the last backslash
;   In : ESI = the string, EDI = buffer for the key path
;   Out: ESI = the value name, EDI = the key path; CF set when there is no
;        backslash (a bare name has no key to look it up in)
;-----------------------------------------------------------------------
rpc_split_path:
        push eax
        push ebx
        push ecx
        push edx
        mov eax, esi
        xor ecx, ecx
.rsp_len:
        cmp byte [eax + ecx], 0
        je .rsp_have
        inc ecx
        jmp .rsp_len
.rsp_have:
        mov edx, ecx
.rsp_scan:
        test edx, edx
        jz .rsp_none
        dec edx
        cmp byte [eax + edx], '\'
        jne .rsp_scan
        push esi
        mov esi, eax
        mov ecx, edx
        cld
        rep movsb
        mov byte [edi], 0
        pop esi
        lea esi, [esi + edx + 1]
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret
.rsp_none:
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret

;-----------------------------------------------------------------------
; rpc_id_equal - compare two 16-byte interface identifiers
;   In : ESI, EDI
;   Out: ZF set when they are equal
;-----------------------------------------------------------------------
rpc_id_equal:
        push esi
        push edi
        push ecx
        mov ecx, 16
        cld
        repe cmpsb
        pop ecx
        pop edi
        pop esi
        ret

;-----------------------------------------------------------------------
; rpc_strlen - In: ESI -> EAX = byte count before the terminator
;-----------------------------------------------------------------------
rpc_strlen:
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

;-----------------------------------------------------------------------
; rpc_str_copy - copy a NUL-terminated string, EDI advanced past the terminator
;   In : ESI = source, EDI = destination
;-----------------------------------------------------------------------
rpc_str_copy:
        push eax
.rsc_loop:
        lodsb
        stosb
        test al, al
        jnz .rsc_loop
        dec edi
        pop eax
        ret

;-----------------------------------------------------------------------
; rpc_u32_dec - write a decimal number, EDI advanced
;   In : EAX = value, EDI = destination
;-----------------------------------------------------------------------
rpc_u32_dec:
        push eax
        push ebx
        push ecx
        push edx
        mov ebx, 10
        xor ecx, ecx
.rud_div:
        xor edx, edx
        div ebx
        add dl, '0'
        push edx
        inc ecx
        test eax, eax
        jnz .rud_div
.rud_out:
        pop eax
        stosb
        dec ecx
        jnz .rud_out
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; rpc_register - add one interface, and its binding under \RpcControl
;   In : ESI = 16-byte id, EAX = major, EBX = minor, ECX = opcount,
;        EDX = handler
;
; The binding is a Sett value named after the interface whose text is
; "<major>.<minor>", which is how a client learns what a machine serves
; without knowing a port number: `sett get \RpcControl\mv.rpc/config`.
;-----------------------------------------------------------------------
rpc_register:
        pushad
        mov [.rr_id], esi
        mov [.rr_major], eax
        mov [.rr_minor], ebx
        mov [.rr_ops], ecx
        mov [.rr_fn], edx

        mov eax, [rpc_iface_count]
        cmp eax, RPC_IF_MAX
        jae .rr_done
        imul eax, RPC_E_SIZE
        lea edi, [rpc_ifaces + eax]
        mov [.rr_base], edi
        mov esi, [.rr_id]
        mov ecx, 16
        cld
        rep movsb                       ; EDI has advanced past the id
        ; The fields are addressed from the entry's base, not from EDI: the
        ; copy left EDI at RPC_E_MAJOR, and adding RPC_E_MAJOR to that wrote
        ; sixteen bytes past the entry - which is why `net iface` printed
        ; every interface as v0.0 with no operations.
        mov edi, [.rr_base]
        mov eax, [.rr_major]
        mov [edi + RPC_E_MAJOR], al
        mov eax, [.rr_minor]
        mov [edi + RPC_E_MINOR], al
        mov eax, [.rr_ops]
        mov [edi + RPC_E_OPCOUNT], ax
        mov eax, [.rr_fn]
        mov [edi + RPC_E_DISPATCH], eax
        inc dword [rpc_iface_count]

        ; the binding: "\RpcControl" <- <id> = "<major>.<minor>"
        mov edi, rpc_bind_key
        mov esi, [.rr_id]
        mov eax, [.rr_major]
        mov [rpc_bind_ver], al
        mov eax, [.rr_minor]
        mov [rpc_bind_ver + 1], al
        mov byte [rpc_bind_ver + 2], '.'
        mov byte [rpc_bind_ver + 3], 0
        ; "1" "." "0": one digit each, with a decimal fallback for more
        mov al, [rpc_bind_ver]
        mov edi, rpc_bind_text
        call .rr_digit
        mov al, '.'
        stosb
        mov al, [rpc_bind_ver + 1]
        call .rr_digit
        mov byte [edi], 0

        mov edi, rpc_bind_key
        mov esi, [.rr_id]
        mov edx, rpc_bind_text
        call sett_kv_set_str
.rr_done:
        popad
        ret
; AL = value 0..15 -> one hex-ish digit at EDI (decimal below ten, 'A'+ for
; the rest, so the binding never lies about a version it cannot print)
.rr_digit:
        cmp al, 9
        ja .rr_hex
        add al, '0'
        stosb
        ret
.rr_hex:
        add al, 'A' - 10
        stosb
        ret
.rr_id:    dd 0
.rr_base:  dd 0
.rr_major: dd 0
.rr_minor: dd 0
.rr_ops:   dd 0
.rr_fn:    dd 0

rpc_bind_key:  db "\RpcControl", 0
rpc_bind_text: times 8 db 0
rpc_bind_ver:  times 4 db 0

;=======================================================================
; rpc_init - the interfaces this machine serves
;=======================================================================
rpc_init:
        pushad
        mov dword [rpc_iface_count], 0
        mov edi, rpc_ifaces
        mov ecx, (RPC_IF_MAX * RPC_E_SIZE) / 4
        xor eax, eax
        cld
        rep stosd

        mov esi, rpc_id_config
        mov eax, 1
        xor ebx, ebx
        mov ecx, RPC_CONFIG_OPS
        mov edx, rpc_config_dispatch
        call rpc_register

        mov esi, rpc_id_eventlog
        mov eax, 1
        xor ebx, ebx
        mov ecx, RPC_EVENTLOG_OPS
        mov edx, rpc_eventlog_dispatch
        call rpc_register
        popad
        ret

;=======================================================================
; rpc_dispatch - run one call frame
;   In : ESI = frame, ECX = its length
;   Out: EAX = status (RPC_*); [rpc_reply_len] bytes at rpc_reply_buf are the
;        reply frame, status and body included
;=======================================================================
rpc_dispatch:
        pushad
        mov [rpc_req_frame], esi
        mov [rpc_req_len], ecx
        mov dword [rpc_reply_len], 0
        mov dword [rpc_body_len], 0

        cmp ecx, RPC_HDR_LEN
        jb .rd_bad_arg

        ; The body has to fit in the frame it arrived in. A length that
        ; overstated it would let a handler read past the message.
        mov eax, [esi + RPC_HDR_BODYLEN]
        add eax, RPC_HDR_LEN
        cmp eax, [rpc_req_len]
        ja .rd_bad_arg
        mov eax, [esi + RPC_HDR_BODYLEN]
        cmp eax, RPC_BODY_MAX
        ja .rd_too_big

        ; Impersonation. `delegate` needs a token to hand on and this
        ; transport authenticates a session, not a user, so it is refused
        ; rather than quietly treated as `impersonate`.
        movzx eax, byte [esi + RPC_HDR_IMPERSON]
        cmp eax, RPC_IMP_DELEGATE
        je .rd_no_delegate
        cmp eax, RPC_IMP_IMPERSONATE
        ja .rd_bad_arg

        ; find the interface
        mov edi, rpc_ifaces
        xor edx, edx
.rd_if_scan:
        cmp edx, [rpc_iface_count]
        jae .rd_no_iface
        push edx
        push edi
        mov esi, [rpc_req_frame]
        call rpc_id_equal
        pop edi
        pop edx
        je .rd_if_found
        add edi, RPC_E_SIZE
        inc edx
        jmp .rd_if_scan

.rd_if_found:
        mov [rpc_cur_iface], edi
        mov esi, [rpc_req_frame]

        ; version: the major must match exactly, and a minor newer than the
        ; one served is refused - a call that expects an opnum this build does
        ; not have must not be dispatched against the older table.
        movzx eax, byte [esi + RPC_HDR_MAJOR]
        movzx ebx, byte [edi + RPC_E_MAJOR]
        cmp eax, ebx
        jne .rd_bad_version
        movzx eax, byte [esi + RPC_HDR_MINOR]
        movzx ebx, byte [edi + RPC_E_MINOR]
        cmp eax, ebx
        ja .rd_bad_version

        movzx eax, word [esi + RPC_HDR_OPNUM]
        movzx ebx, word [edi + RPC_E_OPCOUNT]
        cmp eax, ebx
        jae .rd_no_opnum

        ; call it: EAX = opnum, ESI = body, ECX = body length
        mov ecx, [esi + RPC_HDR_BODYLEN]
        add esi, RPC_HDR_LEN
        ; The default is the handler's if it forgets to publish: a handler ends
        ; with popad, which restores the EAX it was called with, so a status
        ; left in EAX is lost. They write rpc_call_status instead.
        mov dword [rpc_call_status], RPC_S_BAD_ARG
        mov edi, [rpc_cur_iface]
        mov ebx, [edi + RPC_E_DISPATCH]
        call ebx
        jmp .rd_build

.rd_bad_arg:
        mov al, RPC_S_BAD_ARG
        jmp .rd_refuse
.rd_too_big:
        mov al, RPC_S_TOO_BIG
        jmp .rd_refuse
.rd_no_delegate:
        mov al, RPC_S_NO_DELEGATE
        jmp .rd_refuse
.rd_no_iface:
        mov al, RPC_S_UNKNOWN_IF
        jmp .rd_refuse
.rd_bad_version:
        mov al, RPC_S_BAD_VERSION
        jmp .rd_refuse
.rd_no_opnum:
        mov al, RPC_S_UNKNOWN_OPNUM

.rd_refuse:
        movzx eax, al
        mov [rpc_call_status], eax
        mov dword [rpc_body_len], 0
        ; NETWORK.md section 6: every refusal is an audit record. The status
        ; number goes in the record, because "it was refused" without the
        ; reason is the useless half of an audit trail.
        mov edi, rpc_audit_buf
        mov esi, msg_rpc_refused
        call rpc_str_copy
        mov eax, [rpc_call_status]
        call rpc_u32_dec
        mov byte [edi], 0
        mov al, LOG_WARN
        mov esi, rpc_audit_buf
        call bootlog_write

.rd_build:
        mov eax, [rpc_call_status]
        mov [rpc_reply_buf + RPC_REP_STATUS], eax
        mov eax, [rpc_body_len]
        mov [rpc_reply_buf + RPC_REP_BODYLEN], eax
        mov ecx, eax
        lea esi, [rpc_body_buf]
        lea edi, [rpc_reply_buf + RPC_REP_LEN]
        cld
        rep movsb
        mov eax, [rpc_body_len]
        add eax, RPC_REP_LEN
        mov [rpc_reply_len], eax

        mov eax, [rpc_call_status]
        mov [esp + 28], eax
        popad
        ret

;=======================================================================
; rpc_build_frame - assemble a request in rpc_req_buf
;   In : EAX = interface id, EBX = (major << 8) | minor (so BH is the major
;        version and BL the minor), ECX = opnum, ESI = body, EDX = body length
;   Out: [rpc_frame_len] = total frame length; CF set when the body will not fit
;
; The impersonation level is always `identify`: this machine has no user token
; to hand a callee, and claiming `impersonate` would be a lie the far end
; cannot check.
;=======================================================================
rpc_build_frame:
        pushad
        mov [.rb_id], eax
        mov [.rb_ver], ebx
        mov [.rb_op], ecx
        mov [.rb_body], esi
        mov [.rb_len], edx

        mov eax, edx
        cmp eax, RPC_BODY_MAX
        ja .rb_big

        mov edi, rpc_req_buf
        mov esi, [.rb_id]
        mov ecx, 16
        cld
        rep movsb

        ; EBX is (major << 8) | minor, so the major is the high byte. Taking
        ; the low byte for it made every call arrive as 0.1 and the server
        ; answer RPC_S_BAD_VERSION, which is exactly what it should do with a
        ; version it does not serve.
        mov eax, [.rb_ver]
        mov [rpc_req_buf + RPC_HDR_MAJOR], ah
        mov [rpc_req_buf + RPC_HDR_MINOR], al
        mov eax, [.rb_op]
        mov [rpc_req_buf + RPC_HDR_OPNUM], ax
        mov byte [rpc_req_buf + RPC_HDR_IMPERSON], RPC_IMP_IDENTIFY
        mov byte [rpc_req_buf + RPC_HDR_IMPERSON + 1], 0
        mov byte [rpc_req_buf + RPC_HDR_IMPERSON + 2], 0
        mov byte [rpc_req_buf + RPC_HDR_IMPERSON + 3], 0
        mov eax, [.rb_len]
        mov [rpc_req_buf + RPC_HDR_BODYLEN], eax

        mov ecx, eax
        mov esi, [.rb_body]
        lea edi, [rpc_req_buf + RPC_HDR_LEN]
        rep movsb

        mov eax, [.rb_len]
        add eax, RPC_HDR_LEN
        mov [rpc_frame_len], eax
        popad
        clc
        ret
.rb_big:
        popad
        stc
        ret
.rb_id:   dd 0
.rb_ver:  dd 0
.rb_op:   dd 0
.rb_body: dd 0
.rb_len:  dd 0

;=======================================================================
; The configuration interface - Sett keys over the wire
;
;   op 0  GET  body = "\Registry\Machine\Key\Name"
;               replybody = the string, NUL included
;   op 1  SET  body = "\Registry\Machine\Key\Name\0value"
;               replybody = empty
;
; A key and its value name are one string to the caller, split at the last
; backslash, because that is how a person types a registry path.
;=======================================================================
RPC_CONFIG_GET   equ 0
RPC_CONFIG_SET   equ 1
RPC_CONFIG_OPS   equ 2

rpc_config_dispatch:
        pushad
        cmp eax, RPC_CONFIG_GET
        je .rcd_get
        cmp eax, RPC_CONFIG_SET
        je .rcd_set
        mov eax, RPC_S_UNKNOWN_OPNUM
        jmp .rcd_out

.rcd_get:
        test ecx, ecx
        jz .rcd_bad
        mov eax, ecx
        cmp eax, 127
        jbe .rcd_get_copy
        mov eax, 127
.rcd_get_copy:
        mov edi, rpc_key_buf
        mov ecx, eax
        cld
        rep movsb
        mov byte [edi], 0

        mov esi, rpc_key_buf
        mov edi, rpc_path_buf
        call rpc_split_path
        jc .rcd_bad
        mov edi, rpc_path_buf
        call sett_kv_get_str           ; ESI = the string now
        jc .rcd_not_found

        mov edi, rpc_body_buf
        xor ecx, ecx
.rcd_copy_val:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        test al, al
        jz .rcd_val_done
        inc ecx
        cmp ecx, RPC_BODY_MAX - 1
        jb .rcd_copy_val
        mov byte [edi + ecx], 0
.rcd_val_done:
        inc ecx                        ; the caller gets the terminator
        mov [rpc_body_len], ecx
        xor eax, eax
        jmp .rcd_out
.rcd_not_found:
        mov eax, RPC_S_NOT_FOUND
        jmp .rcd_out

.rcd_set:
        test ecx, ecx
        jz .rcd_bad
        mov eax, ecx
        cmp eax, 127
        jbe .rcd_set_copy
        mov eax, 127
.rcd_set_copy:
        mov edi, rpc_key_buf
        mov ecx, eax
        cld
        rep movsb
        mov byte [edi], 0

        ; the value starts after the key's terminator
        mov esi, rpc_key_buf
        xor ecx, ecx
.rcd_set_nul:
        cmp byte [esi + ecx], 0
        je .rcd_set_have
        inc ecx
        cmp ecx, 127
        jb .rcd_set_nul
        jmp .rcd_bad
.rcd_set_have:
        lea eax, [esi + ecx + 1]
        mov [rpc_tmp_val], eax

        mov edi, rpc_path_buf
        call rpc_split_path
        jc .rcd_bad
        mov edi, rpc_path_buf
        mov edx, [rpc_tmp_val]
        call sett_kv_set_str
        jc .rcd_bad

        ; A write that changes the machine is worth a record of its own.
        mov edi, rpc_audit_buf
        mov esi, msg_rpc_config_set
        call rpc_str_copy
        mov esi, rpc_key_buf
        call rpc_str_copy
        mov byte [edi], 0
        mov al, LOG_INFO
        mov esi, rpc_audit_buf
        call bootlog_write

        mov dword [rpc_body_len], 0
        xor eax, eax
        jmp .rcd_out
.rcd_bad:
        mov eax, RPC_S_BAD_ARG
.rcd_out:
        mov [rpc_call_status], eax
        popad
        ret

;=======================================================================
; The event log interface - the boot log ring over the wire
;
;   op 0  COUNT  replybody = dword
;   op 1  READ   body = dword index (0 = oldest held)
;                replybody = level:1 tick:4 source[16] text[80]
;=======================================================================
RPC_EVENTLOG_COUNT equ 0
RPC_EVENTLOG_READ  equ 1
RPC_EVENTLOG_OPS   equ 2

RPC_LOG_RECORD     equ 101            ; 1 + 4 + 16 + 80

rpc_eventlog_dispatch:
        pushad
        cmp eax, RPC_EVENTLOG_COUNT
        je .red_count
        cmp eax, RPC_EVENTLOG_READ
        je .red_read
        mov eax, RPC_S_UNKNOWN_OPNUM
        jmp .red_out

.red_count:
        mov eax, [bootlog_count]
        mov [rpc_body_buf], eax
        mov dword [rpc_body_len], 4
        xor eax, eax
        jmp .red_out

.red_read:
        cmp ecx, 4
        jb .red_bad
        mov eax, [esi]
        cmp eax, [bootlog_count]
        jae .red_bad

        ; slot = (head - count + index) mod BOOTLOG_MAX
        mov edx, [bootlog_head]
        sub edx, [bootlog_count]
        jnc .red_no_wrap
        add edx, BOOTLOG_MAX
.red_no_wrap:
        add edx, eax
        cmp edx, BOOTLOG_MAX
        jb .red_slot
        sub edx, BOOTLOG_MAX
.red_slot:
        imul edx, BOOTLOG_SIZE
        add edx, bootlog_ring

        mov edi, rpc_body_buf
        mov al, [edx + BL_LEVEL]
        mov [edi], al
        mov eax, [edx + BL_TICK]
        mov [edi + 1], eax

        push esi
        lea esi, [edx + BL_SOURCE]
        lea edi, [edi + 5]
        mov ecx, 16
        cld
        rep movsb                       ; source
        lea esi, [edx + BL_TEXT]
        mov edi, rpc_body_buf + 21
        mov ecx, BL_TEXT_MAX
        rep movsb                       ; text, one NUL short of the field
        pop esi
        mov byte [rpc_body_buf + 21 + BL_TEXT_MAX], 0
        mov dword [rpc_body_len], RPC_LOG_RECORD
        xor eax, eax
        jmp .red_out
.red_bad:
        mov eax, RPC_S_BAD_ARG
.red_out:
        mov [rpc_call_status], eax
        popad
        ret

;=======================================================================
; Identifiers and strings
;=======================================================================
rpc_id_config:   db "mv.rpc/config", 0, 0, 0
rpc_id_eventlog: db "mv.rpc/eventlog", 0

msg_rpc_refused:     db "rpc: refused, status ", 0
msg_rpc_config_set:  db "rpc: config set ", 0
