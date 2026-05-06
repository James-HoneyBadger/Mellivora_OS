;=======================================================================
; VGA TEXT MODE DRIVER
;=======================================================================

vga_init:
        ; Set default color and clear screen
        call vga_reset_color
        mov byte [default_color], COLOR_DEFAULT
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        call vga_clear
        call vga_update_cursor
        ret

; Restore vga_color to user-selected default (preserves all regs)
vga_reset_color:
        push eax
        mov al, [default_color]
        mov [vga_color], al
        pop eax
        ret

; Clear screen (always uses default color)
vga_clear:
        push eax
        mov edi, VGA_BASE
        mov al, [default_color]
        mov [vga_color], al
        mov ah, al
        mov al, ' '
        mov ecx, VGA_WIDTH * VGA_HEIGHT
        cld
        rep stosw
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        ; Reset scrollback to live view and snapshot
        mov dword [scroll_view], 0
        call vga_save_live
        call vga_update_cursor
        pop eax
        ret

; Print single character in AL
vga_putchar:
        pushad
        pushf                           ; Save caller's interrupt flag state

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
        popf                            ; Restore caller's interrupt flag
        popad
        ret

.vp_normal:
        popf                            ; Restore caller's interrupt flag (only sti if caller had IF=1)

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
        popad
        ret

; Scroll screen up one line
vga_scroll:
        pushad

        ; Snapshot current live screen (before scroll) so scrollback render
        ; can show the live rows.  Only when we are in live view.
        cmp dword [scroll_view], 0
        jne .sb_skip_snap
        call vga_save_live
.sb_skip_snap:

        ; ---- Save line 0 to scrollback ring buffer ----
        ; dest = scroll_buf + scroll_head * VGA_WIDTH*2
        mov eax, [scroll_head]
        imul eax, VGA_WIDTH * 2
        lea edi, [scroll_buf + eax]
        mov esi, VGA_BASE
        mov ecx, VGA_WIDTH * 2 / 4
        rep movsd

        ; Advance head
        mov eax, [scroll_head]
        inc eax
        cmp eax, SCROLL_LINES
        jl .sb_no_wrap
        xor eax, eax
.sb_no_wrap:
        mov [scroll_head], eax

        ; Increment count (capped)
        mov eax, [scroll_count]
        cmp eax, SCROLL_LINES
        jge .sb_capped
        inc eax
        mov [scroll_count], eax
.sb_capped:

        ; ---- Copy lines 1..24 to lines 0..23 ----
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
        popad
        ret

; vga_do_scroll_back — scroll back one screen (25 lines)
; Called from IRQ keyboard on PgUp when not in a program
vga_do_scroll_back:
        pushad
        mov eax, [scroll_view]
        add eax, VGA_HEIGHT
        ; Cap at scroll_count
        mov ebx, [scroll_count]
        cmp eax, ebx
        jle .sb_ok
        mov eax, ebx
.sb_ok:
        mov [scroll_view], eax
        call vga_render_scrollback
        popad
        ret

; vga_do_scroll_fwd — scroll forward one screen (25 lines)
vga_do_scroll_fwd:
        pushad
        mov eax, [scroll_view]
        sub eax, VGA_HEIGHT
        cmp eax, 0
        jge .sf_ok
        xor eax, eax
.sf_ok:
        mov [scroll_view], eax
        cmp eax, 0
        je .restore_live
        call vga_render_scrollback
        jmp .sf_done
.restore_live:
        call vga_restore_live
.sf_done:
        popad
        ret

; vga_save_live — copy current VGA framebuffer to live_screen
vga_save_live:
        push esi
        push edi
        push ecx
        mov esi, VGA_BASE
        mov edi, live_screen
        mov ecx, VGA_SIZE / 4
        rep movsd
        pop ecx
        pop edi
        pop esi
        ret

; vga_restore_live — restore live_screen to VGA framebuffer
vga_restore_live:
        push esi
        push edi
        push ecx
        mov esi, live_screen
        mov edi, VGA_BASE
        mov ecx, VGA_SIZE / 4
        rep movsd
        pop ecx
        pop edi
        pop esi
        ret

; vga_render_scrollback — render scrollback buffer given scroll_view
; scroll_view = V means we show history lines starting V lines back
vga_render_scrollback:
        pushad
        mov esi, [scroll_view]      ; V (save in esi initially; then reuse)
        mov [rsb_v], esi
        xor ebx, ebx               ; screen row r
.rsb_row:
        cmp ebx, VGA_HEIGHT
        jge .rsb_done

        mov edi, VGA_BASE
        mov eax, ebx
        imul eax, VGA_WIDTH * 2
        add edi, eax               ; dest = VGA row r

        mov esi, [rsb_v]
        cmp ebx, esi               ; r < V ?
        jge .rsb_from_live

        ; Source from ring buffer
        mov eax, [scroll_head]
        sub eax, esi               ; scroll_head - V
        add eax, ebx               ; + r
        ; Normalise modulo SCROLL_LINES
.rsb_mod:
        cmp eax, 0
        jge .rsb_mod_pos
        add eax, SCROLL_LINES
        jmp .rsb_mod
.rsb_mod_pos:
        cmp eax, SCROLL_LINES
        jl .rsb_mod_ok
        sub eax, SCROLL_LINES
        jmp .rsb_mod_pos
.rsb_mod_ok:
        imul eax, VGA_WIDTH * 2
        mov esi, scroll_buf
        add esi, eax
        mov ecx, VGA_WIDTH * 2 / 4
        rep movsd
        jmp .rsb_next

.rsb_from_live:
        ; Source from live_screen at row (r - V)
        mov eax, ebx
        mov esi, [rsb_v]
        sub eax, esi
        imul eax, VGA_WIDTH * 2
        mov esi, live_screen
        add esi, eax
        mov ecx, VGA_WIDTH * 2 / 4
        rep movsd

.rsb_next:
        inc ebx
        jmp .rsb_row
.rsb_done:
        popad
        ret

; Update hardware cursor position
vga_update_cursor:
        pushad
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
        popad
        ret

; Print a newline character (0x0A)
vga_newline:
        push eax
        mov al, 0x0A
        call vga_putchar
        pop eax
        ret

; Print null-terminated string at ESI
vga_print:
        pushad
.loop:
        lodsb
        or al, al
        jz .done
        call vga_putchar
        jmp .loop
.done:
        popad
        ret

; Print string at ESI with color prefix byte
vga_print_color:
        lodsb                   ; First byte = color
        mov [vga_color], al
        call vga_print
        ret

; Print 32-bit hex value in EAX
vga_print_hex:
        pushad
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
        popad
        ret

; Print 32-bit decimal value in EAX
vga_print_dec:
        pushad
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
        push edx
        inc ecx
        test eax, eax
        jnz .push_digits

.pop_digits:
        pop eax
        add al, '0'
        call vga_putchar
        loop .pop_digits

.done:
        popad
        ret

; Print 32-bit decimal value in EAX, right-aligned in field of EDX chars
; EAX = number, EDX = field width (0 = no padding)
vga_print_dec_width:
        pushad
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
        popad
        ret
.vpdw_val:   dd 0
.vpdw_width: dd 0

; Set cursor position: EAX=x, EDX=y
vga_set_cursor:
        mov [vga_cursor_x], eax
        mov [vga_cursor_y], edx
        call vga_update_cursor
        ret
