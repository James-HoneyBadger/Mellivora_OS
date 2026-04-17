;=======================================================================
; mouse.inc - PS/2 Mouse Driver
;
; Initialises the PS/2 auxiliary port (mouse) via the 8042 controller,
; enables IRQ12 and installs an interrupt handler that tracks:
;   mouse_x, mouse_y  – cursor position (0-based, clamped to screen)
;   mouse_buttons      – bit 0 = left, bit 1 = right, bit 2 = middle
;
; The driver collects 3-byte packets (flags, delta-X, delta-Y) from
; the PS/2 aux port and updates position accordingly.
;
; A new `mouse` shell command is registered to show mouse status.
; A new syscall SYS_MOUSE (36) lets programs read mouse state.
;=======================================================================

MOUSE_PKT_SIZE  equ 3          ; Standard PS/2 mouse packet is 3 bytes

;---------------------------------------
; mouse_init - Enable PS/2 mouse
;---------------------------------------
mouse_init:
        PUSHALL

        ; Wait for 8042 input buffer to be clear
        call mouse_wait_write

        ; Enable auxiliary device (mouse) on the 8042 controller
        mov al, 0xA8            ; Command: enable aux port
        out 0x64, al
        call mouse_wait_write

        ; Enable IRQ12 in the 8042 command byte
        mov al, 0x20            ; Command: read command byte
        out 0x64, al
        call mouse_wait_read
        in al, 0x60
        or al, 0x02             ; Set bit 1: enable IRQ12 (aux interrupt)
        push rax
        call mouse_wait_write
        mov al, 0x60            ; Command: write command byte
        out 0x64, al
        call mouse_wait_write
        pop rax
        out 0x60, al

        ; Send "set defaults" to mouse
        mov al, 0xF6
        call mouse_write_byte
        call mouse_read_ack

        ; Send "enable data reporting" to mouse
        mov al, 0xF4
        call mouse_write_byte
        call mouse_read_ack

        ; Reset packet state
        mov dword [mouse_pkt_idx], 0
        mov dword [mouse_x], 40         ; Center of screen
        mov dword [mouse_y], 12         ; Center of screen
        mov byte [mouse_buttons], 0
        mov byte [mouse_btn_latch], 0
        mov byte [mouse_present], 1

        ; Install IRQ12 handler (INT 0x2C = IRQ12 on PIC2)
        mov ecx, 0x2C
        mov eax, irq_mouse
        call idt_set_gate

        ; Unmask IRQ12 on PIC2 (bit 4 of PIC2 data port)
        in al, PIC2_DATA
        and al, ~(1 << 4)       ; Clear bit 4 to unmask IRQ12
        out PIC2_DATA, al

        ; Unmask IRQ2 on PIC1 (cascade from PIC2) so PIC2 interrupts reach the CPU
        in al, PIC1_DATA
        and al, ~(1 << 2)       ; Clear bit 2 to unmask cascade
        out PIC1_DATA, al

        POPALL
        ret

;---------------------------------------
; irq_mouse - IRQ12 handler
; Collects 3-byte PS/2 packets and
; updates mouse_x, mouse_y, mouse_buttons
;---------------------------------------
irq_mouse:
        PUSHALL

        ; Read the byte from port 0x60
        in al, 0x60
        movzx eax, al

        ; If collecting byte 0 (flags), validate bit3 immediately
        ; to resync quickly after packet loss
        mov edx, [mouse_pkt_idx]
        test edx, edx
        jnz .mouse_store
        test al, 0x08           ; Bit 3 must always be set in flags byte
        jz .mouse_eoi           ; Not a valid flags byte — discard

.mouse_store:
        mov [mouse_packet + edx], al
        inc edx

        cmp edx, MOUSE_PKT_SIZE
        jb .mouse_partial

        ; Full 3-byte packet received — process it
        mov dword [mouse_pkt_idx], 0

        ; Byte 0: flags (bits 0-2 = buttons, bit 4 = X sign, bit 5 = Y sign)
        movzx ecx, byte [mouse_packet]

        ; Check for overflow bits (bit 6, bit 7) — discard bad packets
        test ecx, 0xC0
        jnz .mouse_eoi

        ; Update buttons and latch new presses
        mov al, cl
        and al, 0x07
        ; Detect 0->1 transitions: new = (cur ^ prev) & cur
        mov dl, al
        xor dl, [mouse_buttons]
        and dl, al
        or [mouse_btn_latch], dl   ; latch new presses
        mov [mouse_buttons], al

        ; Delta X (signed, clamped to ±mouse_clamp)
        movzx eax, byte [mouse_packet + 1]
        test ecx, 0x10          ; X sign bit
        jz .mouse_x_pos
        or eax, 0xFFFFFF00      ; Sign-extend to negative
.mouse_x_pos:
        ; Clamp delta to [-clamp, +clamp]
        mov edx, [mouse_clamp]
        cmp eax, edx
        jle .mouse_x_nhi
        mov eax, edx
.mouse_x_nhi:
        neg edx
        cmp eax, edx
        jge .mouse_x_nlo
        mov eax, edx
.mouse_x_nlo:
        ; Apply to position (clamped to 0..mouse_max_x)
        add [mouse_x], eax
        cmp dword [mouse_x], 0
        jge .mouse_x_lo_ok
        mov dword [mouse_x], 0
.mouse_x_lo_ok:
        mov edx, [mouse_max_x]
        cmp [mouse_x], edx
        jle .mouse_x_hi_ok
        mov [mouse_x], edx
.mouse_x_hi_ok:

        ; Delta Y (inverted: PS/2 Y axis is up-positive, screen is down-positive)
        movzx eax, byte [mouse_packet + 2]
        test ecx, 0x20          ; Y sign bit
        jz .mouse_y_pos
        or eax, 0xFFFFFF00
.mouse_y_pos:
        neg eax
        ; Clamp delta to [-clamp, +clamp]
        mov edx, [mouse_clamp]
        cmp eax, edx
        jle .mouse_y_nhi
        mov eax, edx
.mouse_y_nhi:
        neg edx
        cmp eax, edx
        jge .mouse_y_nlo
        mov eax, edx
.mouse_y_nlo:
        add [mouse_y], eax
        cmp dword [mouse_y], 0
        jge .mouse_y_lo_ok
        mov dword [mouse_y], 0
.mouse_y_lo_ok:
        mov edx, [mouse_max_y]
        cmp [mouse_y], edx
        jle .mouse_y_hi_ok
        mov [mouse_y], edx
.mouse_y_hi_ok:
        jmp .mouse_eoi

.mouse_partial:
        mov [mouse_pkt_idx], edx

.mouse_eoi:
        ; Send EOI to PIC2 and PIC1
        mov al, 0x20
        out PIC2_CMD, al
        out PIC1_CMD, al

        POPALL
        iretq

;---------------------------------------
; mouse_wait_write - Wait for 8042 input buffer clear
;---------------------------------------
mouse_wait_write:
        push rax
        push rcx
        mov ecx, 100000
.mww:
        in al, 0x64
        test al, 0x02           ; Input buffer full?
        jz .mww_ok
        dec ecx
        jnz .mww
.mww_ok:
        pop rcx
        pop rax
        ret

;---------------------------------------
; mouse_wait_read - Wait for 8042 output buffer full
;---------------------------------------
mouse_wait_read:
        push rax
        push rcx
        mov ecx, 100000
.mwr:
        in al, 0x64
        test al, 0x01           ; Output buffer full?
        jnz .mwr_ok
        dec ecx
        jnz .mwr
.mwr_ok:
        pop rcx
        pop rax
        ret

;---------------------------------------
; mouse_write_byte - Send byte to mouse (via aux port)
; AL = byte to send
;---------------------------------------
mouse_write_byte:
        push rax
        push rax               ; Save data byte
        call mouse_wait_write
        mov al, 0xD4            ; Command: write to aux device
        out 0x64, al
        call mouse_wait_write
        pop rax                 ; Restore data byte
        out 0x60, al
        pop rax
        ret

;---------------------------------------
; mouse_read_ack - Wait for and discard ACK (0xFA)
;---------------------------------------
mouse_read_ack:
        push rax
        call mouse_wait_read
        in al, 0x60             ; Read and discard (should be 0xFA)
        pop rax
        ret

;---------------------------------------
; sys_mouse - SYS_MOUSE syscall handler
; Returns:
;   EAX = mouse_x
;   EBX = mouse_y
;   ECX = mouse_buttons
;---------------------------------------
sys_mouse:
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        movzx ecx, byte [mouse_buttons]
        iretq

; BSS data for mouse driver
mouse_present:  db 0
mouse_buttons:  db 0
                db 0, 0         ; padding
mouse_x:        dd 0
mouse_y:        dd 0
mouse_pkt_idx:  dd 0
mouse_packet:   db 0, 0, 0, 0  ; 3-byte packet buffer + padding
mouse_max_x:    dd 79           ; Max X coordinate (79=text, 639=GUI)
mouse_max_y:    dd 24           ; Max Y coordinate (24=text, 479=GUI)
mouse_scale:    dd 1            ; Movement scale (kept for compat, set to 1)
mouse_clamp:    dd 127          ; Max delta per axis per packet (lower = slower)
mouse_btn_latch: db 0           ; Latched new-press bits (cleared by consumer)
                db 0, 0, 0      ; padding
