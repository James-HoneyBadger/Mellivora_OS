;=======================================================================
; VGA TEXT MODE DRIVER
;=======================================================================

vga_init:
        ; Set default color and clear screen
        mov byte [vga_color], COLOR_DEFAULT
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        call vga_clear
        call vga_update_cursor
        ret

; Clear screen (always uses default color)
vga_clear:
        mov edi, VGA_BASE
        mov byte [vga_color], COLOR_DEFAULT
        mov ah, COLOR_DEFAULT
        mov al, ' '
        mov ecx, VGA_WIDTH * VGA_HEIGHT
        cld
        rep stosw
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        call vga_update_cursor
        ret

; Print single character in AL
vga_putchar:
        PUSHALL

        cli                             ; Protect redirection state from interrupts
        cmp byte [stdout_redir_active], 1
        jne .vp_normal

        cmp al, 0x08            ; Backspace shrinks captured output
        jne .vp_store
        cmp dword [stdout_redir_len], 0
        je .vp_redir_done
        dec dword [stdout_redir_len]
        jmp .vp_redir_done

.vp_store:
        cmp al, 0x0D            ; Ignore CR in redirected output
        je .vp_redir_done
        mov edx, [stdout_redir_len]
        cmp edx, BATCH_BUFFER_SIZE - 1
        jge .vp_redir_done
        mov [redir_out_buf + edx], al
        inc dword [stdout_redir_len]

.vp_redir_done:
        sti
        POPALL
        ret

.vp_normal:
        sti

        cmp al, 0x0A            ; Line feed?
        je .newline
        cmp al, 0x0D            ; Carriage return?
        je .cr
        cmp al, 0x08            ; Backspace?
        je .backspace
        cmp al, 0x09            ; Tab?
        je .tab

        ; Regular character
        mov edi, VGA_BASE
        mov ecx, [vga_cursor_y]
        imul ecx, VGA_WIDTH * 2
        mov edx, [vga_cursor_x]
        shl edx, 1
        add edi, ecx
        add edi, edx

        mov ah, [vga_color]
        mov [edi], ax

        inc dword [vga_cursor_x]
        cmp dword [vga_cursor_x], VGA_WIDTH
        jl .done
        ; Wrap to next line
        mov dword [vga_cursor_x], 0
        inc dword [vga_cursor_y]
        jmp .check_scroll

.newline:
        mov dword [vga_cursor_x], 0
        inc dword [vga_cursor_y]
        jmp .check_scroll

.cr:
        mov dword [vga_cursor_x], 0
        jmp .done

.backspace:
        cmp dword [vga_cursor_x], 0
        je .bs_prevline
        dec dword [vga_cursor_x]
        ; Erase character at cursor
        mov edi, VGA_BASE
        mov ecx, [vga_cursor_y]
        imul ecx, VGA_WIDTH * 2
        mov edx, [vga_cursor_x]
        shl edx, 1
        add edi, ecx
        add edi, edx
        mov byte [edi], ' '
        mov al, [vga_color]
        mov [edi+1], al
        jmp .done

.bs_prevline:
        cmp dword [vga_cursor_y], 0
        je .done
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
        jmp .done

.tab:
        ; Advance to next 8-column boundary
        mov eax, [vga_cursor_x]
        add eax, 8
        and eax, ~7
        cmp eax, VGA_WIDTH
        jl .tab_ok
        mov eax, 0
        inc dword [vga_cursor_y]
.tab_ok:
        mov [vga_cursor_x], eax
        jmp .check_scroll

.check_scroll:
        cmp dword [vga_cursor_y], VGA_HEIGHT
        jl .done
        call vga_scroll
        mov dword [vga_cursor_y], VGA_HEIGHT - 1

.done:
        call vga_update_cursor
        POPALL
        ret

; Scroll screen up one line
vga_scroll:
        PUSHALL
        ; Copy lines 1..24 to lines 0..23
        mov esi, VGA_BASE + VGA_WIDTH * 2
        mov edi, VGA_BASE
        mov ecx, VGA_WIDTH * (VGA_HEIGHT - 1) * 2 / 4
        rep movsd

        ; Clear last line (always use default color)
        mov edi, VGA_BASE + VGA_WIDTH * (VGA_HEIGHT - 1) * 2
        mov ah, COLOR_DEFAULT
        mov al, ' '
        mov ecx, VGA_WIDTH
        rep stosw
        POPALL
        ret

; Update hardware cursor position
vga_update_cursor:
        PUSHALL
        mov eax, [vga_cursor_y]
        imul eax, VGA_WIDTH
        add eax, [vga_cursor_x]
        mov ecx, eax

        mov dx, 0x3D4
        mov al, 0x0F            ; Cursor low register
        out dx, al
        mov dx, 0x3D5
        mov al, cl
        out dx, al

        mov dx, 0x3D4
        mov al, 0x0E            ; Cursor high register
        out dx, al
        mov dx, 0x3D5
        mov al, ch
        out dx, al
        POPALL
        ret

; Print a newline character (0x0A)
vga_newline:
        push rax
        mov al, 0x0A
        call vga_putchar
        pop rax
        ret

; Print null-terminated string at ESI
vga_print:
        PUSHALL
.loop:
        lodsb
        or al, al
        jz .done
        call vga_putchar
        jmp .loop
.done:
        POPALL
        ret

; Print string at ESI with color prefix byte
vga_print_color:
        lodsb                   ; First byte = color
        mov [vga_color], al
        call vga_print
        ret

; Print 32-bit hex value in EAX
vga_print_hex:
        PUSHALL
        mov ecx, 8              ; 8 hex digits
        mov ebx, eax
.loop:
        rol ebx, 4
        mov al, bl
        and al, 0x0F
        cmp al, 10
        jl .digit
        add al, 'A' - 10
        jmp .print
.digit:
        add al, '0'
.print:
        call vga_putchar
        loop .loop
        POPALL
        ret

; Print 32-bit decimal value in EAX
vga_print_dec:
        PUSHALL
        mov ecx, 0              ; Digit counter
        mov ebx, 10

        test eax, eax
        jnz .nonzero
        mov al, '0'
        call vga_putchar
        jmp .done

.nonzero:
.push_digits:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .push_digits

.pop_digits:
        pop rax
        add al, '0'
        call vga_putchar
        loop .pop_digits

.done:
        POPALL
        ret

; Print 32-bit decimal value in EAX, right-aligned in field of EDX chars
; EAX = number, EDX = field width (0 = no padding)
vga_print_dec_width:
        PUSHALL
        mov [.vpdw_width], edx
        ; First count digits
        mov ecx, 0
        mov ebx, 10
        mov [.vpdw_val], eax
        test eax, eax
        jnz .vpdw_count
        mov ecx, 1              ; '0' is 1 digit
        jmp .vpdw_pad
.vpdw_count:
        xor edx, edx
        div ebx
        inc ecx
        test eax, eax
        jnz .vpdw_count
.vpdw_pad:
        ; Print (width - digits) leading spaces
        mov eax, [.vpdw_width]
        sub eax, ecx
        jle .vpdw_print
        mov edx, eax
.vpdw_spc:
        mov al, ' '
        call vga_putchar
        dec edx
        jnz .vpdw_spc
.vpdw_print:
        ; Now print the actual number
        mov eax, [.vpdw_val]
        call vga_print_dec
        POPALL
        ret
.vpdw_val:   dd 0
.vpdw_width: dd 0

; Set cursor position: EAX=x, EDX=y
vga_set_cursor:
        mov [vga_cursor_x], eax
        mov [vga_cursor_y], edx
        call vga_update_cursor
        ret
