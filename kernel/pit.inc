;=======================================================================
; PIT (Programmable Interval Timer) - 100 Hz
;=======================================================================

pit_init:
        ; Channel 0, lobyte/hibyte, rate generator
        mov al, 0x36
        out PIT_CMD, al
        mov ax, PIT_DIVISOR
        out PIT_CH0, al
        mov al, ah
        out PIT_CH0, al
        ret

;=======================================================================
; KEYBOARD DRIVER
;=======================================================================

kb_init:
        mov dword [kb_read_idx], 0
        mov dword [kb_write_idx], 0
        mov byte [kb_shift], 0
        mov byte [kb_ctrl], 0
        mov byte [kb_alt], 0
        ret

; Read a key from buffer (blocking)
; Returns: AL = ASCII character
kb_getchar:
        cmp byte [stdin_redir_active], 1
        jne .wait
        mov eax, [stdin_redir_pos]
        cmp eax, [stdin_redir_len]
        jb .redir_have
        mov byte [stdin_redir_active], 0
        xor eax, eax
        ret
.redir_have:
        mov bl, [redir_in_buf + eax]
        inc eax
        mov [stdin_redir_pos], eax
        mov al, bl
        ret
.wait:
        ; Serial console input. Tests and remote terminals drive the shell
        ; through COM1; the PS/2 ring buffer is only reachable from the local
        ; keyboard. Polled here (the UART interrupt is disabled) so a serial
        ; byte is picked up on the next loop pass, at worst one PIT tick later.
        call serial_getchar
        cmp al, 0xFF
        jne .have_serial

        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        jne .have_key
        sti                     ; Ensure interrupts are enabled
        hlt                     ; Sleep until next interrupt
        jmp .wait

.have_serial:
        ; Echo back to the serial console so a remote terminal sees what it
        ; typed. The shell echoes to VGA on its own, so this only touches COM1.
        cmp al, 0x0D
        je .serial_cr
        cmp al, 0x08
        je .serial_bs
        cmp al, 0x20
        jb .serial_done
        cmp al, 0x7E
        ja .serial_done
        push eax
        call serial_putchar
        pop eax
        jmp .serial_done
.serial_cr:
        push eax
        mov al, 0x0D
        call serial_putchar
        mov al, 0x0A
        call serial_putchar
        pop eax
        jmp .serial_done
.serial_bs:
        push eax
        mov al, 0x08
        call serial_putchar
        mov al, ' '
        call serial_putchar
        mov al, 0x08
        call serial_putchar
        pop eax
.serial_done:
        movzx eax, al
        ret

.have_key:

        mov ebx, eax
        mov al, [kb_buffer + ebx]
        inc ebx
        and ebx, KB_BUFFER_SIZE - 1
        mov [kb_read_idx], ebx
        ret

; Non-blocking key check
; Returns: AL = key (0 if none), ZF set if no key
kb_pollchar:
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        jne .have

        ; No PS/2 key; try the serial console.
        call serial_getchar
        cmp al, 0xFF
        je .none
        movzx eax, al
        or al, al               ; Clear ZF
        ret

.have:
        mov ebx, eax
        mov al, [kb_buffer + ebx]
        inc ebx
        and ebx, KB_BUFFER_SIZE - 1
        mov [kb_read_idx], ebx
        or al, al               ; Clear ZF
        ret

.none:
        xor eax, eax            ; AL=0, ZF=1
        ret

;---------------------------------------
; kb_peekchar - Return the next PS/2 key without consuming it
;   Out: AL = key (0 if the buffer is empty); ZF set when empty
;---------------------------------------
kb_peekchar:
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        je .pk_none
        movzx eax, byte [kb_buffer + eax]
        or al, al
        ret
.pk_none:
        xor eax, eax
        ret
