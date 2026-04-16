; ==========================================================================
; mines - Minesweeper game for Mellivora OS
;
; Usage: mines [easy|medium|hard]
;   easy:    9x9,  10 mines (default)
;   medium: 16x12, 30 mines
;   hard:   20x15, 60 mines
;
; Controls:
;   Arrow keys / WASD  - Move cursor
;   Space / Enter      - Reveal cell
;   F                  - Toggle flag
;   R                  - New game
;   Q                  - Quit
; ==========================================================================
%include "syscalls.inc"

; Cell flags (stored in board[])
CELL_MINE       equ 0x80       ; bit 7: has mine
CELL_REVEALED   equ 0x40       ; bit 6: revealed
CELL_FLAGGED    equ 0x20       ; bit 5: flagged
CELL_COUNT_MASK equ 0x0F       ; bits 0-3: adjacent mine count

; Colors
C_TITLE   equ 0x0E             ; Yellow
C_HIDDEN  equ 0x70             ; Black on gray
C_FLAG    equ 0x4F             ; White on red
C_MINE    equ 0xCF             ; White on light red
C_REVEAL  equ 0x07             ; Light gray
C_CURSOR  equ 0x1E             ; Yellow on blue
C_NUM1    equ 0x09             ; Blue
C_NUM2    equ 0x02             ; Green
C_NUM3    equ 0x0C             ; Red
C_NUM4    equ 0x01             ; Dark blue
C_DEFAULT equ 0x07
C_OK      equ 0x0A             ; Green

MAX_W       equ 20
MAX_H       equ 15
MAX_CELLS   equ (MAX_W * MAX_H)  ; 300

start:
        call seed_rng

        ; Parse difficulty
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Default: easy
        mov byte [grid_w], 9
        mov byte [grid_h], 9
        mov byte [mine_count], 10

        mov esi, arg_buf
        cmp byte [esi], 0
        je .setup

        ; Check for "medium"
        cmp dword [esi], 'medi'
        jne .not_medium
        mov byte [grid_w], 16
        mov byte [grid_h], 12
        mov byte [mine_count], 30
        jmp .setup
.not_medium:
        cmp dword [esi], 'hard'
        jne .setup
        mov byte [grid_w], 20
        mov byte [grid_h], 15
        mov byte [mine_count], 60

.setup:
        call new_game

game_loop:
        call draw_screen

        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 'q'
        je quit
        cmp al, 'Q'
        je quit

        cmp byte [game_over], 0
        jne .only_restart       ; if game over, only R works

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
        cmp al, ' '
        je .reveal
        cmp al, 0x0D            ; Enter
        je .reveal
        cmp al, 'f'
        je .flag
        cmp al, 'F'
        je .flag

.only_restart:
        cmp al, 'r'
        je .restart
        cmp al, 'R'
        je .restart
        jmp game_loop

.move_up:
        cmp byte [cursor_y], 0
        je game_loop
        dec byte [cursor_y]
        jmp game_loop
.move_down:
        movzx eax, byte [grid_h]
        dec eax
        cmp [cursor_y], al
        jge game_loop
        inc byte [cursor_y]
        jmp game_loop
.move_left:
        cmp byte [cursor_x], 0
        je game_loop
        dec byte [cursor_x]
        jmp game_loop
.move_right:
        movzx eax, byte [grid_w]
        dec eax
        cmp [cursor_x], al
        jge game_loop
        inc byte [cursor_x]
        jmp game_loop

.reveal:
        call reveal_cell
        jmp game_loop

.flag:
        call toggle_flag
        jmp game_loop

.restart:
        call new_game
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
; new_game - Reset board and place mines
; -------------------------------------------------------------------
new_game:
        PUSHALL
        mov byte [game_over], 0
        mov byte [game_won], 0
        mov byte [cursor_x], 0
        mov byte [cursor_y], 0
        mov dword [cells_revealed], 0

        ; Clear board
        mov edi, board
        mov ecx, MAX_CELLS
        xor al, al
        rep stosb

        ; Place mines randomly
        movzx ecx, byte [mine_count]
.place_mine:
        test ecx, ecx
        jz .count_adj

        ; Random position
        call rng_next
        mov eax, [rng_state]
        shr eax, 3
        xor edx, edx
        movzx ebx, byte [grid_w]
        div ebx
        push rdx                ; X = edx

        call rng_next
        mov eax, [rng_state]
        shr eax, 7
        xor edx, edx
        movzx ebx, byte [grid_h]
        div ebx
        mov ebx, edx            ; Y = ebx

        pop rax                 ; X = eax

        ; Calculate index: Y * grid_w + X
        push rcx
        movzx ecx, byte [grid_w]
        imul ebx, ecx
        add ebx, eax

        ; Check if already a mine
        test byte [board + ebx], CELL_MINE
        jnz .mine_retry
        or byte [board + ebx], CELL_MINE
        pop rcx
        dec ecx
        jmp .place_mine

.mine_retry:
        pop rcx
        jmp .place_mine

.count_adj:
        ; Count adjacent mines for each cell
        xor ebp, ebp            ; Y
.ca_row:
        movzx eax, byte [grid_h]
        cmp ebp, eax
        jge .ca_done
        xor ecx, ecx            ; X
.ca_col:
        movzx eax, byte [grid_w]
        cmp ecx, eax
        jge .ca_row_next

        ; Skip mine cells (they don't need count)
        movzx eax, byte [grid_w]
        imul eax, ebp
        add eax, ecx
        test byte [board + eax], CELL_MINE
        jnz .ca_next

        ; Count neighbors with mines
        push rcx
        push rbp
        call count_adj_mines    ; ECX=x, EBP=y -> AL=count
        pop rbp
        pop rcx
        movzx ebx, byte [grid_w]
        imul ebx, ebp
        add ebx, ecx
        or [board + ebx], al

.ca_next:
        inc ecx
        jmp .ca_col
.ca_row_next:
        inc ebp
        jmp .ca_row
.ca_done:
        POPALL
        ret

; count_adj_mines - Count mines around (ECX, EBP)
; Returns AL = count (0-8)
count_adj_mines:
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi

        xor edi, edi            ; count
        mov esi, ecx            ; save X
        mov edx, ebp            ; save Y

        ; Check all 8 neighbors
        ; dy = -1, 0, +1 ; dx = -1, 0, +1
        mov ecx, -1             ; dy start
.cam_dy:
        cmp ecx, 2
        jge .cam_done
        mov ebx, -1             ; dx start
.cam_dx:
        cmp ebx, 2
        jge .cam_dy_next
        ; Skip (0,0)
        test ecx, ecx
        jnz .cam_check
        test ebx, ebx
        jz .cam_dx_next
.cam_check:
        ; ny = Y + dy, nx = X + dx
        mov eax, edx
        add eax, ecx            ; ny
        js .cam_dx_next          ; < 0
        movzx edi, byte [grid_h]
        cmp eax, edi
        jge .cam_dx_next
        push rax                ; save ny

        mov eax, esi
        add eax, ebx            ; nx
        js .cam_dx_pop           ; < 0
        movzx edi, byte [grid_w]
        cmp eax, edi
        jge .cam_dx_pop

        ; Valid neighbor: check board[ny * grid_w + nx]
        pop rdi                  ; ny
        push rdi
        movzx edi, byte [grid_w]
        push rax
        push rbx
        imul edi, [rsp + 8]     ; ny (on stack below eax, ebx)
        pop rbx
        pop rax

        ; Ugh, recalculate cleanly
        pop rdi                  ; ny
        push rdi
        push rax
        push rbx
        push rcx
        movzx ecx, byte [grid_w]
        imul edi, ecx
        add edi, eax            ; index = ny * w + nx
        test byte [board + edi], CELL_MINE
        jz .cam_no_mine
        ; Restore count from a different place
        ; We'll use a simpler approach — use [cam_cnt]
        inc dword [cam_cnt]
.cam_no_mine:
        pop rcx
        pop rbx
        pop rax

.cam_dx_pop:
        pop rax                 ; discard saved ny
.cam_dx_next:
        inc ebx
        jmp .cam_dx
.cam_dy_next:
        inc ecx
        jmp .cam_dy
.cam_done:
        mov eax, [cam_cnt]
        mov dword [cam_cnt], 0

        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

; -------------------------------------------------------------------
; reveal_cell - Reveal cell at cursor
; -------------------------------------------------------------------
reveal_cell:
        PUSHALL
        movzx eax, byte [cursor_y]
        movzx ebx, byte [grid_w]
        imul eax, ebx
        movzx ebx, byte [cursor_x]
        add eax, ebx

        ; Already revealed or flagged?
        test byte [board + eax], CELL_REVEALED
        jnz .rc_done
        test byte [board + eax], CELL_FLAGGED
        jnz .rc_done

        ; Mine?
        test byte [board + eax], CELL_MINE
        jnz .rc_boom

        ; Reveal using flood fill
        movzx ecx, byte [cursor_x]
        movzx edx, byte [cursor_y]
        call flood_reveal

        ; Check win
        call check_win
        jmp .rc_done

.rc_boom:
        mov byte [game_over], 1
        mov byte [game_won], 0
        ; Reveal all mines
        call reveal_all_mines

.rc_done:
        POPALL
        ret

; -------------------------------------------------------------------
; flood_reveal - Recursively reveal empty cells from (ECX=x, EDX=y)
; -------------------------------------------------------------------
flood_reveal:
        ; Bounds check
        cmp ecx, 0
        jl .fr_ret
        movzx eax, byte [grid_w]
        cmp ecx, eax
        jge .fr_ret
        cmp edx, 0
        jl .fr_ret
        movzx eax, byte [grid_h]
        cmp edx, eax
        jge .fr_ret

        ; Calculate index
        movzx eax, byte [grid_w]
        imul eax, edx
        add eax, ecx

        ; Already revealed?
        test byte [board + eax], CELL_REVEALED
        jnz .fr_ret
        ; Mine?
        test byte [board + eax], CELL_MINE
        jnz .fr_ret

        ; Reveal it
        or byte [board + eax], CELL_REVEALED
        and byte [board + eax], ~CELL_FLAGGED
        inc dword [cells_revealed]

        ; If count > 0, stop recursion
        mov al, [board + eax]
        and al, CELL_COUNT_MASK
        test al, al
        jnz .fr_ret

        ; Count is 0: flood-fill neighbors
        push rcx
        push rdx
        dec ecx
        call flood_reveal       ; left
        add ecx, 2
        call flood_reveal       ; right
        dec ecx
        dec edx
        call flood_reveal       ; up
        add edx, 2
        call flood_reveal       ; down
        dec edx
        dec ecx
        dec edx
        call flood_reveal       ; upper-left
        add ecx, 2
        call flood_reveal       ; upper-right
        sub ecx, 2
        add edx, 2
        call flood_reveal       ; lower-left
        add ecx, 2
        call flood_reveal       ; lower-right
        pop rdx
        pop rcx

.fr_ret:
        ret

; -------------------------------------------------------------------
; toggle_flag - Toggle flag on cell at cursor
; -------------------------------------------------------------------
toggle_flag:
        PUSHALL
        movzx eax, byte [cursor_y]
        movzx ebx, byte [grid_w]
        imul eax, ebx
        movzx ebx, byte [cursor_x]
        add eax, ebx

        test byte [board + eax], CELL_REVEALED
        jnz .tf_done
        xor byte [board + eax], CELL_FLAGGED
.tf_done:
        POPALL
        ret

; -------------------------------------------------------------------
; reveal_all_mines - Show all mines (game over)
; -------------------------------------------------------------------
reveal_all_mines:
        PUSHALL
        movzx ecx, byte [grid_w]
        movzx eax, byte [grid_h]
        imul ecx, eax
        xor ebx, ebx
.ram_loop:
        test ecx, ecx
        jz .ram_done
        test byte [board + ebx], CELL_MINE
        jz .ram_next
        or byte [board + ebx], CELL_REVEALED
.ram_next:
        inc ebx
        dec ecx
        jmp .ram_loop
.ram_done:
        POPALL
        ret

; -------------------------------------------------------------------
; check_win - Check if all non-mine cells are revealed
; -------------------------------------------------------------------
check_win:
        PUSHALL
        movzx eax, byte [grid_w]
        movzx ebx, byte [grid_h]
        imul eax, ebx
        movzx ebx, byte [mine_count]
        sub eax, ebx            ; total safe cells
        cmp [cells_revealed], eax
        jne .cw_no
        mov byte [game_over], 1
        mov byte [game_won], 1
.cw_no:
        POPALL
        ret

; -------------------------------------------------------------------
; draw_screen - Render the game
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

        ; Draw grid
        xor ebp, ebp            ; row
.ds_row:
        movzx eax, byte [grid_h]
        cmp ebp, eax
        jge .ds_footer

        xor ecx, ecx            ; col
.ds_col:
        movzx eax, byte [grid_w]
        cmp ecx, eax
        jge .ds_row_end

        ; Calculate cell index
        movzx eax, byte [grid_w]
        imul eax, ebp
        add eax, ecx

        ; Determine what to draw
        push rcx
        push rbp
        push rax

        ; Is this the cursor?
        cmp cl, [cursor_x]
        jne .ds_not_cursor
        cmp byte [cursor_y], 0
        je .ds_maybe_cur_r0
        movzx edx, byte [cursor_y]
        cmp ebp, edx
        je .ds_is_cursor
        jmp .ds_not_cursor
.ds_maybe_cur_r0:
        test ebp, ebp
        jnz .ds_not_cursor
.ds_is_cursor:
        cmp byte [game_over], 0
        jne .ds_not_cursor       ; no cursor highlight when game over
        mov eax, SYS_SETCOLOR
        mov ebx, C_CURSOR
        int 0x80
        jmp .ds_draw_cell
.ds_not_cursor:
        pop rax
        push rax

        ; Revealed?
        test byte [board + eax], CELL_REVEALED
        jnz .ds_revealed

        ; Flagged?
        test byte [board + eax], CELL_FLAGGED
        jnz .ds_flagged

        ; Hidden
        mov eax, SYS_SETCOLOR
        mov ebx, C_HIDDEN
        int 0x80
        jmp .ds_draw_cell

.ds_flagged:
        mov eax, SYS_SETCOLOR
        mov ebx, C_FLAG
        int 0x80
        jmp .ds_draw_cell

.ds_revealed:
        ; Mine or number?
        test byte [board + eax], CELL_MINE
        jnz .ds_mine

        ; Number — pick color
        mov dl, [board + eax]
        and dl, CELL_COUNT_MASK
        cmp dl, 1
        je .ds_c1
        cmp dl, 2
        je .ds_c2
        cmp dl, 3
        je .ds_c3
        cmp dl, 4
        je .ds_c4
        ; 0 or 5+: default
        mov eax, SYS_SETCOLOR
        mov ebx, C_REVEAL
        int 0x80
        jmp .ds_draw_cell
.ds_c1:
        mov eax, SYS_SETCOLOR
        mov ebx, C_NUM1
        int 0x80
        jmp .ds_draw_cell
.ds_c2:
        mov eax, SYS_SETCOLOR
        mov ebx, C_NUM2
        int 0x80
        jmp .ds_draw_cell
.ds_c3:
        mov eax, SYS_SETCOLOR
        mov ebx, C_NUM3
        int 0x80
        jmp .ds_draw_cell
.ds_c4:
        mov eax, SYS_SETCOLOR
        mov ebx, C_NUM4
        int 0x80
        jmp .ds_draw_cell

.ds_mine:
        mov eax, SYS_SETCOLOR
        mov ebx, C_MINE
        int 0x80

.ds_draw_cell:
        pop rax
        ; What character?
        test byte [board + eax], CELL_REVEALED
        jnz .ds_show_revealed
        test byte [board + eax], CELL_FLAGGED
        jnz .ds_show_flag
        ; Hidden
        mov eax, SYS_PUTCHAR
        mov ebx, '#'
        int 0x80
        jmp .ds_cell_done

.ds_show_flag:
        mov eax, SYS_PUTCHAR
        mov ebx, 'F'
        int 0x80
        jmp .ds_cell_done

.ds_show_revealed:
        test byte [board + eax], CELL_MINE
        jnz .ds_show_mine
        mov dl, [board + eax]
        and dl, CELL_COUNT_MASK
        test dl, dl
        jz .ds_show_empty
        add dl, '0'
        movzx ebx, dl
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .ds_cell_done
.ds_show_empty:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        jmp .ds_cell_done
.ds_show_mine:
        mov eax, SYS_PUTCHAR
        mov ebx, '*'
        int 0x80

.ds_cell_done:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80

        pop rbp
        pop rcx
        inc ecx
        jmp .ds_col

.ds_row_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        inc ebp
        jmp .ds_row

.ds_footer:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80

        ; Status
        cmp byte [game_over], 0
        je .ds_playing
        cmp byte [game_won], 1
        je .ds_won
        mov eax, SYS_PRINT
        mov ebx, msg_lost
        int 0x80
        jmp .ds_help
.ds_won:
        mov eax, SYS_SETCOLOR
        mov ebx, C_OK
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_won
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DEFAULT
        int 0x80
        jmp .ds_help
.ds_playing:
        mov eax, SYS_PRINT
        mov ebx, msg_mines
        int 0x80
        movzx eax, byte [mine_count]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

.ds_help:
        mov eax, SYS_PRINT
        mov ebx, msg_help
        int 0x80

        POPALL
        ret

; -------------------------------------------------------------------
; RNG (LCG)
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
msg_title:  db " MINESWEEPER", 0x0A, 0
msg_mines:  db "Mines: ", 0
msg_lost:   db "BOOM! Game over.", 0x0A, 0
msg_won:    db "You win!", 0x0A, 0
msg_help:   db "Arrows:move Space:reveal F:flag R:restart Q:quit", 0x0A, 0

; -------------------------------------------------------------------
; BSS
; -------------------------------------------------------------------
grid_w:         db 0
grid_h:         db 0
mine_count:     db 0
cursor_x:       db 0
cursor_y:       db 0
game_over:      db 0
game_won:       db 0
cells_revealed: dd 0
rng_state:      dd 0
cam_cnt:        dd 0
arg_buf:        times 32 db 0
board:          times MAX_CELLS db 0
