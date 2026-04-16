; 2048.asm - The 2048 sliding tile game for Mellivora OS
%include "syscalls.inc"

BOARD_SIZE equ 4
NUM_CELLS  equ 16

start:
        call init_game

.game_loop:
        call draw_board

        ; Get key
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, KEY_UP
        je .move_up
        cmp al, KEY_DOWN
        je .move_down
        cmp al, KEY_LEFT
        je .move_left
        cmp al, KEY_RIGHT
        je .move_right
        cmp al, 'w'
        je .move_up
        cmp al, 's'
        je .move_down
        cmp al, 'a'
        je .move_left
        cmp al, 'd'
        je .move_right
        cmp al, 'r'
        je .restart
        jmp .game_loop

.move_up:
        mov byte [moved], 0
        ; For each column
        xor ecx, ecx
.mu_col:
        ; Slide column up
        mov edx, 0             ; write position (row)
        mov ebx, 0             ; read position (row)
.mu_slide:
        cmp ebx, BOARD_SIZE
        jge .mu_merge
        mov eax, ebx
        shl eax, 2
        add eax, ecx
        cmp dword [board + eax*4], 0
        je .mu_skip
        mov esi, [board + eax*4]
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov [temp_col + edx*4], esi
        inc edx
.mu_skip:
        inc ebx
        jmp .mu_slide
.mu_merge:
        ; Zero rest
        mov ebx, edx
.mu_zero:
        cmp ebx, BOARD_SIZE
        jge .mu_write
        mov dword [temp_col + ebx*4], 0
        inc ebx
        jmp .mu_zero
.mu_write:
        ; Merge adjacent
        xor edx, edx
.mu_mg:
        cmp edx, BOARD_SIZE - 1
        jge .mu_copy
        mov eax, [temp_col + edx*4]
        cmp eax, 0
        je .mu_mg_next
        cmp eax, [temp_col + edx*4 + 4]
        jne .mu_mg_next
        shl eax, 1
        mov [temp_col + edx*4], eax
        add [score], eax
        ; Shift rest down
        mov ebx, edx
        inc ebx
.mu_sh:
        cmp ebx, BOARD_SIZE - 1
        jge .mu_sh_end
        mov eax, [temp_col + ebx*4 + 4]
        mov [temp_col + ebx*4], eax
        inc ebx
        jmp .mu_sh
.mu_sh_end:
        mov dword [temp_col + (BOARD_SIZE-1)*4], 0
.mu_mg_next:
        inc edx
        jmp .mu_mg
.mu_copy:
        ; Write temp_col back to board column ecx
        xor edx, edx
.mu_cp:
        cmp edx, BOARD_SIZE
        jge .mu_col_next
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov ebx, [temp_col + edx*4]
        cmp ebx, [board + eax*4]
        je .mu_cp_same
        mov byte [moved], 1
.mu_cp_same:
        mov [board + eax*4], ebx
        inc edx
        jmp .mu_cp
.mu_col_next:
        inc ecx
        cmp ecx, BOARD_SIZE
        jl .mu_col
        jmp .after_move

.move_down:
        mov byte [moved], 0
        xor ecx, ecx
.md_col:
        mov edx, BOARD_SIZE - 1
        mov ebx, BOARD_SIZE - 1
.md_slide:
        cmp ebx, 0
        jl .md_merge
        mov eax, ebx
        shl eax, 2
        add eax, ecx
        cmp dword [board + eax*4], 0
        je .md_skip
        mov esi, [board + eax*4]
        mov [temp_col + edx*4], esi
        dec edx
.md_skip:
        dec ebx
        jmp .md_slide
.md_merge:
        mov ebx, edx
.md_zero:
        cmp ebx, 0
        jl .md_write
        mov dword [temp_col + ebx*4], 0
        dec ebx
        jmp .md_zero
.md_write:
        mov edx, BOARD_SIZE - 1
.md_mg:
        cmp edx, 0
        jle .md_copy
        mov eax, [temp_col + edx*4]
        cmp eax, 0
        je .md_mg_next
        cmp eax, [temp_col + edx*4 - 4]
        jne .md_mg_next
        shl eax, 1
        mov [temp_col + edx*4], eax
        add [score], eax
        mov ebx, edx
        dec ebx
.md_sh:
        cmp ebx, 0
        jle .md_sh_end
        mov eax, [temp_col + ebx*4 - 4]
        mov [temp_col + ebx*4], eax
        dec ebx
        jmp .md_sh
.md_sh_end:
        mov dword [temp_col], 0
.md_mg_next:
        dec edx
        jmp .md_mg
.md_copy:
        xor edx, edx
.md_cp:
        cmp edx, BOARD_SIZE
        jge .md_col_next
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov ebx, [temp_col + edx*4]
        cmp ebx, [board + eax*4]
        je .md_cp_same
        mov byte [moved], 1
.md_cp_same:
        mov [board + eax*4], ebx
        inc edx
        jmp .md_cp
.md_col_next:
        inc ecx
        cmp ecx, BOARD_SIZE
        jl .md_col
        jmp .after_move

.move_left:
        mov byte [moved], 0
        xor edx, edx            ; row
.ml_row:
        mov ecx, 0              ; write pos (col)
        mov ebx, 0              ; read pos (col)
.ml_slide:
        cmp ebx, BOARD_SIZE
        jge .ml_merge
        mov eax, edx
        shl eax, 2
        add eax, ebx
        cmp dword [board + eax*4], 0
        je .ml_skip
        mov esi, [board + eax*4]
        mov [temp_col + ecx*4], esi
        inc ecx
.ml_skip:
        inc ebx
        jmp .ml_slide
.ml_merge:
        mov ebx, ecx
.ml_zero:
        cmp ebx, BOARD_SIZE
        jge .ml_write
        mov dword [temp_col + ebx*4], 0
        inc ebx
        jmp .ml_zero
.ml_write:
        xor ecx, ecx
.ml_mg:
        cmp ecx, BOARD_SIZE - 1
        jge .ml_copy
        mov eax, [temp_col + ecx*4]
        cmp eax, 0
        je .ml_mg_next
        cmp eax, [temp_col + ecx*4 + 4]
        jne .ml_mg_next
        shl eax, 1
        mov [temp_col + ecx*4], eax
        add [score], eax
        mov ebx, ecx
        inc ebx
.ml_sh:
        cmp ebx, BOARD_SIZE - 1
        jge .ml_sh_end
        mov eax, [temp_col + ebx*4 + 4]
        mov [temp_col + ebx*4], eax
        inc ebx
        jmp .ml_sh
.ml_sh_end:
        mov dword [temp_col + (BOARD_SIZE-1)*4], 0
.ml_mg_next:
        inc ecx
        jmp .ml_mg
.ml_copy:
        xor ecx, ecx
.ml_cp:
        cmp ecx, BOARD_SIZE
        jge .ml_row_next
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov ebx, [temp_col + ecx*4]
        cmp ebx, [board + eax*4]
        je .ml_cp_same
        mov byte [moved], 1
.ml_cp_same:
        mov [board + eax*4], ebx
        inc ecx
        jmp .ml_cp
.ml_row_next:
        inc edx
        cmp edx, BOARD_SIZE
        jl .ml_row
        jmp .after_move

.move_right:
        mov byte [moved], 0
        xor edx, edx
.mr_row:
        mov ecx, BOARD_SIZE - 1
        mov ebx, BOARD_SIZE - 1
.mr_slide:
        cmp ebx, 0
        jl .mr_merge
        mov eax, edx
        shl eax, 2
        add eax, ebx
        cmp dword [board + eax*4], 0
        je .mr_skip
        mov esi, [board + eax*4]
        mov [temp_col + ecx*4], esi
        dec ecx
.mr_skip:
        dec ebx
        jmp .mr_slide
.mr_merge:
        mov ebx, ecx
.mr_zero:
        cmp ebx, 0
        jl .mr_write
        mov dword [temp_col + ebx*4], 0
        dec ebx
        jmp .mr_zero
.mr_write:
        mov ecx, BOARD_SIZE - 1
.mr_mg:
        cmp ecx, 0
        jle .mr_copy
        mov eax, [temp_col + ecx*4]
        cmp eax, 0
        je .mr_mg_next
        cmp eax, [temp_col + ecx*4 - 4]
        jne .mr_mg_next
        shl eax, 1
        mov [temp_col + ecx*4], eax
        add [score], eax
        mov ebx, ecx
        dec ebx
.mr_sh:
        cmp ebx, 0
        jle .mr_sh_end
        mov eax, [temp_col + ebx*4 - 4]
        mov [temp_col + ebx*4], eax
        dec ebx
        jmp .mr_sh
.mr_sh_end:
        mov dword [temp_col], 0
.mr_mg_next:
        dec ecx
        jmp .mr_mg
.mr_copy:
        xor ecx, ecx
.mr_cp:
        cmp ecx, BOARD_SIZE
        jge .mr_row_next
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov ebx, [temp_col + ecx*4]
        cmp ebx, [board + eax*4]
        je .mr_cp_same
        mov byte [moved], 1
.mr_cp_same:
        mov [board + eax*4], ebx
        inc ecx
        jmp .mr_cp
.mr_row_next:
        inc edx
        cmp edx, BOARD_SIZE
        jl .mr_row
        jmp .after_move

.after_move:
        cmp byte [moved], 0
        je .game_loop
        call add_random_tile
        call check_game_over
        cmp byte [game_over], 1
        je .show_game_over
        jmp .game_loop

.show_game_over:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_game_over
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        jmp .quit

.restart:
        call init_game
        jmp .game_loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; Initialize game
;---------------------------------------
init_game:
        PUSHALL
        ; Clear board
        mov edi, board
        mov ecx, NUM_CELLS
        xor eax, eax
.ig_clr:
        mov [edi], eax
        add edi, 4
        dec ecx
        jnz .ig_clr

        mov dword [score], 0
        mov byte [game_over], 0
        ; Seed PRNG
        mov eax, SYS_GETTIME
        int 0x80
        mov [prng_state], eax

        call add_random_tile
        call add_random_tile
        POPALL
        ret

;---------------------------------------
; Add random tile (2 or 4)
;---------------------------------------
add_random_tile:
        PUSHALL
        ; Count empty cells
        xor ecx, ecx
        xor ebx, ebx
.art_count:
        cmp ebx, NUM_CELLS
        jge .art_pick
        cmp dword [board + ebx*4], 0
        jne .art_cnt_next
        mov [empty_cells + ecx*4], ebx
        inc ecx
.art_cnt_next:
        inc ebx
        jmp .art_count

.art_pick:
        cmp ecx, 0
        je .art_done
        call prng
        xor edx, edx
        div ecx
        ; edx = index into empty_cells
        mov eax, [empty_cells + edx*4]
        ; 90% chance of 2, 10% chance of 4
        call prng
        xor edx, edx
        mov ebx, 10
        div ebx
        cmp edx, 0
        je .art_four
        mov dword [board + eax*4], 2
        jmp .art_done
.art_four:
        mov dword [board + eax*4], 4
.art_done:
        POPALL
        ret

;---------------------------------------
; Check game over
;---------------------------------------
check_game_over:
        PUSHALL
        mov byte [game_over], 0
        ; Check for any empty cell
        xor ecx, ecx
.cgo_loop:
        cmp ecx, NUM_CELLS
        jge .cgo_check_adj
        cmp dword [board + ecx*4], 0
        je .cgo_not_over
        inc ecx
        jmp .cgo_loop

.cgo_check_adj:
        ; Check adjacent cells for merges
        xor edx, edx            ; row
.cgo_row:
        xor ecx, ecx            ; col
.cgo_col:
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov ebx, [board + eax*4]
        ; Check right
        mov esi, ecx
        inc esi
        cmp esi, BOARD_SIZE
        jge .cgo_no_right
        mov eax, edx
        shl eax, 2
        add eax, esi
        cmp ebx, [board + eax*4]
        je .cgo_not_over
.cgo_no_right:
        ; Check down
        mov esi, edx
        inc esi
        cmp esi, BOARD_SIZE
        jge .cgo_no_down
        mov eax, esi
        shl eax, 2
        add eax, ecx
        cmp ebx, [board + eax*4]
        je .cgo_not_over
.cgo_no_down:
        inc ecx
        cmp ecx, BOARD_SIZE
        jl .cgo_col
        inc edx
        cmp edx, BOARD_SIZE
        jl .cgo_row
        mov byte [game_over], 1
.cgo_not_over:
        POPALL
        ret

;---------------------------------------
; Draw the board
;---------------------------------------
draw_board:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [score]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_separator
        int 0x80

        xor edx, edx            ; row
.db_row:
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        xor ecx, ecx            ; col
.db_col:
        mov eax, edx
        shl eax, 2
        add eax, ecx
        mov eax, [board + eax*4]
        cmp eax, 0
        je .db_empty

        ; Print number right-justified in 6 chars
        push rdx
        push rcx
        call .count_digits       ; result in edi
        mov ecx, 6
        sub ecx, edi
        shr ecx, 1
        push rcx
        ; leading spaces
.db_lead:
        cmp ecx, 0
        jle .db_pval
        push rax
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rax
        dec ecx
        jmp .db_lead
.db_pval:
        call print_dec
        pop rcx
        mov esi, 6
        sub esi, edi
        sub esi, ecx
.db_trail:
        cmp esi, 0
        jle .db_colsep
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec esi
        jmp .db_trail

.db_empty:
        push rdx
        push rcx
        mov eax, SYS_PRINT
        mov ebx, msg_empty_cell
        int 0x80

.db_colsep:
        pop rcx
        pop rdx
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        inc ecx
        cmp ecx, BOARD_SIZE
        jl .db_col

        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_separator
        int 0x80

        inc edx
        cmp edx, BOARD_SIZE
        jl .db_row

        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        POPALL
        ret

; Count decimal digits of EAX -> EDI
.count_digits:
        push rax
        push rdx
        xor edi, edi
        mov ebx, 10
        cmp eax, 0
        jne .cd_loop
        mov edi, 1
        jmp .cd_done
.cd_loop:
        cmp eax, 0
        je .cd_done
        xor edx, edx
        div ebx
        inc edi
        jmp .cd_loop
.cd_done:
        pop rdx
        pop rax
        ret

;---------------------------------------
; PRNG
;---------------------------------------
prng:
        push rbx
        mov eax, [prng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [prng_state], eax
        shr eax, 16
        and eax, 0x7FFF
        pop rbx
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_title:      db "=== 2048 ===", 0x0A, 0
msg_score:      db "Score: ", 0
msg_separator:  db "+------+------+------+------+", 0x0A, 0
msg_empty_cell: db "      ", 0
msg_controls:   db "Arrow keys/WASD to move, [r]estart, [q]uit", 0x0A, 0
msg_game_over:  db 0x0A, "  GAME OVER! Press any key.", 0x0A, 0
prng_state:     dd 42

;---------------------------------------
; BSS
;---------------------------------------
board:       times NUM_CELLS dd 0
temp_col:    times BOARD_SIZE dd 0
score:       dd 0
moved:       db 0
game_over:   db 0
empty_cells: times NUM_CELLS dd 0
