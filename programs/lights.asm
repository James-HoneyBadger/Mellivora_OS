; lights.asm - Lights Out puzzle
; 5x5 grid of lights. Toggle a light and its 4 neighbors to turn all off.

%include "syscalls.inc"

GRID_DIM    equ 5

start:
        call init_puzzle
        call game_loop
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

init_puzzle:
        ; Default starting configuration (solvable)
        mov byte [grid + 0*5 + 2], 1
        mov byte [grid + 1*5 + 1], 1
        mov byte [grid + 1*5 + 2], 1
        mov byte [grid + 1*5 + 3], 1
        mov byte [grid + 2*5 + 0], 1
        mov byte [grid + 2*5 + 2], 1
        mov byte [grid + 2*5 + 4], 1
        mov byte [grid + 3*5 + 1], 1
        mov byte [grid + 3*5 + 2], 1
        mov byte [grid + 3*5 + 3], 1
        mov byte [grid + 4*5 + 2], 1
        mov dword [move_count], 0
        ret

game_loop:
.loop:
        call draw_grid
        ; Check solved
        call check_solved
        test eax, eax
        jnz .solved
        ; Get input
        mov eax, SYS_PRINT
        mov ebx, msg_ask
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 16
        int 0x80
        ; Parse "RC" format (row/col 1-5)
        movzx eax, byte [input_buf]
        sub eax, '1'
        cmp eax, 4
        ja .bad_input
        mov [sel_row], eax
        movzx eax, byte [input_buf + 1]
        sub eax, '1'
        cmp eax, 4
        ja .bad_input
        mov [sel_col], eax
        call do_toggle
        inc dword [move_count]
        jmp .loop
.bad_input:
        mov eax, SYS_PRINT
        mov ebx, msg_bad
        int 0x80
        jmp .loop
.solved:
        mov eax, SYS_PRINT
        mov ebx, msg_solved
        int 0x80
        mov eax, [move_count]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_moves
        int 0x80
        ret

draw_grid:
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80
        mov ebp, 0
.dr_row:
        cmp ebp, GRID_DIM
        jge .dr_done
        ; Row number
        mov eax, ebp
        add eax, '1'
        push eax
        mov eax, SYS_PUTCHAR
        pop ebx
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        ; Cells
        xor ecx, ecx
.dr_col:
        cmp ecx, GRID_DIM
        jge .dr_nl
        mov eax, ebp
        imul eax, GRID_DIM
        add eax, ecx
        movzx ebx, byte [grid + eax]
        test ebx, ebx
        jz .dr_off
        mov eax, SYS_PUTCHAR
        mov ebx, '#'
        int 0x80
        jmp .dr_sep
.dr_off:
        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80
.dr_sep:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc ecx
        jmp .dr_col
.dr_nl:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc ebp
        jmp .dr_row
.dr_done:
        ret

do_toggle:
        ; Toggle (sel_row, sel_col) and 4 neighbors
        mov ebp, [sel_row]
        mov ecx, [sel_col]
        call toggle_cell
        ; Up
        mov ebp, [sel_row]
        dec ebp
        js .no_up
        mov ecx, [sel_col]
        call toggle_cell
.no_up:
        ; Down
        mov ebp, [sel_row]
        inc ebp
        cmp ebp, GRID_DIM
        jge .no_dn
        mov ecx, [sel_col]
        call toggle_cell
.no_dn:
        ; Left
        mov ebp, [sel_row]
        mov ecx, [sel_col]
        dec ecx
        js .no_lf
        call toggle_cell
.no_lf:
        ; Right
        mov ebp, [sel_row]
        mov ecx, [sel_col]
        inc ecx
        cmp ecx, GRID_DIM
        jge .no_rt
        call toggle_cell
.no_rt:
        ret

toggle_cell:
        ; EBP=row, ECX=col
        mov eax, ebp
        imul eax, GRID_DIM
        add eax, ecx
        xor byte [grid + eax], 1
        ret

check_solved:
        ; Returns EAX=1 if all zeros
        xor ecx, ecx
        xor ebp, ebp
.cs:
        cmp ebp, 25
        jge .all_off
        movzx eax, byte [grid + ebp]
        test eax, eax
        jnz .not_solved
        inc ebp
        jmp .cs
.not_solved:
        xor eax, eax
        ret
.all_off:
        mov eax, 1
        ret


msg_hdr:        db "=== LIGHTS OUT ===", 10
                db "  1 2 3 4 5", 10, 0
msg_ask:        db "Toggle (RC, e.g. 23): ", 0
msg_bad:        db "Invalid input. Use two digits 1-5.", 10, 0
msg_solved:     db "Solved! Moves: ", 0
msg_moves:      db 10, 0

grid:           times 25 db 0
sel_row:        dd 0
sel_col:        dd 0
move_count:     dd 0
input_buf:      times 16 db 0
