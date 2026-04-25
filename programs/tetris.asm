; tetris.asm - Tetris — VBE pixel graphics
%include "syscalls.inc"

; ─── Board / layout ─────────────────────────────────────────────────
BOARD_W         equ 10
BOARD_H         equ 20
BOARD_CELLS     equ BOARD_W * BOARD_H

CELL            equ 20          ; px per board cell
BOARD_PX_X      equ 120         ; board left pixel
BOARD_PX_Y      equ 40          ; board top pixel

NEXT_PX_X       equ 380         ; next-piece preview top-left
NEXT_PX_Y       equ 80

UI_X            equ 370         ; score / controls panel text x
UI_Y_SCORE      equ 200
UI_Y_LEVEL      equ 224
UI_Y_LINES      equ 248
UI_Y_CTRL       equ 300

DROP_BASE       equ 40
FRAME_SLEEP     equ 2

; ─── Key codes ──────────────────────────────────────────────────────
KEY_UP          equ 0x80
KEY_DOWN        equ 0x81
KEY_LEFT        equ 0x82
KEY_RIGHT       equ 0x83

; ─── VBE ────────────────────────────────────────────────────────────
SCREEN_W        equ 640
SCREEN_H        equ 480

; ─── Colors ─────────────────────────────────────────────────────────
C_BG            equ 0x0A0A0A
C_BORDER        equ 0x888888
C_EMPTY         equ 0x111111
C_TEXT          equ 0xFFFFFF
C_TITLE         equ 0xFFDD00
C_GRID          equ 0x222222
C_GOBOX         equ 0xAA0000
C_GOTEXT        equ 0xFFFFFF

; Piece colors index 0..6 (1-based cell values map: cell-1 = index)
; I=cyan, O=yellow, T=magenta, S=green, Z=red, L=orange, J=blue
piece_rgb:
        dd 0x00CCFF     ; 1 I cyan
        dd 0xFFFF00     ; 2 O yellow
        dd 0xFF00FF     ; 3 T magenta
        dd 0x00FF00     ; 4 S green
        dd 0xFF0000     ; 5 Z red
        dd 0xFF8800     ; 6 L orange
        dd 0x0044FF     ; 7 J blue

; ─── Piece data (kept identical to original v2) ─────────────────────
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

; ===========================================================================
; Entry
; ===========================================================================
start:
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCREEN_W
        mov edx, SCREEN_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je exit_program

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCREEN_W * 4

        call init_game

main_loop:
        cmp byte [game_over], 0
        jne game_over_loop

        call handle_input
        call auto_drop
        call render_frame

        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        jmp main_loop

; ===========================================================================
; init_game  (unchanged logic)
; ===========================================================================
init_game:
        pushad
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

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

        mov edi, board
        mov ecx, BOARD_CELLS
        xor eax, eax
        rep stosb

        call random_piece
        mov [next_type], eax
        call spawn_piece

        mov eax, SYS_GETTIME
        int 0x80
        mov [last_drop_tick], eax
        popad
        ret

; ===========================================================================
; handle_input  (unchanged logic, pause message uses VBE text now)
; ===========================================================================
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
        mov eax, [cur_type]
        mov ebx, ecx
        mov edx, [cur_x]
        mov esi, [cur_y]
        call can_place
        test eax, eax
        jnz .rot_apply
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
        ; Show PAUSED overlay via VBE text
        mov ebx, BOARD_PX_X + 20
        mov ecx, BOARD_PX_Y + BOARD_H * CELL / 2
        mov esi, msg_paused
        mov edi, 0xFFFF00
        call fb_draw_text
        mov eax, SYS_GETCHAR
        int 0x80
        jmp .done

.quit:
        jmp exit_program

.done:
        ret

; ===========================================================================
; auto_drop  (unchanged)
; ===========================================================================
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

; ===========================================================================
; try_place_current / can_place  (unchanged)
; ===========================================================================
try_place_current:
        push edx
        push esi
        mov edx, eax
        mov esi, ebx
        mov eax, [cur_type]
        mov ebx, [cur_rot]
        call can_place
        pop esi
        pop edx
        ret

can_place:
        push ebp
        push edi
        push ecx

        mov edi, [piece_table + eax * 4]
        and ebx, 3
        shl ebx, 3
        add edi, ebx

        xor ecx, ecx
.cp_loop:
        cmp ecx, 4
        jge .cp_ok

        movzx eax, byte [edi]
        add eax, edx
        cmp eax, 0
        jl .cp_fail
        cmp eax, BOARD_W
        jge .cp_fail

        movzx ebx, byte [edi + 1]
        add ebx, esi
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

; ===========================================================================
; spawn_piece  (unchanged)
; ===========================================================================
spawn_piece:
        pushad
        mov eax, [next_type]
        mov [cur_type], eax
        mov dword [cur_rot], 0
        mov dword [cur_x], 3
        mov dword [cur_y], 0

        call random_piece
        mov [next_type], eax

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

; ===========================================================================
; lock_piece  (unchanged)
; ===========================================================================
lock_piece:
        pushad
        mov eax, [cur_type]
        mov edi, [piece_table + eax * 4]
        mov eax, [cur_rot]
        and eax, 3
        shl eax, 3
        add edi, eax

        mov eax, [cur_type]
        inc eax
        mov [lock_val], al

        xor ecx, ecx
.lp_loop:
        cmp ecx, 4
        jge .lp_after

        movzx eax, byte [edi]
        add eax, [cur_x]
        movzx ebx, byte [edi + 1]
        add ebx, [cur_y]

        cmp eax, 0
        jl .lp_next
        cmp eax, BOARD_W
        jge .lp_next
        cmp ebx, 0
        jl .lp_next
        cmp ebx, BOARD_H
        jge .lp_next

        imul edx, ebx, BOARD_W
        add edx, eax
        mov al, [lock_val]
        mov [board + edx], al

.lp_next:
        add edi, 2
        inc ecx
        jmp .lp_loop

.lp_after:
        call clear_lines
        call update_speed
        call spawn_piece
        mov eax, SYS_GETTIME
        int 0x80
        mov [last_drop_tick], eax
        popad
        ret

; ===========================================================================
; clear_lines  (unchanged)
; ===========================================================================
clear_lines:
        pushad
        xor edi, edi

        mov ebx, BOARD_H - 1
.cl_row:
        cmp ebx, 0
        jl .cl_score
        xor ecx, ecx
        xor ebp, ebp
.cl_test:
        cmp ecx, BOARD_W
        jge .cl_checked
        imul edx, ebx, BOARD_W
        add edx, ecx
        cmp byte [board + edx], 0
        je .cl_next_cell
        inc ebp
.cl_next_cell:
        inc ecx
        jmp .cl_test
.cl_checked:
        cmp ebp, BOARD_W
        jne .cl_prev
        inc edi
        push ebx
        call shift_down_from_row
        pop ebx
        jmp .cl_row
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

; ===========================================================================
; shift_down_from_row  (unchanged)
; ===========================================================================
shift_down_from_row:
        pushad
        mov ecx, ebx
.sd_rows:
        cmp ecx, 0
        jle .sd_clear_top
        mov edx, ecx
        dec edx
        xor esi, esi
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

; ===========================================================================
; update_speed  (unchanged)
; ===========================================================================
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

; ===========================================================================
; random_piece  (unchanged)
; ===========================================================================
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
        mov eax, edx
        pop edx
        pop ebx
        ret

; ===========================================================================
; render_frame  (VBE rewrite)
; ===========================================================================
render_frame:
        pushad

        ; Background
        xor ebx, ebx
        xor ecx, ecx
        mov edx, SCREEN_W
        mov esi, SCREEN_H
        mov edi, C_BG
        call fb_fill_rect

        call draw_border_vbe
        call draw_board_vbe
        call draw_current_piece_vbe
        call draw_next_piece_vbe
        call draw_ui_vbe

        popad
        ret

; ===========================================================================
; draw_border_vbe
; ===========================================================================
draw_border_vbe:
        pushad

        ; Left wall
        mov ebx, BOARD_PX_X - 2
        mov ecx, BOARD_PX_Y - 2
        mov edx, 2
        mov esi, BOARD_H * CELL + 4
        mov edi, C_BORDER
        call fb_fill_rect

        ; Right wall
        mov ebx, BOARD_PX_X + BOARD_W * CELL
        mov ecx, BOARD_PX_Y - 2
        mov edx, 2
        mov esi, BOARD_H * CELL + 4
        mov edi, C_BORDER
        call fb_fill_rect

        ; Top wall
        mov ebx, BOARD_PX_X - 2
        mov ecx, BOARD_PX_Y - 2
        mov edx, BOARD_W * CELL + 4
        mov esi, 2
        mov edi, C_BORDER
        call fb_fill_rect

        ; Bottom wall
        mov ebx, BOARD_PX_X - 2
        mov ecx, BOARD_PX_Y + BOARD_H * CELL
        mov edx, BOARD_W * CELL + 4
        mov esi, 2
        mov edi, C_BORDER
        call fb_fill_rect

        popad
        ret

; ===========================================================================
; draw_board_vbe — draw all locked cells
; ===========================================================================
draw_board_vbe:
        pushad
        xor ebp, ebp            ; linear cell index 0..BOARD_CELLS-1

.dbv_loop:
        cmp ebp, BOARD_CELLS
        jge .dbv_done

        ; col = ebp % BOARD_W,  row = ebp / BOARD_W
        mov eax, ebp
        xor edx, edx
        mov ecx, BOARD_W
        div ecx                 ; eax = row, edx = col

        ; pixel x = col*CELL + BOARD_PX_X  → EBX
        imul ebx, edx, CELL
        add ebx, BOARD_PX_X

        ; pixel y = row*CELL + BOARD_PX_Y  → ECX
        imul ecx, eax, CELL
        add ecx, BOARD_PX_Y

        ; cell value
        movzx esi, byte [board + ebp]
        test esi, esi
        jz .dbv_empty
        dec esi
        mov edi, [piece_rgb + esi * 4]
        jmp .dbv_fill

.dbv_empty:
        mov edi, C_GRID

.dbv_fill:
        mov edx, CELL - 1
        mov esi, CELL - 1
        call fb_fill_rect       ; preserves EBP via pushad/popad
        inc ebp
        jmp .dbv_loop

.dbv_done:
        popad
        ret

; ===========================================================================
; draw_current_piece_vbe
; ===========================================================================
draw_current_piece_vbe:
        pushad

        mov eax, [cur_type]
        mov edi, [piece_table + eax * 4]
        mov eax, [cur_rot]
        and eax, 3
        shl eax, 3
        add edi, eax

        mov eax, [cur_type]
        mov ebp, [piece_rgb + eax * 4]

        xor ecx, ecx
.dcp_loop:
        cmp ecx, 4
        jge .dcp_done

        push ecx
        push edi                  ; save piece data ptr
        movzx eax, byte [edi]
        add eax, [cur_x]
        movzx edx, byte [edi + 1]
        add edx, [cur_y]

        cmp edx, 0
        jl .dcp_next
        cmp edx, BOARD_H
        jge .dcp_next

        ; pixel x, y
        imul ebx, eax, CELL
        add ebx, BOARD_PX_X

        imul ecx, edx, CELL
        add ecx, BOARD_PX_Y

        mov edx, CELL - 1
        mov esi, CELL - 1
        mov edi, ebp
        call fb_fill_rect

.dcp_next:
        pop edi                   ; restore piece data ptr
        add edi, 2
        pop ecx
        inc ecx
        jmp .dcp_loop

.dcp_done:
        popad
        ret

; ===========================================================================
; draw_next_piece_vbe
; ===========================================================================
draw_next_piece_vbe:
        pushad

        ; Clear 4x4 preview area
        mov ebx, NEXT_PX_X
        mov ecx, NEXT_PX_Y
        mov edx, 4 * CELL
        mov esi, 4 * CELL
        mov edi, C_BG
        call fb_fill_rect

        mov eax, [next_type]
        mov edi, [piece_table + eax * 4]   ; rotation 0
        mov ebp, [piece_rgb + eax * 4]

        xor ecx, ecx
.dnp_loop:
        cmp ecx, 4
        jge .dnp_done

        push ecx
        push edi                  ; save piece data ptr
        movzx eax, byte [edi]
        movzx edx, byte [edi + 1]

        imul ebx, eax, CELL
        add ebx, NEXT_PX_X

        imul ecx, edx, CELL
        add ecx, NEXT_PX_Y

        mov edx, CELL - 1
        mov esi, CELL - 1
        mov edi, ebp
        call fb_fill_rect

        pop edi                   ; restore piece data ptr
        add edi, 2
        pop ecx
        inc ecx
        jmp .dnp_loop

.dnp_done:
        popad
        ret

; ===========================================================================
; draw_ui_vbe
; ===========================================================================
draw_ui_vbe:
        pushad

        mov ebx, UI_X
        mov ecx, 10
        mov esi, ui_title
        mov edi, C_TITLE
        call fb_draw_text

        mov ebx, NEXT_PX_X
        mov ecx, NEXT_PX_Y - 20
        mov esi, ui_next
        mov edi, C_TEXT
        call fb_draw_text

        ; Score
        mov ebx, UI_X
        mov ecx, UI_Y_SCORE
        mov esi, ui_score
        mov edi, C_TEXT
        call fb_draw_text

        mov eax, [score]
        mov ebx, UI_X
        mov ecx, UI_Y_SCORE + 14
        mov edi, 0xFFFF44
        call fb_draw_num

        ; Level
        mov ebx, UI_X
        mov ecx, UI_Y_LEVEL
        mov esi, ui_level
        mov edi, C_TEXT
        call fb_draw_text

        mov eax, [level]
        mov ebx, UI_X
        mov ecx, UI_Y_LEVEL + 14
        mov edi, 0x44CCFF
        call fb_draw_num

        ; Lines
        mov ebx, UI_X
        mov ecx, UI_Y_LINES
        mov esi, ui_lines
        mov edi, C_TEXT
        call fb_draw_text

        mov eax, [lines_total]
        mov ebx, UI_X
        mov ecx, UI_Y_LINES + 14
        mov edi, 0x44FF44
        call fb_draw_num

        ; Controls
        mov ebx, UI_X
        mov ecx, UI_Y_CTRL
        mov esi, ui_ctrl1
        mov edi, 0x888888
        call fb_draw_text

        mov ebx, UI_X
        mov ecx, UI_Y_CTRL + 16
        mov esi, ui_ctrl2
        mov edi, 0x888888
        call fb_draw_text

        mov ebx, UI_X
        mov ecx, UI_Y_CTRL + 32
        mov esi, ui_ctrl3
        mov edi, 0x888888
        call fb_draw_text

        mov ebx, UI_X
        mov ecx, UI_Y_CTRL + 48
        mov esi, ui_ctrl4
        mov edi, 0x888888
        call fb_draw_text

        popad
        ret

; ===========================================================================
; game_over_loop  (VBE rewrite)
; ===========================================================================
game_over_loop:
        call render_frame

        ; Red banner
        mov ebx, BOARD_PX_X + 10
        mov ecx, BOARD_PX_Y + BOARD_H * CELL / 2 - 20
        mov edx, BOARD_W * CELL - 20
        mov esi, 50
        mov edi, C_GOBOX
        call fb_fill_rect

        mov ebx, BOARD_PX_X + 20
        mov ecx, BOARD_PX_Y + BOARD_H * CELL / 2 - 10
        mov esi, msg_game_over
        mov edi, C_GOTEXT
        call fb_draw_text

        mov ebx, BOARD_PX_X + 10
        mov ecx, BOARD_PX_Y + BOARD_H * CELL / 2 + 20
        mov esi, msg_restart
        mov edi, C_TEXT
        call fb_draw_text

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
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ===========================================================================
; VBE HELPERS
; ===========================================================================

fb_fill_rect:
        pushad
        test edx, edx
        jz .ffr_done
        test esi, esi
        jz .ffr_done
        mov eax, ecx
        imul eax, [fb_pitch]
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
.ffr_row:
        push eax
        push edx
        mov ecx, edx
.ffr_col:
        mov [eax], edi
        add eax, 4
        dec ecx
        jnz .ffr_col
        pop edx
        pop eax
        add eax, [fb_pitch]
        dec esi
        jnz .ffr_row
.ffr_done:
        popad
        ret

fb_draw_text:
        pushad
        mov edx, ecx
        mov ecx, ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        int 0x80
        popad
        ret

itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        dec edi
        test eax, eax
        jnz .itoa_d
        mov byte [edi], '0'
        dec edi
        jmp .itoa_cp
.itoa_d:
        mov ecx, 10
.itoa_lp:
        test eax, eax
        jz .itoa_cp
        xor edx, edx
        div ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .itoa_lp
.itoa_cp:
        inc edi
        mov esi, edi
        mov edi, num_buf
.itoa_mv:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        test al, al
        jnz .itoa_mv
        popad
        ret

fb_draw_num:
        push esi
        push ebx
        push ecx
        push edi
        call itoa
        pop edi
        pop ecx
        pop ebx
        mov esi, num_buf
        call fb_draw_text
        pop esi
        ret

; ===========================================================================
; DATA
; ===========================================================================
ui_title:       db "TETRIS", 0
ui_next:        db "NEXT:", 0
ui_score:       db "Score:", 0
ui_level:       db "Level:", 0
ui_lines:       db "Lines:", 0
ui_ctrl1:       db "L/R: Move", 0
ui_ctrl2:       db "Up: Rotate", 0
ui_ctrl3:       db "Dn: Soft drop", 0
ui_ctrl4:       db "Spc: Hard drop", 0
msg_paused:     db "PAUSED", 0
msg_game_over:  db "GAME OVER", 0
msg_restart:    db "R: Restart  ESC: Quit", 0

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

section .bss
fb_addr:        resd 1
fb_pitch:       resd 1
num_buf:        resb 12
