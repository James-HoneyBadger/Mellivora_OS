;=======================================================================
; gdbstub.inc - GDB Remote Serial Protocol (RSP) stub over COM1
;
; Allows remote debugging of the kernel via GDB connected to the
; COM1 serial port (115200 8N1, same as kernel console).
;
; Handles:
;   INT 3  (0x03) - software breakpoint / SYS_BREAKPOINT
;   INT 1  (0x01) - single-step (Debug exception)
;
; Supported GDB RSP commands:
;   ?          - Halt reason (signals S05 = SIGTRAP)
;   g          - Read all registers
;   G          - Write all registers
;   p n        - Read register n
;   P n=v      - Write register n
;   m addr,len - Read memory
;   M addr,len:data - Write memory
;   c [addr]   - Continue (optional new PC)
;   s [addr]   - Single-step
;   Z0,addr,4  - Insert software breakpoint (write 0xCC)
;   z0,addr,4  - Remove software breakpoint (restore saved byte)
;   k          - Kill (reboot)
;   q ...      - Query (minimal stub answer)
;
; GDB i386 register order (for 'g' packet):
;   EAX(0) ECX(1) EDX(2) EBX(3) ESP(4) EBP(5) ESI(6) EDI(7)
;   EIP(8) EFLAGS(9) CS(10) SS(11) DS(12) ES(13) FS(14) GS(15)
;
; Usage from GDB:
;   target remote /dev/ttyS0   (or target remote localhost:1234 in QEMU)
;   set architecture i386
;   b *0x100000                (for kernel entry)
;=======================================================================

; Max breakpoints we track for z/Z commands
GDB_MAX_BP      equ 32

; Signal values for '?' reply
GDB_SIGTRAP     equ 5

; EFLAGS trap flag
EFLAGS_TF       equ (1<<8)

; Register indices in gdb_regs[] buffer
GDB_REG_EAX     equ 0
GDB_REG_ECX     equ 1
GDB_REG_EDX     equ 2
GDB_REG_EBX     equ 3
GDB_REG_ESP     equ 4
GDB_REG_EBP     equ 5
GDB_REG_ESI     equ 6
GDB_REG_EDI     equ 7
GDB_REG_EIP     equ 8
GDB_REG_EFL     equ 9
GDB_REG_CS      equ 10
GDB_REG_SS      equ 11
GDB_REG_DS      equ 12
GDB_REG_ES      equ 13
GDB_REG_FS      equ 14
GDB_REG_GS      equ 15
GDB_REG_COUNT   equ 16

;=======================================================================
; gdbstub_init - Install INT 1 and INT 3 handlers
;=======================================================================
gdbstub_init:
        pushad

        ; Replace INT 3 (BP) with our handler — kernel-mode trap gate DPL=0
        mov ecx, 3
        mov eax, gdb_bp_handler
        call idt_set_gate

        ; Replace INT 1 (DB / single-step) with our handler
        mov ecx, 1
        mov eax, gdb_db_handler
        call idt_set_gate

        ; Zero breakpoint table
        mov edi, gdb_bp_table
        mov ecx, GDB_MAX_BP * 8
        xor eax, eax
        rep stosb

        popad
        ret

;=======================================================================
; INT 3 handler — software breakpoint
;=======================================================================
gdb_bp_handler:
        pushad                  ; save EAX..EDI
        ; Stack layout after pushad (all ring-0):
        ;  [esp+0..28] = pushad  (EDI=+0, ESI=+4, EBP=+8, ESP=+12,
        ;                         EBX=+16, EDX=+20, ECX=+24, EAX=+28)
        ;  [esp+32]    = EIP     (INT 3 pushed 3-byte insn; EIP = after CC)
        ;  [esp+36]    = CS
        ;  [esp+40]    = EFLAGS

        ; Save register snapshot to gdb_regs[]
        mov eax, [esp+28]      ; EAX
        mov [gdb_regs + GDB_REG_EAX*4], eax
        mov eax, [esp+24]      ; ECX
        mov [gdb_regs + GDB_REG_ECX*4], eax
        mov eax, [esp+20]      ; EDX
        mov [gdb_regs + GDB_REG_EDX*4], eax
        mov eax, [esp+16]      ; EBX
        mov [gdb_regs + GDB_REG_EBX*4], eax
        ; ESP: the value before the interrupt (before 3 dwords were pushed)
        lea eax, [esp+44]
        mov [gdb_regs + GDB_REG_ESP*4], eax
        mov eax, [esp+8]       ; EBP
        mov [gdb_regs + GDB_REG_EBP*4], eax
        mov eax, [esp+4]       ; ESI
        mov [gdb_regs + GDB_REG_ESI*4], eax
        mov eax, [esp+0]       ; EDI
        mov [gdb_regs + GDB_REG_EDI*4], eax
        mov eax, [esp+32]      ; EIP
        mov [gdb_regs + GDB_REG_EIP*4], eax
        mov eax, [esp+40]      ; EFLAGS
        mov [gdb_regs + GDB_REG_EFL*4], eax

        ; Segment registers
        xor eax, eax
        mov ax, cs
        mov [gdb_regs + GDB_REG_CS*4], eax
        mov ax, ss
        mov [gdb_regs + GDB_REG_SS*4], eax
        mov ax, ds
        mov [gdb_regs + GDB_REG_DS*4], eax
        mov ax, es
        mov [gdb_regs + GDB_REG_ES*4], eax
        mov ax, fs
        mov [gdb_regs + GDB_REG_FS*4], eax
        mov ax, gs
        mov [gdb_regs + GDB_REG_GS*4], eax

        ; Enter RSP loop
        call gdb_rsp_loop       ; returns when 'c' or 's' received

        ; Restore registers from gdb_regs[] back to the saved-register frame
        ; (allows GDB to modify regs via 'G' or 'P' commands)
        mov eax, [gdb_regs + GDB_REG_EAX*4]
        mov [esp+28], eax
        mov eax, [gdb_regs + GDB_REG_ECX*4]
        mov [esp+24], eax
        mov eax, [gdb_regs + GDB_REG_EDX*4]
        mov [esp+20], eax
        mov eax, [gdb_regs + GDB_REG_EBX*4]
        mov [esp+16], eax
        mov eax, [gdb_regs + GDB_REG_EBP*4]
        mov [esp+8], eax
        mov eax, [gdb_regs + GDB_REG_ESI*4]
        mov [esp+4], eax
        mov eax, [gdb_regs + GDB_REG_EDI*4]
        mov [esp+0], eax
        mov eax, [gdb_regs + GDB_REG_EIP*4]
        mov [esp+32], eax
        mov eax, [gdb_regs + GDB_REG_EFL*4]
        mov [esp+40], eax       ; restore potentially TF-modified EFLAGS

        popad
        iretd

;=======================================================================
; INT 1 handler — single-step / debug exception
;=======================================================================
gdb_db_handler:
        ; Single-step / debug exception — same handling as breakpoint
        jmp gdb_bp_handler

;=======================================================================
; gdb_rsp_loop - Main RSP command loop (runs until c/s)
;=======================================================================
gdb_rsp_loop:
        push eax
        push esi
        push edi

.main:
        ; Send halt notification: $S05#b8
        call gdb_send_notification

.cmd_loop:
        call gdb_recv_packet     ; returns EAX=0 on success, packet in gdb_pkt_buf
        test eax, eax
        jnz .main               ; retry on timeout

        ; Dispatch on first byte of packet
        mov al, [gdb_pkt_buf]
        cmp al, '?'
        je  .cmd_question
        cmp al, 'g'
        je  .cmd_g
        cmp al, 'G'
        je  .cmd_G
        cmp al, 'p'
        je  .cmd_p
        cmp al, 'P'
        je  .cmd_P
        cmp al, 'm'
        je  .cmd_m
        cmp al, 'M'
        je  .cmd_M
        cmp al, 'c'
        je  .cmd_c
        cmp al, 's'
        je  .cmd_s
        cmp al, 'Z'
        je  .cmd_Z
        cmp al, 'z'
        je  .cmd_z
        cmp al, 'k'
        je  .cmd_k
        ; Unknown command — send empty OK
        call gdb_send_empty
        jmp .cmd_loop

.cmd_question:
        ; ? -> S05
        call gdb_send_notification
        jmp .cmd_loop

.cmd_g:
        ; g -> all registers as hex
        call gdb_send_regs
        jmp .cmd_loop

.cmd_G:
        ; G... -> write all registers
        call gdb_recv_regs
        call gdb_send_ok
        jmp .cmd_loop

.cmd_p:
        ; p n -> register n as hex
        lea esi, [gdb_pkt_buf + 1]
        call gdb_parse_hex      ; EAX = register number
        cmp eax, GDB_REG_COUNT
        jge .cmd_p_bad
        mov ecx, eax
        mov eax, [gdb_regs + ecx*4]
        call gdb_send_hex32_reply
        jmp .cmd_loop
.cmd_p_bad:
        call gdb_send_error
        jmp .cmd_loop

.cmd_P:
        ; P n=v -> write register n
        lea esi, [gdb_pkt_buf + 1]
        call gdb_parse_hex      ; EAX = reg index
        cmp eax, GDB_REG_COUNT
        jge .cmd_P_bad
        mov ecx, eax
        ; skip '='
        inc esi
        call gdb_parse_hex32    ; EAX = new value
        mov [gdb_regs + ecx*4], eax
        call gdb_send_ok
        jmp .cmd_loop
.cmd_P_bad:
        call gdb_send_error
        jmp .cmd_loop

.cmd_m:
        ; m addr,len -> read memory as hex
        lea esi, [gdb_pkt_buf + 1]
        call gdb_parse_hex      ; EAX = addr
        mov [gdb_tmp_addr], eax
        inc esi                 ; skip ','
        call gdb_parse_hex      ; EAX = len
        mov [gdb_tmp_len], eax
        call gdb_send_mem
        jmp .cmd_loop

.cmd_M:
        ; M addr,len:data -> write memory
        lea esi, [gdb_pkt_buf + 1]
        call gdb_parse_hex
        mov [gdb_tmp_addr], eax
        inc esi                 ; skip ','
        call gdb_parse_hex
        mov [gdb_tmp_len], eax
        inc esi                 ; skip ':'
        call gdb_write_mem      ; reads hex pairs from ESI into addr
        call gdb_send_ok
        jmp .cmd_loop

.cmd_c:
        ; c [addr] -> continue; if addr given, set EIP
        lea esi, [gdb_pkt_buf + 1]
        mov al, [esi]
        cmp al, '#'
        je .c_noadd
        cmp al, 0
        je .c_noadd
        call gdb_parse_hex
        mov [gdb_regs + GDB_REG_EIP*4], eax
.c_noadd:
        ; Clear TF in saved EFLAGS
        and dword [gdb_regs + GDB_REG_EFL*4], ~EFLAGS_TF
        call gdb_send_ok
        jmp .exit               ; return to caller (continue execution)

.cmd_s:
        ; s [addr] -> single step
        lea esi, [gdb_pkt_buf + 1]
        mov al, [esi]
        cmp al, '#'
        je .s_noaddr
        cmp al, 0
        je .s_noaddr
        call gdb_parse_hex
        mov [gdb_regs + GDB_REG_EIP*4], eax
.s_noaddr:
        ; Set TF in saved EFLAGS — CPU will trigger INT 1 after next insn
        or  dword [gdb_regs + GDB_REG_EFL*4], EFLAGS_TF
        call gdb_send_ok
        jmp .exit

.cmd_Z:
        ; Z0,addr,4 — insert SW breakpoint
        lea esi, [gdb_pkt_buf + 1]
        movzx eax, byte [esi]   ; type
        cmp al, '0'
        jne .Z_unsup
        add esi, 2              ; skip '0,'
        call gdb_parse_hex      ; EAX = addr
        call gdb_insert_bp
        call gdb_send_ok
        jmp .cmd_loop
.Z_unsup:
        call gdb_send_empty
        jmp .cmd_loop

.cmd_z:
        ; z0,addr,4 — remove SW breakpoint
        lea esi, [gdb_pkt_buf + 1]
        movzx eax, byte [esi]
        cmp al, '0'
        jne .z_unsup
        add esi, 2
        call gdb_parse_hex
        call gdb_remove_bp
        call gdb_send_ok
        jmp .cmd_loop
.z_unsup:
        call gdb_send_empty
        jmp .cmd_loop

.cmd_k:
        ; k -> kill (triple fault)
        mov byte [0], 0xFF      ; Write to null ptr — triple fault / reset
        ; If that didn't work, halt
        hlt

.exit:
        pop edi
        pop esi
        pop eax
        ret

;=======================================================================
; gdb_send_notification - Send $S05#b8
;=======================================================================
gdb_send_notification:
        push esi
        mov esi, gdb_str_s05
        call gdb_send_raw_packet
        pop esi
        ret

gdb_str_s05: db "S05", 0

;=======================================================================
; gdb_send_ok - Send $OK#9a
;=======================================================================
gdb_send_ok:
        push esi
        mov esi, gdb_str_ok
        call gdb_send_raw_packet
        pop esi
        ret

gdb_str_ok: db "OK", 0

;=======================================================================
; gdb_send_empty - Send $#00 (empty reply = unsupported)
;=======================================================================
gdb_send_empty:
        push esi
        mov esi, gdb_str_empty
        call gdb_send_raw_packet
        pop esi
        ret

gdb_str_empty: db "", 0

;=======================================================================
; gdb_send_error - Send $E01#a6
;=======================================================================
gdb_send_error:
        push esi
        mov esi, gdb_str_e01
        call gdb_send_raw_packet
        pop esi
        ret

gdb_str_e01: db "E01", 0

;=======================================================================
; gdb_send_regs - Send all registers as 16*8 hex digits
;=======================================================================
gdb_send_regs:
        push eax
        push ecx
        push edi

        ; Build the reply in gdb_reply_buf
        mov edi, gdb_reply_buf
        mov ecx, 0
.loop:
        cmp ecx, GDB_REG_COUNT
        jge .done
        mov eax, [gdb_regs + ecx*4]
        ; Write 8 hex bytes (little-endian per GDB RSP)
        call gdb_hex32_le_to_buf    ; writes 8 chars to [EDI], advances EDI
        inc ecx
        jmp .loop
.done:
        mov byte [edi], 0       ; null-terminate
        mov esi, gdb_reply_buf
        call gdb_send_raw_packet

        pop edi
        pop ecx
        pop eax
        ret

;=======================================================================
; gdb_hex32_le_to_buf - Write 32-bit value EAX as 8 hex digits little-endian
; EDI = output pointer; advances EDI by 8
;=======================================================================
gdb_hex32_le_to_buf:
        push eax
        push ecx
        push ebx
        mov ecx, 4
.byte_loop:
        mov bl, al
        shr bl, 4
        call gdb_nib2hex
        mov [edi], al
        inc edi
        mov al, bl
        and al, 0x0F
        call gdb_nib2hex
        mov [edi], al
        inc edi
        ror eax, 8              ; next byte into AL
        loop .byte_loop
        pop ebx
        pop ecx
        pop eax
        ret

gdb_nib2hex:
        ; AL = nibble 0-15 -> AL = '0'-'9' or 'a'-'f'
        cmp al, 10
        jl .digit
        add al, 'a' - 10
        ret
.digit: add al, '0'
        ret

;=======================================================================
; gdb_recv_regs - Parse 128 hex chars from gdb_pkt_buf+1 into gdb_regs
;=======================================================================
gdb_recv_regs:
        push esi
        push ecx
        lea esi, [gdb_pkt_buf + 1]
        mov ecx, 0
.loop:
        cmp ecx, GDB_REG_COUNT
        jge .done
        call gdb_parse_hex32
        mov [gdb_regs + ecx*4], eax
        inc ecx
        jmp .loop
.done:
        pop ecx
        pop esi
        ret

;=======================================================================
; gdb_send_hex32_reply - Send $XXXXXXXX#xx for value in EAX
;=======================================================================
gdb_send_hex32_reply:
        push edi
        push esi
        mov edi, gdb_reply_buf
        call gdb_hex32_le_to_buf
        mov byte [edi], 0
        mov esi, gdb_reply_buf
        call gdb_send_raw_packet
        pop esi
        pop edi
        ret

;=======================================================================
; gdb_send_mem - Send memory bytes as hex
; gdb_tmp_addr = address, gdb_tmp_len = length
;=======================================================================
gdb_send_mem:
        push eax
        push ecx
        push esi
        push edi

        mov esi, [gdb_tmp_addr]
        mov ecx, [gdb_tmp_len]
        mov edi, gdb_reply_buf
        ; Sanity: cap at 1024 bytes
        cmp ecx, 1024
        jle .ok_len
        mov ecx, 1024
.ok_len:
        test ecx, ecx
        jz  .empty

.byte_loop:
        movzx eax, byte [esi]
        inc esi
        ; High nibble
        push eax
        shr al, 4
        call gdb_nib2hex
        mov [edi], al
        inc edi
        pop eax
        and al, 0x0F
        call gdb_nib2hex
        mov [edi], al
        inc edi
        loop .byte_loop

.empty:
        mov byte [edi], 0
        mov esi, gdb_reply_buf
        call gdb_send_raw_packet

        pop edi
        pop esi
        pop ecx
        pop eax
        ret

;=======================================================================
; gdb_write_mem - Write hex bytes from packet buffer to memory
; ESI = pointer to hex string in packet, gdb_tmp_addr/len set
;=======================================================================
gdb_write_mem:
        push eax
        push ecx
        push edi

        mov edi, [gdb_tmp_addr]
        mov ecx, [gdb_tmp_len]
.loop:
        test ecx, ecx
        jz  .done
        call gdb_parse_hex_byte  ; AL = one byte from ESI
        mov [edi], al
        inc edi
        dec ecx
        jmp .loop
.done:
        pop edi
        pop ecx
        pop eax
        ret

;=======================================================================
; gdb_insert_bp - Write 0xCC at address EAX, save original byte
;=======================================================================
gdb_insert_bp:
        push eax
        push ebx
        push ecx

        ; Find free slot in bp table
        mov ecx, 0
.find:
        cmp ecx, GDB_MAX_BP
        jge .full
        movzx ebx, byte [gdb_bp_table + ecx*8 + 4]  ; valid flag
        test bl, bl
        jz  .found
        inc ecx
        jmp .find
.full:
        pop ecx
        pop ebx
        pop eax
        ret
.found:
        ; Store address, original byte, mark valid
        mov [gdb_bp_table + ecx*8 + 0], eax        ; address
        movzx ebx, byte [eax]
        mov [gdb_bp_table + ecx*8 + 3], bl          ; original byte
        mov byte [gdb_bp_table + ecx*8 + 4], 1      ; valid
        ; Write INT 3 opcode
        mov byte [eax], 0xCC

        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; gdb_remove_bp - Restore original byte at address EAX
;=======================================================================
gdb_remove_bp:
        push eax
        push ebx
        push ecx

        mov ecx, 0
.find:
        cmp ecx, GDB_MAX_BP
        jge .not_found
        mov ebx, [gdb_bp_table + ecx*8 + 0]
        cmp ebx, eax
        je  .found
        inc ecx
        jmp .find
.found:
        ; Restore original byte
        movzx ebx, byte [gdb_bp_table + ecx*8 + 3]
        mov [eax], bl
        ; Clear valid flag
        mov byte [gdb_bp_table + ecx*8 + 4], 0
.not_found:
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; RSP packet I/O
;=======================================================================

; gdb_send_raw_packet
;   ESI = null-terminated payload string
;   Sends: $<payload>#<2-digit checksum>
;   Then waits for '+' ACK (ignores '-' for simplicity)
gdb_send_raw_packet:
        push eax
        push esi
        push ecx

        ; Compute checksum
        push esi
        xor ecx, ecx            ; checksum accumulator
.cksum:
        lodsb
        or  al, al
        jz  .cksum_done
        add cl, al
        jmp .cksum
.cksum_done:
        pop esi

        ; Send '$'
        mov al, '$'
        call serial_putchar

        ; Send payload
.send_payload:
        lodsb
        or  al, al
        jz  .send_hash
        call serial_putchar
        jmp .send_payload

.send_hash:
        ; Send '#'
        mov al, '#'
        call serial_putchar

        ; Send checksum as 2 hex digits
        mov al, cl
        shr al, 4
        call gdb_nib2hex
        call serial_putchar
        mov al, cl
        and al, 0x0F
        call gdb_nib2hex
        call serial_putchar

        ; Wait for ACK ('+')
        mov ecx, 0x400000
.wait_ack:
        call serial_getchar
        cmp al, '+'
        je  .acked
        cmp al, 0xFF
        jne .got_nak            ; got a '-' or something
        loop .wait_ack
        jmp .acked              ; timeout = treat as acked
.got_nak:
        ; On '-', resend (not implemented — just continue)
.acked:
        pop ecx
        pop esi
        pop eax
        ret

; gdb_recv_packet
;   Receives: $<payload>#<2-digit checksum>
;   Validates checksum; sends '+' on success, '-' on fail.
;   Stores payload (null-terminated) in gdb_pkt_buf.
;   Returns EAX=0 success, EAX=1 timeout/error.
gdb_recv_packet:
        push esi
        push ecx

        ; Wait for '$'
        mov ecx, 0x4000000      ; ~several seconds at serial rate
.wait_dollar:
        call serial_getchar
        cmp al, 0xFF
        je  .tick_wait
        cmp al, '$'
        je  .got_dollar
        jmp .wait_dollar
.tick_wait:
        loop .wait_dollar
        ; Timeout
        mov eax, 1
        pop ecx
        pop esi
        ret

.got_dollar:
        ; Read until '#'
        mov esi, gdb_pkt_buf
        xor ecx, ecx            ; checksum
.read_payload:
        call gdb_getchar_wait   ; blocking read
        cmp al, '#'
        je  .got_hash
        mov [esi], al
        add cl, al
        inc esi
        cmp esi, gdb_pkt_buf + GDB_PKT_MAX - 1
        jl  .read_payload
        ; Overflow — skip to hash
.skip_to_hash:
        call gdb_getchar_wait
        cmp al, '#'
        jne .skip_to_hash

.got_hash:
        mov byte [esi], 0       ; null-terminate packet

        ; Read 2 hex digit checksum from stream
        call gdb_getchar_wait
        call gdb_hex_to_nib
        shl al, 4
        mov ah, al
        call gdb_getchar_wait
        call gdb_hex_to_nib
        or  al, ah              ; al = received checksum

        ; Validate
        cmp al, cl
        jne .bad_cksum

        ; Send '+'
        mov al, '+'
        call serial_putchar
        xor eax, eax
        pop ecx
        pop esi
        ret

.bad_cksum:
        mov al, '-'
        call serial_putchar
        mov eax, 1
        pop ecx
        pop esi
        ret

; gdb_getchar_wait - blocking COM1 read (spins until char available)
gdb_getchar_wait:
        push edx
.spin:
        mov dx, COM1_LSR
        in  al, dx
        test al, 0x01
        jz  .spin
        mov dx, COM1_PORT
        in  al, dx
        pop edx
        ret

; gdb_parse_hex - Parse hex string at [ESI] -> EAX; ESI advances past digits
gdb_parse_hex:
        push ecx
        xor eax, eax
.loop:
        movzx ecx, byte [esi]
        call gdb_hex_to_nib_ecx
        cmp ecx, 0xFF
        je  .done
        shl eax, 4
        or  eax, ecx
        inc esi
        jmp .loop
.done:
        pop ecx
        ret

; gdb_parse_hex32 - Parse exactly 8 hex chars -> EAX; ESI advances 8
gdb_parse_hex32:
        push ecx
        push edx
        xor eax, eax
        mov edx, 8
.loop:
        movzx ecx, byte [esi]
        call gdb_hex_to_nib_ecx
        shl eax, 4
        or  eax, ecx
        inc esi
        dec edx
        jnz .loop
        pop edx
        pop ecx
        ret

; gdb_parse_hex_byte - Parse 2 hex chars from [ESI] -> AL; ESI += 2
gdb_parse_hex_byte:
        push ecx
        movzx ecx, byte [esi]
        inc esi
        call gdb_hex_to_nib_ecx
        shl cl, 4
        push ecx
        movzx ecx, byte [esi]
        inc esi
        call gdb_hex_to_nib_ecx
        pop eax
        or  al, cl
        pop ecx
        ret

; gdb_hex_to_nib  AL = ASCII hex char -> AL = nibble (0-15); FF if invalid
gdb_hex_to_nib:
        cmp al, '0'
        jl  .bad
        cmp al, '9'
        jle .digit
        cmp al, 'a'
        jl  .try_upper
        cmp al, 'f'
        jg  .bad
        sub al, 'a' - 10
        ret
.try_upper:
        cmp al, 'A'
        jl  .bad
        cmp al, 'F'
        jg  .bad
        sub al, 'A' - 10
        ret
.digit: sub al, '0'
        ret
.bad:   mov al, 0xFF
        ret

; gdb_hex_to_nib_ecx  ECX = ASCII char -> ECX = nibble; ECX=0 if bad
gdb_hex_to_nib_ecx:
        push eax
        mov al, cl
        call gdb_hex_to_nib
        movzx ecx, al
        cmp al, 0xFF
        jne .ok
        xor ecx, ecx
.ok:    pop eax
        ret

;-----------------------------------------------------------------------
; BSS data
;-----------------------------------------------------------------------
GDB_PKT_MAX     equ 1024

gdb_regs:       times GDB_REG_COUNT * 4 db 0   ; Register snapshot
gdb_reply_buf:  times GDB_PKT_MAX db 0
gdb_pkt_buf:    times GDB_PKT_MAX db 0
gdb_tmp_addr:   dd 0
gdb_tmp_len:    dd 0

; Breakpoint table: 8 bytes per entry = addr(4) + pad(3) + orig(1) + valid(1) + pad(3)
; Actually use: addr(4) pad(3-byte spacer) orig_byte(1) valid(1) padding(3)
gdb_bp_table:   times GDB_MAX_BP * 8 db 0
