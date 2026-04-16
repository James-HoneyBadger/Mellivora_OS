;=======================================================================
; vga.inc - Mellivora VGA Text Mode API
;
; Direct VGA text buffer manipulation, cursor control, color management,
; and UI drawing primitives for the 80x25 text mode display.
;
; The VGA text buffer is at 0xB8000, 80 columns x 25 rows.
; Each cell is 2 bytes: [ASCII char][attribute byte]
; Attribute: bits 0-3 = foreground, bits 4-6 = background, bit 7 = blink
;
; Functions:
;   vga_set_cursor    - Set cursor position
;   vga_get_cursor    - Get cursor position (from VGA buffer tracking)
;   vga_set_color     - Set text color attribute
;   vga_clear         - Clear screen
;   vga_clear_region  - Clear a rectangular region
;   vga_put_char_at   - Write char+color at position
;   vga_get_char_at   - Read char+color from position
;   vga_write_at      - Write string at position with current color
;   vga_write_color   - Write string at position with specific color
;   vga_draw_hline    - Draw horizontal line
;   vga_draw_vline    - Draw vertical line
;   vga_draw_box      - Draw a box with border characters
;   vga_draw_filled   - Draw a filled rectangle
;   vga_status_bar    - Draw a status bar at row
;   vga_progress_bar  - Draw a progress bar
;   vga_scroll_region - Scroll a region up by one line
;   vga_make_color    - Create color attribute from fg/bg
;   vga_hide_cursor   - Hide the text cursor
;   vga_show_cursor   - Show the text cursor
;=======================================================================

; Box-drawing characters (code page 437)
BOX_H           equ 0xC4       ; ─
BOX_V           equ 0xB3       ; │
BOX_TL          equ 0xDA       ; ┌
BOX_TR          equ 0xBF       ; ┐
BOX_BL          equ 0xC0       ; └
BOX_BR          equ 0xD9       ; ┘
BOX_DH          equ 0xCD       ; ═
BOX_DV          equ 0xBA       ; ║
BOX_DTL         equ 0xC9       ; ╔
BOX_DTR         equ 0xBB       ; ╗
BOX_DBL         equ 0xC8       ; ╚
BOX_DBR         equ 0xBC       ; ╝
BOX_FULL        equ 0xDB       ; █
BOX_HALF        equ 0xDD       ; ▌
BOX_SHADE_L     equ 0xB0       ; ░
BOX_SHADE_M     equ 0xB1       ; ▒
BOX_SHADE_H     equ 0xB2       ; ▓

; Common color constants
VGA_BLACK       equ 0x00
VGA_BLUE        equ 0x01
VGA_GREEN       equ 0x02
VGA_CYAN        equ 0x03
VGA_RED         equ 0x04
VGA_MAGENTA     equ 0x05
VGA_BROWN       equ 0x06
VGA_LGRAY       equ 0x07
VGA_DGRAY       equ 0x08
VGA_LBLUE       equ 0x09
VGA_LGREEN      equ 0x0A
VGA_LCYAN       equ 0x0B
VGA_LRED        equ 0x0C
VGA_LMAGENTA    equ 0x0D
VGA_YELLOW      equ 0x0E
VGA_WHITE       equ 0x0F

;-----------------------------------------------------------------------
; vga_set_cursor - Set cursor position
; Input:  EBX = column (0-79), ECX = row (0-24)
; Output: none
;-----------------------------------------------------------------------
vga_set_cursor:
        push rax
        mov eax, SYS_SETCURSOR
        int 0x80
        pop rax
        ret

;-----------------------------------------------------------------------
; vga_set_color - Set current text color
; Input:  BL = color attribute
; Output: none
;-----------------------------------------------------------------------
vga_set_color:
        push rax
        push rbx
        movzx ebx, bl
        mov eax, SYS_SETCOLOR
        int 0x80
        pop rbx
        pop rax
        ret

;-----------------------------------------------------------------------
; vga_clear - Clear the screen
;-----------------------------------------------------------------------
vga_clear:
        push rax
        mov eax, SYS_CLEAR
        int 0x80
        pop rax
        ret

;-----------------------------------------------------------------------
; vga_make_color - Create color attribute from foreground and background
; Input:  AL = foreground (0-15), AH = background (0-7)
; Output: AL = combined color attribute
;-----------------------------------------------------------------------
vga_make_color:
        push rcx
        movzx ecx, ah
        shl cl, 4
        and al, 0x0F
        or al, cl
        pop rcx
        ret

;-----------------------------------------------------------------------
; vga_put_char_at - Write a character with color at specific position
; Input:  AL = character, AH = color attribute
;         EBX = column (0-79), ECX = row (0-24)
; Output: none
;-----------------------------------------------------------------------
vga_put_char_at:
        PUSHALL
        ; Calculate VGA offset: (row * 80 + col) * 2
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax           ; write char + attr
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_get_char_at - Read character and color from position
; Input:  EBX = column, ECX = row
; Output: AL = character, AH = color attribute
;-----------------------------------------------------------------------
vga_get_char_at:
        push rcx
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov ax, [ecx]
        pop rcx
        ret

;-----------------------------------------------------------------------
; vga_write_at - Write string at position using current color
; Input:  ESI = string, EBX = column, ECX = row
; Output: none
;-----------------------------------------------------------------------
vga_write_at:
        PUSHALL
        ; Set cursor position
        mov eax, SYS_SETCURSOR
        int 0x80
        ; Print string
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_write_color - Write string at position with specific color
; Input:  ESI = string, EBX = column, ECX = row, DL = color
; Output: none
;-----------------------------------------------------------------------
vga_write_color:
        PUSHALL
        ; Set color
        push rbx
        push rcx
        movzx ebx, dl
        mov eax, SYS_SETCOLOR
        int 0x80
        pop rcx
        pop rbx
        ; Set cursor
        mov eax, SYS_SETCURSOR
        int 0x80
        ; Print
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_draw_hline - Draw a horizontal line
; Input:  EBX = start col, ECX = row, EDX = length, AL = char, AH = color
; Output: none
;-----------------------------------------------------------------------
vga_draw_hline:
        PUSHALL
        mov esi, edx            ; length counter
        ; Calc VGA offset
        push rax
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        pop rax
.vdhl_loop:
        cmp esi, 0
        jle .vdhl_done
        mov [ecx], ax
        add ecx, 2
        dec esi
        jmp .vdhl_loop
.vdhl_done:
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_draw_vline - Draw a vertical line
; Input:  EBX = col, ECX = start row, EDX = length, AL = char, AH = color
; Output: none
;-----------------------------------------------------------------------
vga_draw_vline:
        PUSHALL
        mov esi, edx            ; length
        push rax
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        pop rax
.vdvl_loop:
        cmp esi, 0
        jle .vdvl_done
        mov [ecx], ax
        add ecx, VGA_WIDTH * 2
        dec esi
        jmp .vdvl_loop
.vdvl_done:
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_draw_box - Draw a single-line box
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
;         AH = color attribute
; Output: none
;-----------------------------------------------------------------------
vga_draw_box:
        PUSHALL
        mov edi, esi            ; height
        mov ebp, edx            ; width

        ; Top-left corner
        mov al, BOX_TL
        call vga_put_char_at
        ; Top-right corner
        push rbx
        add ebx, ebp
        dec ebx
        mov al, BOX_TR
        call vga_put_char_at
        pop rbx
        ; Top horizontal line
        push rbx
        inc ebx
        sub ebp, 2             ; inner width
        push rdx
        mov edx, ebp
        mov al, BOX_H
        call vga_draw_hline
        pop rdx
        pop rbx
        add ebp, 2             ; restore width

        ; Bottom corners and line
        push rcx
        add ecx, edi
        dec ecx
        mov al, BOX_BL
        call vga_put_char_at
        push rbx
        add ebx, ebp
        dec ebx
        mov al, BOX_BR
        call vga_put_char_at
        pop rbx
        push rbx
        inc ebx
        push rdx
        mov edx, ebp
        sub edx, 2
        mov al, BOX_H
        call vga_draw_hline
        pop rdx
        pop rbx
        pop rcx

        ; Left and right vertical lines
        push rcx
        inc ecx
        push rdx
        mov edx, edi
        sub edx, 2
        mov al, BOX_V
        call vga_draw_vline
        push rbx
        add ebx, ebp
        dec ebx
        call vga_draw_vline
        pop rbx
        pop rdx
        pop rcx

        POPALL
        ret

;-----------------------------------------------------------------------
; vga_draw_filled - Fill a rectangular area with char and color
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
;         AL = fill char, AH = color
; Output: none
;-----------------------------------------------------------------------
vga_draw_filled:
        PUSHALL
        mov edi, esi            ; rows remaining
        mov ebp, edx            ; width

.vdf_row:
        cmp edi, 0
        jle .vdf_done
        push rdx
        mov edx, ebp
        call vga_draw_hline
        pop rdx
        inc ecx
        dec edi
        jmp .vdf_row
.vdf_done:
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_clear_region - Clear a rectangular region (fill with spaces)
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
; Output: none
;-----------------------------------------------------------------------
vga_clear_region:
        push rax
        mov al, ' '
        mov ah, COLOR_DEFAULT
        call vga_draw_filled
        pop rax
        ret

;-----------------------------------------------------------------------
; vga_status_bar - Draw a colored status bar across full width
; Input:  ESI = text, ECX = row, DL = color
; Output: none
;-----------------------------------------------------------------------
vga_status_bar:
        PUSHALL
        ; Fill entire row with spaces in given color
        push rsi
        push rcx
        push rdx
        mov al, ' '
        mov ah, dl
        mov ebx, 0
        mov edx, VGA_WIDTH
        mov esi, 1
        call vga_draw_filled
        pop rdx
        pop rcx
        pop rsi
        ; Write text at column 1
        mov ebx, 1
        call vga_write_color
        POPALL
        ret

;-----------------------------------------------------------------------
; vga_progress_bar - Draw a progress bar
; Input:  EBX = left col, ECX = row, EDX = width (total bar)
;         ESI = current value, EDI = max value
;         AH = color attribute
; Output: none
;-----------------------------------------------------------------------
vga_progress_bar:
        PUSHALL
        ; Calculate filled width: (current * width) / max
        mov ebp, edx            ; save total width
        mov eax, esi
        mul edx                 ; EAX = current * width
        test edi, edi
        jz .vpb_skip
        xor edx, edx
        div edi                 ; EAX = filled width
        jmp .vpb_draw
.vpb_skip:
        xor eax, eax
.vpb_draw:
        mov esi, eax            ; filled count
        mov edi, ebp
        sub edi, eax            ; empty count

        ; Draw filled portion
        push rax
        mov al, BOX_FULL
        mov edx, esi
        call vga_draw_hline
        pop rax

        ; Draw empty portion
        push rbx
        add ebx, esi
        mov al, BOX_SHADE_L
        mov edx, edi
        call vga_draw_hline
        pop rbx

        POPALL
        ret

;-----------------------------------------------------------------------
; vga_scroll_region - Scroll a region up by one line
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
;         AH = color for cleared bottom row
; Output: none
;-----------------------------------------------------------------------
vga_scroll_region:
        PUSHALL
        mov edi, esi
        dec edi                 ; rows to copy = height - 1

        ; Calculate source start = (top+1, left)
        push rcx
        inc ecx
        push rax

        ; Each row: copy from (row, left) to (row-1, left), width words
.vsr_row:
        cmp edi, 0
        jle .vsr_clear

        ; Source offset
        push rcx
        push rbx
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE

        ; Dest offset (one row up)
        mov eax, ecx
        sub eax, VGA_WIDTH * 2

        ; Copy edx words
        push rsi
        push rdi
        push rcx
        mov esi, ecx
        mov edi, eax
        mov ecx, edx
        rep movsw
        pop rcx
        pop rdi
        pop rsi
        pop rbx
        pop rcx
        inc ecx
        dec edi
        jmp .vsr_row

.vsr_clear:
        ; Clear the bottom row
        pop rax
        pop rcx
        add ecx, esi
        dec ecx
        mov al, ' '
        call vga_draw_hline

        POPALL
        ret
