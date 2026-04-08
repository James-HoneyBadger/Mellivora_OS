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
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        jne .have_key
        sti                     ; Ensure interrupts are enabled
        hlt                     ; Sleep until next interrupt
        jmp .wait
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
        je .none

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
