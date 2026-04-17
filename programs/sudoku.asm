; ==========================================================================
; sudoku - Sudoku puzzle game for Mellivora OS
;
; Usage: sudoku            Start a random puzzle
;
; Controls:
;   Arrow keys / WASD  - Move cursor
;   1-9                - Place number
;   0 / Delete / Space - Clear cell
;   N                  - New puzzle
;   C                  - Check solution
;   Q                  - Quit
;
; Fixed cells shown in white, editable cells in cyan.
; ==========================================================================
%include "syscalls.inc"

C_TITLE  equ 0x0E              ; Yellow
C_GRID   equ 0x07              ; Light gray
C_FIXED  equ 0x0F              ; Bright white
C_EDIT   equ 0x0B              ; Light cyan
C_CURSOR equ 0x1E              ; Yellow on blue
C_ERROR  equ 0x0C              ; Light red
C_OK     equ 0x0A              ; Light green
C_DEFAULT equ 0x07

start:
        call seed_rng
        call new_puzzle
        jmp game_loop

game_loop:
        call draw_screen

        ; Wait for input
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 'q'
        je quit
        cmp al, 'Q'
        je quit
        cmp al, 'n'
        je .new_game
        cmp al, 'N'
        je .new_game
        cmp al, 'c'
        je .check
        cmp al, 'C'
        je .check

        ; Movement
        cmp al, KEY_UP
        je .move_up
        cmp al, 'w'
        je .move_up
        cmp al, KEY_DOWN
        je .move_down
        cmp al, 's'
        je .move_down
        cmp al, KEY_LEFT
        je .move_left
        cmp al, 'a'
        je .move_left
        cmp al, KEY_RIGHT
        je .move_right
        cmp al, 'd'
        je .move_right

        ; Number input
        cmp al, '1'
        jb .clear_check
        cmp al, '9'
        ja game_loop
        sub al, '0'
        call place_number
        jmp game_loop

.clear_check:
        cmp al, '0'
        je .clear_cell
        cmp al, ' '
        je .clear_cell
        cmp al, 0x7F            ; Delete
        je .clear_cell
        jmp game_loop

.clear_cell:
        xor al, al
        call place_number
        jmp game_loop

.move_up:
        cmp byte [cursor_y], 0
        je game_loop
        dec byte [cursor_y]
        jmp game_loop
.move_down:
        cmp byte [cursor_y], 8
        jge game_loop
        inc byte [cursor_y]
        jmp game_loop
.move_left:
        cmp byte [cursor_x], 0
        je game_loop
        dec byte [cursor_x]
        jmp game_loop
.move_right:
        cmp byte [cursor_x], 8
        jge game_loop
        inc byte [cursor_x]
        jmp game_loop

.new_game:
        call new_puzzle
        jmp game_loop

.check:
        call check_solution
        jmp game_loop

quit:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -------------------------------------------------------------------
; place_number - Place digit AL at cursor (if cell is editable)
; -------------------------------------------------------------------
place_number:
        PUSHALL
        movzx ebx, byte [cursor_y]
        imul ebx, 9
        movzx ecx, byte [cursor_x]
        add ebx, ecx
        ; Check if fixed
        cmp byte [fixed + ebx], 1
        je .pn_done
        mov [board + ebx], al
.pn_done:
        POPALL
        ret

; -------------------------------------------------------------------
; draw_screen - Clear and redraw the entire board
; -------------------------------------------------------------------
draw_screen:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        ; Title
        mov eax, SYS_SETCOLOR
        mov ebx, C_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; Draw 9x9 grid
        xor ebp, ebp            ; row counter

.draw_row:
        cmp ebp, 9
        jge .draw_footer

        ; Top border for row 0, 3, 6
        mov eax, ebp
        xor edx, edx
        push rbx
        mov ebx, 3
        div ebx
        pop rbx
        test edx, edx
        jnz .no_thick_border
        mov eax, SYS_SETCOLOR
        mov ebx, C_GRID
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_thick_line
        int 0x80
.no_thick_border:

        ; Draw cells in this row
        xor ecx, ecx            ; col counter
.draw_col:
        cmp ecx, 9
        jge .row_end

        ; Box separator
        push rcx
        mov eax, ecx
        xor edx, edx
        push rbx
        mov ebx, 3
        div ebx
        pop rbx
        test edx, edx
        jnz .thin_sep
        ; Thick separator
        mov eax, SYS_SETCOLOR
        mov ebx, C_GRID
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        jmp .after_sep
.thin_sep:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
.after_sep:
        pop rcx

        ; Determine cell index
        mov eax, ebp
        imul eax, 9
        add eax, ecx
        push rcx

        ; Choose color: cursor, fixed, or editable
        cmp cl, [cursor_x]
        jne .not_cursor
        cmp byte [cursor_y], 0
        je .maybe_cursor_r0
        movzx edx, byte [cursor_y]
        cmp ebp, edx
        je .is_cursor
        jmp .not_cursor
.maybe_cursor_r0:
        test ebp, ebp
        jnz .not_cursor
.is_cursor:
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, C_CURSOR
        int 0x80
        pop rax
        jmp .draw_digit
.not_cursor:
        cmp byte [fixed + eax], 1
        jne .editable_color
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, C_FIXED
        int 0x80
        pop rax
        jmp .draw_digit
.editable_color:
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, C_EDIT
        int 0x80
        pop rax

.draw_digit:
        movzx ebx, byte [board + eax]
        test ebx, ebx
        jz .empty_cell
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .cell_done
.empty_cell:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
.cell_done:
        ; Reset color
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, C_GRID
        int 0x80
        pop rax

        pop rcx
        inc ecx
        jmp .draw_col

.row_end:
        ; Right border
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        inc ebp
        jmp .draw_row

.draw_footer:
        ; Bottom border
        mov eax, SYS_SETCOLOR
        mov ebx, C_GRID
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_thick_line
        int 0x80

        ; Status line
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_status
        int 0x80

        ; Show message if any
        cmp byte [msg_flag], 0
        je .no_msg
        movzx ebx, byte [msg_color]
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_result
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        mov byte [msg_flag], 0
.no_msg:
        POPALL
        ret

; -------------------------------------------------------------------
; check_solution - Validate current board
; -------------------------------------------------------------------
check_solution:
        PUSHALL
        ; Check all rows, columns, and 3x3 boxes

        ; Check rows
        xor ebp, ebp
.cr_row:
        cmp ebp, 9
        jge .cr_cols
        call clear_seen
        xor ecx, ecx
.cr_row_cell:
        cmp ecx, 9
        jge .cr_row_next
        mov eax, ebp
        imul eax, 9
        add eax, ecx
        movzx edx, byte [board + eax]
        test edx, edx
        jz .cr_incomplete
        dec edx
        cmp byte [seen + edx], 1
        je .cr_conflict
        mov byte [seen + edx], 1
        inc ecx
        jmp .cr_row_cell
.cr_row_next:
        inc ebp
        jmp .cr_row

        ; Check columns
.cr_cols:
        xor ebp, ebp
.cr_col:
        cmp ebp, 9
        jge .cr_boxes
        call clear_seen
        xor ecx, ecx
.cr_col_cell:
        cmp ecx, 9
        jge .cr_col_next
        mov eax, ecx
        imul eax, 9
        add eax, ebp
        movzx edx, byte [board + eax]
        test edx, edx
        jz .cr_incomplete
        dec edx
        cmp byte [seen + edx], 1
        je .cr_conflict
        mov byte [seen + edx], 1
        inc ecx
        jmp .cr_col_cell
.cr_col_next:
        inc ebp
        jmp .cr_col

        ; Check 3x3 boxes
.cr_boxes:
        xor ebp, ebp            ; box index (0-8)
.cr_box:
        cmp ebp, 9
        jge .cr_pass
        call clear_seen
        ; Box top-left: row = (box/3)*3, col = (box%3)*3
        mov eax, ebp
        xor edx, edx
        push rbx
        mov ebx, 3
        div ebx
        pop rbx
        imul eax, 3              ; start row
        imul edx, 3              ; start col
        mov [box_sr], eax
        mov [box_sc], edx

        xor ecx, ecx            ; 0..8 within box
.cr_box_cell:
        cmp ecx, 9
        jge .cr_box_next
        ; row = box_sr + i/3, col = box_sc + i%3
        mov eax, ecx
        xor edx, edx
        push rbx
        mov ebx, 3
        div ebx
        pop rbx
        add eax, [box_sr]
        add edx, [box_sc]
        imul eax, 9
        add eax, edx
        movzx edx, byte [board + eax]
        test edx, edx
        jz .cr_incomplete
        dec edx
        cmp byte [seen + edx], 1
        je .cr_conflict
        mov byte [seen + edx], 1
        inc ecx
        jmp .cr_box_cell
.cr_box_next:
        inc ebp
        jmp .cr_box

.cr_pass:
        ; All valid!
        mov byte [msg_flag], 1
        mov byte [msg_color], C_OK
        mov esi, msg_correct
        mov edi, msg_result
        call copy_str
        POPALL
        ret

.cr_incomplete:
        mov byte [msg_flag], 1
        mov byte [msg_color], C_ERROR
        mov esi, msg_incomplete
        mov edi, msg_result
        call copy_str
        POPALL
        ret

.cr_conflict:
        mov byte [msg_flag], 1
        mov byte [msg_color], C_ERROR
        mov esi, msg_conflict
        mov edi, msg_result
        call copy_str
        POPALL
        ret

clear_seen:
        push rcx
        push rdi
        push rax
        mov edi, seen
        mov ecx, 9
        xor eax, eax
        rep stosb
        pop rax
        pop rdi
        pop rcx
        ret

copy_str:
.cs_loop:
        lodsb
        stosb
        test al, al
        jnz .cs_loop
        ret

; -------------------------------------------------------------------
; new_puzzle - Generate a new puzzle
; -------------------------------------------------------------------
new_puzzle:
        PUSHALL
        ; Clear board and fixed arrays
        mov edi, board
        mov ecx, 81
        xor al, al
        rep stosb
        mov edi, fixed
        mov ecx, 81
        rep stosb

        ; Pick one of 4 hardcoded puzzles
        mov eax, [rng_state]
        and eax, 3
        imul eax, 81
        lea esi, [puzzles + eax]
        mov edi, board
        mov ecx, 81
        rep movsb

        ; Mark non-zero cells as fixed
        mov ecx, 81
        xor ebx, ebx
.mark_fixed:
        cmp byte [board + ebx], 0
        je .mf_next
        mov byte [fixed + ebx], 1
.mf_next:
        inc ebx
        dec ecx
        jnz .mark_fixed

        ; Reset cursor
        mov byte [cursor_x], 0
        mov byte [cursor_y], 0
        mov byte [msg_flag], 0

        ; Advance RNG
        call rng_next

        POPALL
        ret

; -------------------------------------------------------------------
; RNG (simple LCG)
; -------------------------------------------------------------------
seed_rng:
        mov eax, SYS_GETTIME
        int 0x80
        mov [rng_state], eax
        ret

rng_next:
        mov eax, [rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rng_state], eax
        ret

; -------------------------------------------------------------------
; Strings
; -------------------------------------------------------------------
msg_title:      db "  === SUDOKU ===", 0x0A, 0
msg_thick_line: db "+---+---+---+", 0x0A, 0
msg_status:     db "Arrows:move  1-9:place  0:clear  C:check  N:new  Q:quit", 0x0A, 0
msg_correct:    db "Correct! Puzzle solved!", 0x0A, 0
msg_incomplete: db "Board is incomplete.", 0x0A, 0
msg_conflict:   db "Conflict detected!", 0x0A, 0

; -------------------------------------------------------------------
; 4 embedded puzzles (81 bytes each, 0=empty)
; Puzzle 1 (easy)
puzzles:
        db 5,3,0, 0,7,0, 0,0,0
        db 6,0,0, 1,9,5, 0,0,0
        db 0,9,8, 0,0,0, 0,6,0
        db 8,0,0, 0,6,0, 0,0,3
        db 4,0,0, 8,0,3, 0,0,1
        db 7,0,0, 0,2,0, 0,0,6
        db 0,6,0, 0,0,0, 2,8,0
        db 0,0,0, 4,1,9, 0,0,5
        db 0,0,0, 0,8,0, 0,7,9

; Puzzle 2 (medium)
        db 0,0,0, 2,6,0, 7,0,1
        db 6,8,0, 0,7,0, 0,9,0
        db 1,9,0, 0,0,4, 5,0,0
        db 8,2,0, 1,0,0, 0,4,0
        db 0,0,4, 6,0,2, 9,0,0
        db 0,5,0, 0,0,3, 0,2,8
        db 0,0,9, 3,0,0, 0,7,4
        db 0,4,0, 0,5,0, 0,3,6
        db 7,0,3, 0,1,8, 0,0,0

; Puzzle 3 (medium)
        db 0,0,5, 3,0,0, 0,0,0
        db 8,0,0, 0,0,0, 0,2,0
        db 0,7,0, 0,1,0, 5,0,0
        db 4,0,0, 0,0,5, 3,0,0
        db 0,1,0, 0,7,0, 0,0,6
        db 0,0,3, 2,0,0, 0,8,0
        db 0,6,0, 5,0,0, 0,0,9
        db 0,0,4, 0,0,0, 0,3,0
        db 0,0,0, 0,0,9, 7,0,0

; Puzzle 4 (hard)
        db 0,0,0, 6,0,0, 4,0,0
        db 7,0,0, 0,0,3, 6,0,0
        db 0,0,0, 0,9,1, 0,8,0
        db 0,0,0, 0,0,0, 0,0,0
        db 0,5,0, 1,8,0, 0,0,3
        db 0,0,0, 3,0,6, 0,4,5
        db 0,4,0, 2,0,0, 0,6,0
        db 9,0,3, 0,0,0, 0,0,0
        db 0,2,0, 0,0,0, 1,0,0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
board:          times 81 db 0
fixed:          times 81 db 0
seen:           times 9 db 0
cursor_x:       db 0
cursor_y:       db 0
msg_flag:       db 0
msg_color:      db 0
msg_result:     times 64 db 0
rng_state:      dd 0
box_sr:         dd 0
box_sc:         dd 0
