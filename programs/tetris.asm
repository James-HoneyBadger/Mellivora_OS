; tetris.asm - Tetris game for Mellivora OS
; Classic falling block puzzle with 7 tetrominoes, rotation, scoring
; Controls: Left/Right=Move, Up=Rotate, Down=Drop, ESC=Quit
%include "syscalls.inc"

%if 0

; Board dimensions
BOARD_W         equ 10
BOARD_H         equ 20
BOARD_SIZE      equ BOARD_W * BOARD_H

; Display offsets (center board on 80x25 screen)
DRAW_X          equ 30          ; left edge of board on screen
DRAW_Y          equ 2           ; top edge of board on screen
NEXT_X          equ 55          ; "Next" preview position
NEXT_Y          equ 4

; Timing
DROP_DELAY      equ 40          ; ticks between automatic drops (100Hz, so ~0.4s)
FAST_DROP_DELAY equ 3           ; ticks when holding down

; Piece data: each piece has 4 rotations, each rotation is 4 (x,y) pairs
; Pieces: I, O, T, S, Z, L, J
NUM_PIECES      equ 7

; Colors for each piece type (1-based, 0=empty)
piece_colors:
        db 0x0B                 ; I = Cyan
        db 0x0E                 ; O = Yellow
        db 0x0D                 ; T = Magenta
        db 0x0A                 ; S = Green
        db 0x0C                 ; Z = Red
        db 0x06                 ; L = Brown/Orange
        db 0x09                 ; J = Blue

; Piece rotation data: 4 rotations x 4 blocks x 2 coords (x,y) = 32 bytes per piece
; Coordinates relative to pivot point

; I piece
piece_I:
        db 0,1, 1,1, 2,1, 3,1  ; rotation 0 (horizontal)
        db 2,0, 2,1, 2,2, 2,3  ; rotation 1 (vertical)
        db 0,2, 1,2, 2,2, 3,2  ; rotation 2
        db 1,0, 1,1, 1,2, 1,3  ; rotation 3

; O piece (no real rotation)
piece_O:
        db 1,0, 2,0, 1,1, 2,1
        db 1,0, 2,0, 1,1, 2,1
        db 1,0, 2,0, 1,1, 2,1
        db 1,0, 2,0, 1,1, 2,1

; T piece
piece_T:
        db 0,1, 1,1, 2,1, 1,0
        db 1,0, 1,1, 1,2, 2,1
        db 0,1, 1,1, 2,1, 1,2
        db 1,0, 1,1, 1,2, 0,1

; S piece
piece_S:
        db 1,0, 2,0, 0,1, 1,1
        db 1,0, 1,1, 2,1, 2,2
        db 1,1, 2,1, 0,2, 1,2
        db 0,0, 0,1, 1,1, 1,2

; Z piece
piece_Z:
        db 0,0, 1,0, 1,1, 2,1
        db 2,0, 1,1, 2,1, 1,2
        db 0,1, 1,1, 1,2, 2,2
        db 1,0, 0,1, 1,1, 0,2

; L piece
piece_L:
        db 0,1, 1,1, 2,1, 2,0
        db 1,0, 1,1, 1,2, 2,2
        db 0,1, 1,1, 2,1, 0,2
        db 0,0, 1,0, 1,1, 1,2

; J piece
piece_J:
        db 0,0, 0,1, 1,1, 2,1
        db 1,0, 2,0, 1,1, 1,2
        db 0,1, 1,1, 2,1, 2,2
        db 1,0, 1,1, 1,2, 0,2

; Table of piece data pointers
piece_table:
        dd piece_I, piece_O, piece_T, piece_S
        dd piece_Z, piece_L, piece_J

; Points per lines cleared
score_table:
        dd 0, 100, 300, 500, 800

start:
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; Init game state
        xor eax, eax
        mov [score], eax
        mov [level], eax
        mov [lines_cleared], eax
        mov [game_over], byte 0

        ; Clear board
        mov edi, board
        mov ecx, BOARD_SIZE
        xor al, al
        rep stosb

        ; Generate first two pieces
        call random_piece
        mov [current_piece], al
        call random_piece
        mov [next_piece], al

        ; Spawn first piece
        call spawn_piece

        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Draw static elements
        call draw_border
        call draw_sidebar

;=== Main game loop ===
game_loop:
        cmp byte [game_over], 0
        jne game_over_screen

        ; Draw everything
        call draw_board
        call draw_current_piece
        call draw_next_piece
        call draw_score

        ; Get current time
        mov eax, SYS_GETTIME
        int 0x80
        mov [frame_time], eax

        ; Wait for input or timeout
.input_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .check_timer

        ; Process input
        cmp eax, KEY_LEFT
        je .move_left
        cmp eax, KEY_RIGHT
        je .move_right
        cmp eax, KEY_UP
        je .rotate
        cmp eax, KEY_DOWN
        je .soft_drop
        cmp eax, ' '
        je .hard_drop
        cmp eax, 0x1B           ; ESC
        je .quit
        cmp eax, 'p'
        je .pause
        cmp eax, 'P'
        je .pause
        jmp .check_timer

.move_left:
        dec dword [piece_x]
        call check_collision
        test eax, eax
        jz .input_done
        inc dword [piece_x]
        jmp .input_done

.move_right:
        inc dword [piece_x]
        call check_collision
        test eax, eax
        jz .input_done
        dec dword [piece_x]
        jmp .input_done

.rotate:
        mov eax, [piece_rot]
        push eax                ; save old rotation
        inc eax
        and eax, 3
        mov [piece_rot], eax
        call check_collision
        test eax, eax
        jz .rot_ok
        pop eax
        mov [piece_rot], eax    ; restore old rotation
        jmp .input_done
.rot_ok:
        add esp, 4              ; discard saved rotation
        jmp .input_done

.soft_drop:
        inc dword [piece_y]
        call check_collision
        test eax, eax
        jz .drop_ok
        dec dword [piece_y]
        call lock_piece
        jmp .input_done
.drop_ok:
        add dword [score], 1
        jmp .input_done

.hard_drop:
        ; Drop piece all the way down
.hard_loop:
        inc dword [piece_y]
        call check_collision
        test eax, eax
        jz .hard_cont
        dec dword [piece_y]
        call lock_piece
        jmp .input_done
.hard_cont:
        add dword [score], 2
        jmp .hard_loop

.pause:
        ; Show pause message
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 1
        mov ecx, 12
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_paused
        int 0x80
        ; Wait for another keypress
        mov eax, SYS_GETCHAR
        int 0x80
        jmp .input_done

.input_done:
        jmp game_loop

.check_timer:
        ; Check if it's time for automatic drop
        mov eax, SYS_GETTIME
        int 0x80
        sub eax, [frame_time]
        mov ebx, [drop_delay]
        cmp eax, ebx
        jl .input_loop

        ; Auto drop
        inc dword [piece_y]
        call check_collision
        test eax, eax
        jz game_loop
        dec dword [piece_y]
        call lock_piece
        jmp game_loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=== Spawn a new piece at top ===
spawn_piece:
        pushad
        movzx eax, byte [current_piece]
        mov [piece_type], eax
        mov dword [piece_x], 3
        mov dword [piece_y], 0
        mov dword [piece_rot], 0

        ; Calculate drop delay based on level
        mov eax, DROP_DELAY
        mov ebx, [level]
        cmp ebx, 10
        jle .calc_delay
        mov ebx, 10
.calc_delay:
        imul ebx, 3
        sub eax, ebx
        cmp eax, 5
        jge .set_delay
        mov eax, 5
.set_delay:
        mov [drop_delay], eax

        ; Move next to current, generate new next
        mov al, [next_piece]
        mov [current_piece], al
        call random_piece
        mov [next_piece], al

        ; Check if spawn position is valid
        call check_collision
        test eax, eax
        jz .spawn_ok
        mov byte [game_over], 1
.spawn_ok:
        popad
        ret

;=== Check collision: returns 0 if no collision, 1 if collision ===
check_collision:
        push ebx
        push ecx
        push edx
        push esi

        ; Get piece data pointer
        mov eax, [piece_type]
        mov esi, [piece_table + eax * 4]
        mov eax, [piece_rot]
        shl eax, 3              ; 8 bytes per rotation
        add esi, eax

        ; Check each of 4 blocks
        xor ecx, ecx
.cc_loop:
        cmp ecx, 4
        jge .cc_ok

        movzx eax, byte [esi]   ; relative x
        add eax, [piece_x]
        movzx ebx, byte [esi+1] ; relative y
        add ebx, [piece_y]

        ; Check bounds
        cmp eax, 0
        jl .cc_fail
        cmp eax, BOARD_W
        jge .cc_fail
        cmp ebx, 0
        jl .cc_fail
        cmp ebx, BOARD_H
        jge .cc_fail

        ; Check board cell
        imul edx, ebx, BOARD_W
        add edx, eax
        cmp byte [board + edx], 0
        jne .cc_fail

        add esi, 2
        inc ecx
        jmp .cc_loop

.cc_ok:
        xor eax, eax           ; no collision
        jmp .cc_done
.cc_fail:
        mov eax, 1              ; collision
.cc_done:
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;=== Lock current piece onto board ===
lock_piece:
        pushad
        ; Get piece data pointer
        mov eax, [piece_type]
        mov esi, [piece_table + eax * 4]
        mov eax, [piece_rot]
        shl eax, 3
        add esi, eax

        ; Get piece color
        mov eax, [piece_type]
        movzx edi, byte [piece_colors + eax]
        inc edi                 ; color + 1 (0 = empty)

        ; Place each block
        xor ecx, ecx
.lp_loop:
        cmp ecx, 4
        jge .lp_check_lines

        movzx eax, byte [esi]
        add eax, [piece_x]
        movzx ebx, byte [esi+1]
        add ebx, [piece_y]

        ; Bounds check (should be valid from collision check)
        cmp ebx, BOARD_H
        jge .lp_next
        cmp eax, BOARD_W
        jge .lp_next

        imul edx, ebx, BOARD_W
        add edx, eax
        mov [board + edx], dl   ; store color (non-zero = occupied)
        ; Actually store the piece color
        mov byte [board + edx], 0xFF ; mark as occupied
        ; Better: store actual color index
        push edi
        pop eax
        mov [board + edx], al

.lp_next:
        add esi, 2
        inc ecx
        jmp .lp_loop

.lp_check_lines:
        call check_clear_lines
        call spawn_piece
        popad
        ret

;=== Check and clear completed lines ===
check_clear_lines:
        pushad
        xor edi, edi            ; lines cleared this turn

        ; Check each row from bottom to top
        mov ebx, BOARD_H - 1
.cl_row:
        cmp ebx, 0
        jl .cl_score

        ; Check if row is full
        xor ecx, ecx
        mov eax, 1              ; assume full
.cl_check:
        cmp ecx, BOARD_W
        jge .cl_full_check
        imul edx, ebx, BOARD_W
        add edx, ecx
        cmp byte [board + edx], 0
        jne .cl_next_cell
        xor eax, eax            ; not full
        jmp .cl_full_check
.cl_next_cell:
        inc ecx
        jmp .cl_check

.cl_full_check:
        test eax, eax
        jz .cl_prev_row

        ; Row is full - clear it by shifting everything down
        inc edi                 ; count cleared lines
        push ebx
        call clear_row
        pop ebx
        ; Don't decrement ebx - re-check same row (rows shifted down)
        jmp .cl_row

.cl_prev_row:
        dec ebx
        jmp .cl_row

.cl_score:
        ; Update score based on lines cleared
        cmp edi, 0
        je .cl_done
        cmp edi, 4
        jle .cl_lookup
        mov edi, 4
.cl_lookup:
        mov eax, [score_table + edi * 4]
        ; Multiply by (level + 1)
        mov ebx, [level]
        inc ebx
        imul eax, ebx
        add [score], eax

        ; Update total lines and level
        add [lines_cleared], edi
        mov eax, [lines_cleared]
        xor edx, edx
        mov ebx, 10
        div ebx
        mov [level], eax

.cl_done:
        popad
        ret

;=== Clear row EBX by shifting rows above down ===
clear_row:
        pushad
        mov ecx, ebx           ; row to clear
.cr_shift:
        cmp ecx, 0
        jle .cr_top

        ; Copy row ecx-1 to row ecx
        mov edx, ecx
        dec edx                 ; source row
        push ecx
        xor ebx, ebx
.cr_copy:
        cmp ebx, BOARD_W
        jge .cr_next
        imul eax, edx, BOARD_W
        add eax, ebx
        movzx eax, byte [board + eax]
        imul edi, ecx, BOARD_W
        add edi, ebx
        mov [board + edi], al
        inc ebx
        jmp .cr_copy
.cr_next:
        pop ecx
        dec ecx
        jmp .cr_shift

.cr_top:
        ; Clear top row
        xor ebx, ebx
.cr_clear:
        cmp ebx, BOARD_W
        jge .cr_done
        mov byte [board + ebx], 0
        inc ebx
        jmp .cr_clear
.cr_done:
        popad
        ret

;=== Draw the board border ===
draw_border:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; dark gray
        int 0x80

        ; Top border
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X - 1
        mov ecx, DRAW_Y - 1
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xC9           ; top-left corner
        int 0x80
        mov ecx, BOARD_W * 2
.top_line:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0xCD           ; horizontal line
        int 0x80
        pop ecx
        dec ecx
        jnz .top_line
        mov eax, SYS_PUTCHAR
        mov ebx, 0xBB           ; top-right corner
        int 0x80

        ; Side borders
        xor edx, edx
.side_loop:
        cmp edx, BOARD_H
        jge .bottom

        ; Left border
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X - 1
        lea ecx, [edx + DRAW_Y]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xBA           ; vertical line
        int 0x80

        ; Right border
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + BOARD_W * 2
        lea ecx, [edx + DRAW_Y]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xBA
        int 0x80

        inc edx
        jmp .side_loop

.bottom:
        ; Bottom border
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X - 1
        mov ecx, DRAW_Y + BOARD_H
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xC8           ; bottom-left corner
        int 0x80
        mov ecx, BOARD_W * 2
.bot_line:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0xCD
        int 0x80
        pop ecx
        dec ecx
        jnz .bot_line
        mov eax, SYS_PUTCHAR
        mov ebx, 0xBC           ; bottom-right corner
        int 0x80

        popad
        ret

;=== Draw the board contents ===
draw_board:
        pushad
        xor edx, edx           ; row
.db_row:
        cmp edx, BOARD_H
        jge .db_done
        xor ecx, ecx           ; col

        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X
        push ecx
        lea ecx, [edx + DRAW_Y]
        int 0x80
        pop ecx

.db_col:
        cmp ecx, BOARD_W
        jge .db_next_row

        ; Get cell value
        imul eax, edx, BOARD_W
        add eax, ecx
        movzx eax, byte [board + eax]

        test al, al
        jz .db_empty

        ; Occupied cell - draw colored block
        push ecx
        push edx
        movzx ebx, al
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB           ; full block
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        pop edx
        pop ecx
        jmp .db_next_col

.db_empty:
        ; Empty cell
        push ecx
        push edx
        mov eax, SYS_SETCOLOR
        mov ebx, 0x00           ; black on black
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop edx
        pop ecx

.db_next_col:
        inc ecx
        jmp .db_col

.db_next_row:
        inc edx
        jmp .db_row

.db_done:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        popad
        ret

;=== Draw current falling piece ===
draw_current_piece:
        pushad
        mov eax, [piece_type]
        mov esi, [piece_table + eax * 4]
        mov eax, [piece_rot]
        shl eax, 3
        add esi, eax

        ; Get piece color
        mov eax, [piece_type]
        movzx ebx, byte [piece_colors + eax]
        push ebx
        mov eax, SYS_SETCOLOR
        int 0x80
        pop ebx

        xor ecx, ecx
.dp_loop:
        cmp ecx, 4
        jge .dp_done

        movzx eax, byte [esi]   ; rel x
        add eax, [piece_x]
        movzx edx, byte [esi+1] ; rel y
        add edx, [piece_y]

        ; Skip if off-screen
        cmp edx, 0
        jl .dp_next
        cmp edx, BOARD_H
        jge .dp_next

        ; Set cursor
        push ecx
        push esi
        push eax
        mov eax, SYS_SETCURSOR
        pop eax
        push eax
        lea ebx, [eax * 2 + DRAW_X]
        lea ecx, [edx + DRAW_Y]
        int 0x80
        pop eax

        ; Draw block
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80

        pop esi
        pop ecx

.dp_next:
        add esi, 2
        inc ecx
        jmp .dp_loop

.dp_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        popad
        ret

;=== Draw next piece preview ===
draw_next_piece:
        pushad

        ; Clear preview area
        mov edx, 0
.np_clear:
        cmp edx, 4
        jge .np_draw
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        lea ecx, [edx + NEXT_Y]
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x00
        int 0x80
        mov ecx, 8
.np_clr:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ecx
        dec ecx
        jnz .np_clr
        inc edx
        jmp .np_clear

.np_draw:
        movzx eax, byte [next_piece]
        mov esi, [piece_table + eax * 4]
        ; Use rotation 0 for preview

        ; Get color
        movzx eax, byte [next_piece]
        movzx ebx, byte [piece_colors + eax]
        mov eax, SYS_SETCOLOR
        int 0x80

        xor ecx, ecx
.np_loop:
        cmp ecx, 4
        jge .np_done

        push ecx
        push esi

        ; Read coords
        movzx eax, byte [esi]     ; x
        movzx edx, byte [esi+1]   ; y

        ; Set cursor
        lea ebx, [eax * 2 + NEXT_X]
        lea ecx, [edx + NEXT_Y]
        mov eax, SYS_SETCURSOR
        int 0x80

        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80

        pop esi
        pop ecx
        add esi, 2
        inc ecx
        jmp .np_loop

.np_done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        popad
        ret

;=== Draw sidebar (score, level, lines, controls) ===
draw_sidebar:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        ; Title
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X
        mov ecx, 0
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; "NEXT" label
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, NEXT_Y - 1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_next
        int 0x80

        ; Controls
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 14
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 15
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 16
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 17
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls4
        int 0x80

        popad
        ret

;=== Draw score display ===
draw_score:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        ; Score
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [score]
        call print_dec
        ; Pad with spaces
        mov eax, SYS_PRINT
        mov ebx, msg_pad
        int 0x80

        ; Level
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_level
        int 0x80
        mov eax, [level]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_pad
        int 0x80

        ; Lines
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_lines
        int 0x80
        mov eax, [lines_cleared]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_pad
        int 0x80

        popad
        ret

;=== Game over screen ===
game_over_screen:
        ; Draw final board state
        call draw_board
        call draw_score

        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F           ; white on red
        int 0x80

        ; Draw game over message in center of board
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 2
        mov ecx, DRAW_Y + 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_gameover
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 1
        mov ecx, DRAW_Y + 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_final_score
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4E
        int 0x80
        mov eax, [score]
        call print_dec

        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 1
        mov ecx, DRAW_Y + 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80

        ; Wait for input
.go_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'r'
        je .go_restart
        cmp al, 'R'
        je .go_restart
        cmp al, 0x1B
        je .go_quit
        jmp .go_wait

.go_restart:
        ; Reset everything
        mov eax, SYS_CLEAR
        int 0x80
        jmp start

.go_quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=== Random piece generator (0-6) ===
random_piece:
        push ebx
        push ecx
        push edx
        ; Simple LCG random
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        xor edx, edx
        mov ebx, NUM_PIECES
        div ebx
        mov eax, edx            ; remainder = 0..6
        pop edx
        pop ecx
        pop ebx
        ret

;=== Data ===
msg_title:      db "T E T R I S", 0
msg_next:       db "NEXT:", 0
msg_score:      db "Score: ", 0
msg_level:      db "Level: ", 0
msg_lines:      db "Lines: ", 0
msg_pad:        db "      ", 0
msg_gameover:   db "  GAME OVER!  ", 0
msg_final_score: db " Final Score: ", 0
msg_restart:    db "R:Restart ESC:Quit", 0
msg_paused:     db " PAUSED ", 0
msg_controls1:  db "Left/Right:Move", 0
msg_controls2:  db "Up:Rotate", 0
msg_controls3:  db "Down:Soft Drop", 0
msg_controls4:  db "Space:Hard Drop", 0

;=== BSS ===
board:          times BOARD_SIZE db 0
piece_type:     dd 0
piece_x:        dd 0
piece_y:        dd 0
piece_rot:      dd 0
current_piece:  db 0
next_piece:     db 0
score:          dd 0
level:          dd 0
lines_cleared:  dd 0
drop_delay:     dd DROP_DELAY
game_over:      db 0
rand_seed:      dd 0
frame_time:     dd 0

%endif

; ============================================================================
; Fresh rewrite (v2)
; ============================================================================

; --- Dimensions/layout ---
BOARD_W         equ 10
BOARD_H         equ 20
BOARD_CELLS     equ BOARD_W * BOARD_H

DRAW_X          equ 26          ; board left (column)
DRAW_Y          equ 2           ; board top (row)
NEXT_X          equ 52          ; preview left
NEXT_Y          equ 5           ; preview top

DROP_BASE       equ 40          ; ticks at level 0
FRAME_SLEEP     equ 2           ; ticks per frame

; --- Piece colors (1..7 mapped to VGA attrs) ---
piece_colors:
        db 0x0B                 ; I cyan
        db 0x0E                 ; O yellow
        db 0x0D                 ; T magenta
        db 0x0A                 ; S green
        db 0x0C                 ; Z red
        db 0x06                 ; L brown
        db 0x09                 ; J blue

; --- 7 pieces, each: 4 rotations, each rotation 4 (x,y) pairs ---
piece_I:
        db 0,1, 1,1, 2,1, 3,1
        db 2,0, 2,1, 2,2, 2,3
        db 0,2, 1,2, 2,2, 3,2
        db 1,0, 1,1, 1,2, 1,3

piece_O:
        db 1,0, 2,0, 1,1, 2,1
        db 1,0, 2,0, 1,1, 2,1
        db 1,0, 2,0, 1,1, 2,1
        db 1,0, 2,0, 1,1, 2,1

piece_T:
        db 0,1, 1,1, 2,1, 1,0
        db 1,0, 1,1, 1,2, 2,1
        db 0,1, 1,1, 2,1, 1,2
        db 1,0, 1,1, 1,2, 0,1

piece_S:
        db 1,0, 2,0, 0,1, 1,1
        db 1,0, 1,1, 2,1, 2,2
        db 1,1, 2,1, 0,2, 1,2
        db 0,0, 0,1, 1,1, 1,2

piece_Z:
        db 0,0, 1,0, 1,1, 2,1
        db 2,0, 1,1, 2,1, 1,2
        db 0,1, 1,1, 1,2, 2,2
        db 1,0, 0,1, 1,1, 0,2

piece_L:
        db 0,1, 1,1, 2,1, 2,0
        db 1,0, 1,1, 1,2, 2,2
        db 0,1, 1,1, 2,1, 0,2
        db 0,0, 1,0, 1,1, 1,2

piece_J:
        db 0,0, 0,1, 1,1, 2,1
        db 1,0, 2,0, 1,1, 1,2
        db 0,1, 1,1, 2,1, 2,2
        db 1,0, 1,1, 1,2, 0,2

piece_table:
        dd piece_I, piece_O, piece_T, piece_S
        dd piece_Z, piece_L, piece_J

score_table:
        dd 0, 100, 300, 500, 800

; ---------------------------------------------------------------------------
; Entry
; ---------------------------------------------------------------------------
start:
        call init_game

main_loop:
        cmp byte [game_over], 0
        jne game_over_loop

        call handle_input
        call auto_drop
        call render_frame

        mov eax, SYS_SLEEP
        mov ebx, FRAME_SLEEP
        int 0x80
        jmp main_loop

; ---------------------------------------------------------------------------
; Init
; ---------------------------------------------------------------------------
init_game:
        pushad

        ; seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; reset state
        xor eax, eax
        mov [score], eax
        mov [lines_total], eax
        mov [level], eax
        mov [cur_type], eax
        mov [cur_rot], eax
        mov [cur_x], eax
        mov [cur_y], eax
        mov [next_type], eax
        mov [drop_delay], dword DROP_BASE
        mov byte [game_over], 0

        ; clear board
        mov edi, board
        mov ecx, BOARD_CELLS
        xor eax, eax
        rep stosb

        ; create next and spawn current
        call random_piece
        mov [next_type], eax
        call spawn_piece

        ; initial timer baseline
        mov eax, SYS_GETTIME
        int 0x80
        mov [last_drop_tick], eax

        popad
        ret

; ---------------------------------------------------------------------------
; Input
; ---------------------------------------------------------------------------
handle_input:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .done

        cmp eax, KEY_LEFT
        je .left
        cmp eax, KEY_RIGHT
        je .right
        cmp eax, KEY_UP
        je .rotate
        cmp eax, KEY_DOWN
        je .down
        cmp eax, ' '
        je .hard
        cmp eax, 'p'
        je .pause
        cmp eax, 'P'
        je .pause
        cmp eax, 0x1B
        je .quit
        jmp .done

.left:
        mov eax, [cur_x]
        dec eax
        mov ebx, [cur_y]
        call try_place_current
        test eax, eax
        jz .done
        dec dword [cur_x]
        jmp .done

.right:
        mov eax, [cur_x]
        inc eax
        mov ebx, [cur_y]
        call try_place_current
        test eax, eax
        jz .done
        inc dword [cur_x]
        jmp .done

.rotate:
        mov ecx, [cur_rot]
        inc ecx
        and ecx, 3

        ; in place
        mov eax, [cur_type]
        mov ebx, ecx
        mov edx, [cur_x]
        mov esi, [cur_y]
        call can_place
        test eax, eax
        jnz .rot_apply

        ; wall kick left
        mov eax, [cur_type]
        mov ebx, ecx
        mov edx, [cur_x]
        dec edx
        mov esi, [cur_y]
        call can_place
        test eax, eax
        jz .rot_kick_right
        mov [cur_rot], ecx
        dec dword [cur_x]
        jmp .done

.rot_kick_right:
        mov eax, [cur_type]
        mov ebx, ecx
        mov edx, [cur_x]
        inc edx
        mov esi, [cur_y]
        call can_place
        test eax, eax
        jz .done
        mov [cur_rot], ecx
        inc dword [cur_x]
        jmp .done

.rot_apply:
        mov [cur_rot], ecx
        jmp .done

.down:
        mov eax, [cur_x]
        mov ebx, [cur_y]
        inc ebx
        call try_place_current
        test eax, eax
        jz .down_lock
        inc dword [cur_y]
        inc dword [score]
        jmp .done

.down_lock:
        call lock_piece
        jmp .done

.hard:
.hard_loop:
        mov eax, [cur_x]
        mov ebx, [cur_y]
        inc ebx
        call try_place_current
        test eax, eax
        jz .hard_lock
        inc dword [cur_y]
        add dword [score], 2
        jmp .hard_loop
.hard_lock:
        call lock_piece
        jmp .done

.pause:
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 1
        mov ecx, DRAW_Y + BOARD_H + 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_paused
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        jmp .done

.quit:
        jmp exit_program

.done:
        ret

; ---------------------------------------------------------------------------
; Drop timer
; ---------------------------------------------------------------------------
auto_drop:
        push ebx
        mov eax, SYS_GETTIME
        int 0x80
        mov ebx, [last_drop_tick]
        sub eax, ebx
        cmp eax, [drop_delay]
        jl .ad_done

        mov eax, SYS_GETTIME
        int 0x80
        mov [last_drop_tick], eax

        mov eax, [cur_x]
        mov ebx, [cur_y]
        inc ebx
        call try_place_current
        test eax, eax
        jz .lock
        inc dword [cur_y]
        jmp .ad_done

.lock:
        call lock_piece

.ad_done:
        pop ebx
        ret

; ---------------------------------------------------------------------------
; Piece/board logic
; ---------------------------------------------------------------------------

; try_place_current
; in: EAX=new_x, EBX=new_y
; out: EAX=1 if valid else 0
try_place_current:
        push edx
        push esi
        mov edx, eax            ; x
        mov esi, ebx            ; y
        mov eax, [cur_type]
        mov ebx, [cur_rot]
        call can_place
        pop esi
        pop edx
        ret

; can_place
; in: EAX=piece type (0..6), EBX=rot (0..3), EDX=x, ESI=y
; out: EAX=1 placeable, 0 collision
can_place:
        push ebp
        push edi
        push ecx

        mov edi, [piece_table + eax * 4]
        and ebx, 3
        shl ebx, 3              ; 8 bytes per rotation
        add edi, ebx

        xor ecx, ecx
.cp_loop:
        cmp ecx, 4
        jge .cp_ok

        movzx eax, byte [edi]   ; rx
        add eax, edx            ; abs x
        cmp eax, 0
        jl .cp_fail
        cmp eax, BOARD_W
        jge .cp_fail

        movzx ebx, byte [edi + 1] ; ry
        add ebx, esi              ; abs y
        cmp ebx, 0
        jl .cp_fail
        cmp ebx, BOARD_H
        jge .cp_fail

        imul ebp, ebx, BOARD_W
        add ebp, eax
        cmp byte [board + ebp], 0
        jne .cp_fail

        add edi, 2
        inc ecx
        jmp .cp_loop

.cp_ok:
        mov eax, 1
        jmp .cp_done

.cp_fail:
        xor eax, eax

.cp_done:
        pop ecx
        pop edi
        pop ebp
        ret

spawn_piece:
        pushad
        mov eax, [next_type]
        mov [cur_type], eax
        mov dword [cur_rot], 0
        mov dword [cur_x], 3
        mov dword [cur_y], 0

        call random_piece
        mov [next_type], eax

        ; check spawn validity
        mov eax, [cur_type]
        mov ebx, [cur_rot]
        mov edx, [cur_x]
        mov esi, [cur_y]
        call can_place
        test eax, eax
        jnz .sp_ok
        mov byte [game_over], 1
.sp_ok:
        popad
        ret

lock_piece:
        pushad

        ; place 4 blocks into board
        mov eax, [cur_type]
        mov edi, [piece_table + eax * 4]
        mov eax, [cur_rot]
        and eax, 3
        shl eax, 3
        add edi, eax

        mov eax, [cur_type]
        inc eax                 ; cell value 1..7
        mov [lock_val], al

        xor ecx, ecx
.lp_loop:
        cmp ecx, 4
        jge .lp_after

        movzx eax, byte [edi]
        add eax, [cur_x]
        movzx ebx, byte [edi + 1]
        add ebx, [cur_y]

        imul edx, ebx, BOARD_W
        add edx, eax
        mov al, [lock_val]
        mov [board + edx], al

        add edi, 2
        inc ecx
        jmp .lp_loop

.lp_after:
        call clear_lines
        call update_speed
        call spawn_piece

        ; reset drop timer after lock/spawn
        mov eax, SYS_GETTIME
        int 0x80
        mov [last_drop_tick], eax

        popad
        ret

clear_lines:
        pushad
        xor edi, edi            ; cleared count this lock

        mov ebx, BOARD_H - 1    ; y
.cl_row:
        cmp ebx, 0
        jl .cl_score

        ; test if row full
        xor ecx, ecx
        mov eax, 1
.cl_test:
        cmp ecx, BOARD_W
        jge .cl_checked
        imul edx, ebx, BOARD_W
        add edx, ecx
        cmp byte [board + edx], 0
        jne .cl_next_cell
        xor eax, eax
        jmp .cl_checked
.cl_next_cell:
        inc ecx
        jmp .cl_test

.cl_checked:
        test eax, eax
        jz .cl_prev

        ; full -> shift everything above down
        inc edi
        push ebx
        call shift_down_from_row
        pop ebx
        jmp .cl_row             ; re-check same row index

.cl_prev:
        dec ebx
        jmp .cl_row

.cl_score:
        test edi, edi
        jz .cl_done
        cmp edi, 4
        jle .cl_ok_count
        mov edi, 4
.cl_ok_count:
        mov eax, [score_table + edi * 4]
        mov ebx, [level]
        inc ebx
        imul eax, ebx
        add [score], eax

        add [lines_total], edi
        mov eax, [lines_total]
        xor edx, edx
        mov ebx, 10
        div ebx
        mov [level], eax

.cl_done:
        popad
        ret

; shift_down_from_row
; in: EBX = destination row to fill from rows above
shift_down_from_row:
        pushad
        mov ecx, ebx            ; current row
.sd_rows:
        cmp ecx, 0
        jle .sd_clear_top

        mov edx, ecx
        dec edx                 ; src row
        xor esi, esi            ; x
.sd_cols:
        cmp esi, BOARD_W
        jge .sd_next_row

        imul eax, edx, BOARD_W
        add eax, esi
        mov al, [board + eax]

        imul edi, ecx, BOARD_W
        add edi, esi
        mov [board + edi], al

        inc esi
        jmp .sd_cols

.sd_next_row:
        dec ecx
        jmp .sd_rows

.sd_clear_top:
        xor esi, esi
.sd_clear_loop:
        cmp esi, BOARD_W
        jge .sd_done
        mov byte [board + esi], 0
        inc esi
        jmp .sd_clear_loop

.sd_done:
        popad
        ret

update_speed:
        push eax
        push ebx
        mov eax, DROP_BASE
        mov ebx, [level]
        cmp ebx, 10
        jle .us_lvl_ok
        mov ebx, 10
.us_lvl_ok:
        imul ebx, 3
        sub eax, ebx
        cmp eax, 5
        jge .us_store
        mov eax, 5
.us_store:
        mov [drop_delay], eax
        pop ebx
        pop eax
        ret

random_piece:
        push ebx
        push edx
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        xor edx, edx
        mov ebx, 7
        div ebx
        mov eax, edx            ; 0..6
        pop edx
        pop ebx
        ret

; ---------------------------------------------------------------------------
; Rendering
; ---------------------------------------------------------------------------
render_frame:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        call draw_border
        call draw_board
        call draw_current_piece
        call draw_next_piece
        call draw_ui

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        popad
        ret

draw_border:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80

        ; top
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X - 1
        mov ecx, DRAW_Y - 1
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '+'
        int 0x80

        mov ecx, BOARD_W * 2
.db_top_loop:
        cmp ecx, 0
        je .db_top_end
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop ecx
        dec ecx
        jmp .db_top_loop
.db_top_end:
        mov eax, SYS_PUTCHAR
        mov ebx, '+'
        int 0x80

        ; sides
        xor edx, edx
.db_side_loop:
        cmp edx, BOARD_H
        jge .db_bottom

        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X - 1
        lea ecx, [edx + DRAW_Y]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + BOARD_W * 2
        lea ecx, [edx + DRAW_Y]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '|'
        int 0x80

        inc edx
        jmp .db_side_loop

.db_bottom:
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X - 1
        mov ecx, DRAW_Y + BOARD_H
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '+'
        int 0x80

        mov ecx, BOARD_W * 2
.db_bot_loop:
        cmp ecx, 0
        je .db_bot_end
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        pop ecx
        dec ecx
        jmp .db_bot_loop
.db_bot_end:
        mov eax, SYS_PUTCHAR
        mov ebx, '+'
        int 0x80

        popad
        ret

draw_board:
        pushad
        xor edx, edx            ; y
.dbr_row:
        cmp edx, BOARD_H
        jge .dbr_done
        xor ecx, ecx            ; x
.dbr_col:
        cmp ecx, BOARD_W
        jge .dbr_next_row

        imul eax, edx, BOARD_W
        add eax, ecx
        movzx eax, byte [board + eax]
        mov [cell_tmp], eax

        ; cursor for this cell
        mov ebx, ecx
        shl ebx, 1
        add ebx, DRAW_X
        mov esi, edx
        add esi, DRAW_Y
        mov edi, ebx            ; keep col

        mov ebx, edi
        push ecx
        mov ecx, esi
        mov eax, SYS_SETCURSOR
        int 0x80
        pop ecx

        mov eax, [cell_tmp]
        test eax, eax
        jz .dbr_empty

        mov eax, [cell_tmp]
        dec eax                 ; 0..6 index
        movzx ebx, byte [piece_colors + eax]
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        jmp .dbr_next_col

.dbr_empty:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

.dbr_next_col:
        inc ecx
        jmp .dbr_col

.dbr_next_row:
        inc edx
        jmp .dbr_row

.dbr_done:
        popad
        ret

draw_current_piece:
        pushad
        mov eax, [cur_type]
        mov edi, [piece_table + eax * 4]
        mov eax, [cur_rot]
        and eax, 3
        shl eax, 3
        add edi, eax

        mov eax, [cur_type]
        movzx ebp, byte [piece_colors + eax]

        xor ecx, ecx
.dcp_loop:
        cmp ecx, 4
        jge .dcp_done

        movzx eax, byte [edi]
        add eax, [cur_x]
        movzx edx, byte [edi + 1]
        add edx, [cur_y]

        cmp edx, 0
        jl .dcp_next
        cmp edx, BOARD_H
        jge .dcp_next

        ; cursor
        mov ebx, eax
        shl ebx, 1
        add ebx, DRAW_X
        add edx, DRAW_Y
        push ecx
        mov ecx, edx
        mov eax, SYS_SETCURSOR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, ebp
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        pop ecx

.dcp_next:
        add edi, 2
        inc ecx
        jmp .dcp_loop

.dcp_done:
        popad
        ret

draw_next_piece:
        pushad

        ; clear preview box 8x4
        xor edx, edx
.dnp_clr_row:
        cmp edx, 4
        jge .dnp_draw
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        lea ecx, [edx + NEXT_Y]
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov ecx, 8
.dnp_clr_col:
        cmp ecx, 0
        je .dnp_next_clr_row
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ecx
        dec ecx
        jmp .dnp_clr_col
.dnp_next_clr_row:
        inc edx
        jmp .dnp_clr_row

.dnp_draw:
        mov eax, [next_type]
        mov edi, [piece_table + eax * 4] ; rot 0
        mov eax, [next_type]
        movzx ebp, byte [piece_colors + eax]

        xor ecx, ecx
.dnp_loop:
        cmp ecx, 4
        jge .dnp_done
        push ecx
        movzx eax, byte [edi]
        movzx edx, byte [edi + 1]

        lea ebx, [eax * 2 + NEXT_X]
        lea ecx, [edx + NEXT_Y]
        mov eax, SYS_SETCURSOR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, ebp
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80

        pop ecx
        add edi, 2
        inc ecx
        jmp .dnp_loop

.dnp_done:
        popad
        ret

draw_ui:
        pushad

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X
        mov ecx, 0
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_title
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, NEXT_Y - 1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_next
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_score
        int 0x80
        mov eax, [score]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, ui_pad
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_level
        int 0x80
        mov eax, [level]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, ui_pad
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_lines
        int 0x80
        mov eax, [lines_total]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, ui_pad
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 15
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_ctrl1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 16
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_ctrl2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 17
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_ctrl3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, NEXT_X
        mov ecx, 18
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, ui_ctrl4
        int 0x80

        popad
        ret

; ---------------------------------------------------------------------------
; Game over / exit
; ---------------------------------------------------------------------------
game_over_loop:
        call render_frame

        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 2
        mov ecx, DRAW_Y + 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_game_over
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, DRAW_X + 1
        mov ecx, DRAW_Y + 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80

.go_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'r'
        je .go_restart
        cmp al, 'R'
        je .go_restart
        cmp al, 0x1B
        je exit_program
        jmp .go_wait

.go_restart:
        call init_game
        jmp main_loop

exit_program:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---------------------------------------------------------------------------
; Data/state
; ---------------------------------------------------------------------------
ui_title:       db "TETRIS", 0
ui_next:        db "NEXT:", 0
ui_score:       db "Score: ", 0
ui_level:       db "Level: ", 0
ui_lines:       db "Lines: ", 0
ui_pad:         db "      ", 0
ui_ctrl1:       db "Left/Right: Move", 0
ui_ctrl2:       db "Up: Rotate", 0
ui_ctrl3:       db "Down: Soft drop", 0
ui_ctrl4:       db "Space: Hard drop", 0
msg_paused:     db "PAUSED (any key)", 0
msg_game_over:  db "   GAME OVER   ", 0
msg_restart:    db "R:Restart ESC:Quit", 0

board:          times BOARD_CELLS db 0

cur_type:       dd 0
cur_rot:        dd 0
cur_x:          dd 0
cur_y:          dd 0
next_type:      dd 0

score:          dd 0
lines_total:    dd 0
level:          dd 0
drop_delay:     dd DROP_BASE
last_drop_tick: dd 0
rand_seed:      dd 0

game_over:      db 0
lock_val:       db 0
cell_tmp:       dd 0
