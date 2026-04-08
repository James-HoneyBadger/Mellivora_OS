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
        push eax
        mov eax, SYS_SETCURSOR
        int 0x80
        pop eax
        ret

;-----------------------------------------------------------------------
; vga_set_color - Set current text color
; Input:  BL = color attribute
; Output: none
;-----------------------------------------------------------------------
vga_set_color:
        push eax
        push ebx
        movzx ebx, bl
        mov eax, SYS_SETCOLOR
        int 0x80
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; vga_clear - Clear the screen
;-----------------------------------------------------------------------
vga_clear:
        push eax
        mov eax, SYS_CLEAR
        int 0x80
        pop eax
        ret

;-----------------------------------------------------------------------
; vga_make_color - Create color attribute from foreground and background
; Input:  AL = foreground (0-15), AH = background (0-7)
; Output: AL = combined color attribute
;-----------------------------------------------------------------------
vga_make_color:
        push ecx
        movzx ecx, ah
        shl cl, 4
        and al, 0x0F
        or al, cl
        pop ecx
        ret

;-----------------------------------------------------------------------
; vga_put_char_at - Write a character with color at specific position
; Input:  AL = character, AH = color attribute
;         EBX = column (0-79), ECX = row (0-24)
; Output: none
;-----------------------------------------------------------------------
vga_put_char_at:
        pushad
        ; Calculate VGA offset: (row * 80 + col) * 2
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax           ; write char + attr
        popad
        ret

;-----------------------------------------------------------------------
; vga_get_char_at - Read character and color from position
; Input:  EBX = column, ECX = row
; Output: AL = character, AH = color attribute
;-----------------------------------------------------------------------
vga_get_char_at:
        push ecx
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov ax, [ecx]
        pop ecx
        ret

;-----------------------------------------------------------------------
; vga_write_at - Write string at position using current color
; Input:  ESI = string, EBX = column, ECX = row
; Output: none
;-----------------------------------------------------------------------
vga_write_at:
        pushad
        ; Set cursor position
        mov eax, SYS_SETCURSOR
        int 0x80
        ; Print string
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        popad
        ret

;-----------------------------------------------------------------------
; vga_write_color - Write string at position with specific color
; Input:  ESI = string, EBX = column, ECX = row, DL = color
; Output: none
;-----------------------------------------------------------------------
vga_write_color:
        pushad
        ; Set color
        push ebx
        push ecx
        movzx ebx, dl
        mov eax, SYS_SETCOLOR
        int 0x80
        pop ecx
        pop ebx
        ; Set cursor
        mov eax, SYS_SETCURSOR
        int 0x80
        ; Print
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        popad
        ret

;-----------------------------------------------------------------------
; vga_draw_hline - Draw a horizontal line
; Input:  EBX = start col, ECX = row, EDX = length, AL = char, AH = color
; Output: none
;-----------------------------------------------------------------------
vga_draw_hline:
        pushad
        mov esi, edx            ; length counter
        ; Calc VGA offset
        push eax
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        pop eax
.vdhl_loop:
        cmp esi, 0
        jle .vdhl_done
        mov [ecx], ax
        add ecx, 2
        dec esi
        jmp .vdhl_loop
.vdhl_done:
        popad
        ret

;-----------------------------------------------------------------------
; vga_draw_vline - Draw a vertical line
; Input:  EBX = col, ECX = start row, EDX = length, AL = char, AH = color
; Output: none
;-----------------------------------------------------------------------
vga_draw_vline:
        pushad
        mov esi, edx            ; length
        push eax
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        pop eax
.vdvl_loop:
        cmp esi, 0
        jle .vdvl_done
        mov [ecx], ax
        add ecx, VGA_WIDTH * 2
        dec esi
        jmp .vdvl_loop
.vdvl_done:
        popad
        ret

;-----------------------------------------------------------------------
; vga_draw_box - Draw a single-line box
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
;         AH = color attribute
; Output: none
;-----------------------------------------------------------------------
vga_draw_box:
        pushad
        mov edi, esi            ; height
        mov ebp, edx            ; width

        ; Top-left corner
        mov al, BOX_TL
        call vga_put_char_at
        ; Top-right corner
        push ebx
        add ebx, ebp
        dec ebx
        mov al, BOX_TR
        call vga_put_char_at
        pop ebx
        ; Top horizontal line
        push ebx
        inc ebx
        sub ebp, 2             ; inner width
        push edx
        mov edx, ebp
        mov al, BOX_H
        call vga_draw_hline
        pop edx
        pop ebx
        add ebp, 2             ; restore width

        ; Bottom corners and line
        push ecx
        add ecx, edi
        dec ecx
        mov al, BOX_BL
        call vga_put_char_at
        push ebx
        add ebx, ebp
        dec ebx
        mov al, BOX_BR
        call vga_put_char_at
        pop ebx
        push ebx
        inc ebx
        push edx
        mov edx, ebp
        sub edx, 2
        mov al, BOX_H
        call vga_draw_hline
        pop edx
        pop ebx
        pop ecx

        ; Left and right vertical lines
        push ecx
        inc ecx
        push edx
        mov edx, edi
        sub edx, 2
        mov al, BOX_V
        call vga_draw_vline
        push ebx
        add ebx, ebp
        dec ebx
        call vga_draw_vline
        pop ebx
        pop edx
        pop ecx

        popad
        ret

;-----------------------------------------------------------------------
; vga_draw_filled - Fill a rectangular area with char and color
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
;         AL = fill char, AH = color
; Output: none
;-----------------------------------------------------------------------
vga_draw_filled:
        pushad
        mov edi, esi            ; rows remaining
        mov ebp, edx            ; width

.vdf_row:
        cmp edi, 0
        jle .vdf_done
        push edx
        mov edx, ebp
        call vga_draw_hline
        pop edx
        inc ecx
        dec edi
        jmp .vdf_row
.vdf_done:
        popad
        ret

;-----------------------------------------------------------------------
; vga_clear_region - Clear a rectangular region (fill with spaces)
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
; Output: none
;-----------------------------------------------------------------------
vga_clear_region:
        push eax
        mov al, ' '
        mov ah, COLOR_DEFAULT
        call vga_draw_filled
        pop eax
        ret

;-----------------------------------------------------------------------
; vga_status_bar - Draw a colored status bar across full width
; Input:  ESI = text, ECX = row, DL = color
; Output: none
;-----------------------------------------------------------------------
vga_status_bar:
        pushad
        ; Fill entire row with spaces in given color
        push esi
        push ecx
        push edx
        mov al, ' '
        mov ah, dl
        mov ebx, 0
        mov edx, VGA_WIDTH
        mov esi, 1
        call vga_draw_filled
        pop edx
        pop ecx
        pop esi
        ; Write text at column 1
        mov ebx, 1
        call vga_write_color
        popad
        ret

;-----------------------------------------------------------------------
; vga_progress_bar - Draw a progress bar
; Input:  EBX = left col, ECX = row, EDX = width (total bar)
;         ESI = current value, EDI = max value
;         AH = color attribute
; Output: none
;-----------------------------------------------------------------------
vga_progress_bar:
        pushad
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
        push eax
        mov al, BOX_FULL
        mov edx, esi
        call vga_draw_hline
        pop eax

        ; Draw empty portion
        push ebx
        add ebx, esi
        mov al, BOX_SHADE_L
        mov edx, edi
        call vga_draw_hline
        pop ebx

        popad
        ret

;-----------------------------------------------------------------------
; vga_scroll_region - Scroll a region up by one line
; Input:  EBX = left col, ECX = top row, EDX = width, ESI = height
;         AH = color for cleared bottom row
; Output: none
;-----------------------------------------------------------------------
vga_scroll_region:
        pushad
        mov edi, esi
        dec edi                 ; rows to copy = height - 1

        ; Calculate source start = (top+1, left)
        push ecx
        inc ecx
        push eax

        ; Each row: copy from (row, left) to (row-1, left), width words
.vsr_row:
        cmp edi, 0
        jle .vsr_clear

        ; Source offset
        push ecx
        push ebx
        imul ecx, VGA_WIDTH
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE

        ; Dest offset (one row up)
        mov eax, ecx
        sub eax, VGA_WIDTH * 2

        ; Copy edx words
        push esi
        push edi
        push ecx
        mov esi, ecx
        mov edi, eax
        mov ecx, edx
        rep movsw
        pop ecx
        pop edi
        pop esi
        pop ebx
        pop ecx
        inc ecx
        dec edi
        jmp .vsr_row

.vsr_clear:
        ; Clear the bottom row
        pop eax
        pop ecx
        add ecx, esi
        dec ecx
        mov al, ' '
        call vga_draw_hline

        popad
        ret
